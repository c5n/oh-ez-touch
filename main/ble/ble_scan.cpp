/**
 * @file ble_scan.cpp
 *
 * See ble_scan.hpp.
 *
 * ------------------------------------------------------------------- topics
 *
 * Under the MQTT client's own prefix, so with the defaults these read
 * `oheztouch/oheztouch-new/ble/...`. The key is the advertiser's hardware
 * address in lower-case hex with no separators, which is the one thing every
 * advertisement carries and which is already a valid topic segment:
 *
 *   ble/count                   advertisers currently published
 *   ble/dropped                 reports lost to a full queue, since boot
 *   ble/<addr>/type             iBeacon, Eddystone-UID, Eddystone-URL, device
 *   ble/<addr>/id               the beacon's own identity, when it has one
 *   ble/<addr>/name             the advertised name, when it has one
 *   ble/<addr>/power            dBm: the power at one metre that a beacon
 *                               format declares, or a plain device's
 *                               advertised transmit power, whichever it has
 *   ble/<addr>/rssi             dBm, the mean over the last scan window
 *   ble/<addr>/distance         metres, estimated -- beacons only, because
 *                               only a beacon states a calibrated power
 *   ble/<addr>/battery          mV, Eddystone-TLM only
 *   ble/<addr>/temperature      degrees Celsius, Eddystone-TLM only
 *
 * The address is the key and the *identity* is a value, which is the other way
 * round from how it is usually drawn. It has to be: an iBeacon's identity is
 * shared on purpose -- a shop's hundred tags carry one UUID and differ only in
 * the minor -- so it is not unique, while the address always is. The
 * consequence is worth knowing before wiring anything up: many beacons
 * randomise their address every few minutes for exactly the privacy reason that
 * makes this awkward, and one of those will come and go under a new key each
 * time. A beacon meant to be tracked advertises a stable address.
 *
 * The first four topics change only when the advertiser changes what it says,
 * so they are published once and on change; the rest go out at the end of every
 * scan window. Same split, and the same reason, as the `system/` topics: an
 * identity is not telemetry.
 *
 * When an advertiser has not been heard for the configured expiry, all ten
 * topics are cleared -- a zero-length retained publish, which is how a retained
 * message is removed. Without that a beacon carried out of the building would
 * sit in the broker at its last RSSI forever, which is worse than no data.
 *
 * ------------------------------------------------------------- the duty cycle
 *
 * A window of `ble.window` seconds every `ble.interval` seconds, five in thirty
 * by default. Not continuous, because the radio is shared with WiFi: the
 * coexistence arbiter interleaves them so neither breaks, but a scan that never
 * stops takes its share of airtime from the openHAB polling and the web
 * interface for the whole time it runs. A beacon advertises several times a
 * second, so five seconds is many reports from everything in range.
 *
 * RSSI is averaged over the window rather than last-one-wins. A single reading
 * moves by five or ten dB between advertisements from a beacon that has not
 * moved at all, and a distance derived from one of those swings by a factor of
 * three. The mean over a window is still noisy; it is not misleading.
 */

#include "ble_scan.hpp"

#include "ble_beacon.hpp"
#include "config/config_fields.hpp" /* config_item_t */
#include "debug.h"
#include "mqtt/ohez_mqtt.hpp"
#include "port/port_ble.h"
#include "port/port_sys.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"

static const char *TAG = "ble_scan";

/**
 * Advertisers tracked at once.
 *
 * A fixed table rather than a setting: it is about 3 KB of static RAM, and the
 * number that matters to a user is the RSSI threshold -- "only things in this
 * room" -- which is a setting and does a better job of keeping the table small
 * than a cap on its size would.
 */
#define BLE_SCAN_MAX 24

/* "ble/" + 12 hex + "/" + the longest leaf, which is "temperature". */
#define BLE_TOPIC_MAX 40

#define BLE_VALUE_MAX 64

struct entry_s
{
    bool     used;
    char     key[13]; /* the address as a topic segment: 12 hex plus a NUL */
    uint64_t last_seen;

    /* Accumulated over the window in progress, then collapsed into rssi. */
    int32_t  rssi_sum;
    uint16_t rssi_count;

    int8_t rssi;
    bool   have_rssi;

    /* Merged across advertisements: Eddystone sends its identity and its
     * telemetry in different frames, and a plain device may send its name in
     * one advertisement and its transmit power in another. */
    struct ble_beacon_s beacon;

    /* The identity topics are published once and on change, so the table has
     * to remember whether the broker has them. Cleared for every entry when a
     * connection comes back, because a new session's broker may not. */
    bool identity_published;
    bool identity_dirty;

    /* Whether anything of this entry is on the broker, which decides whether
     * there is anything to clear when it expires or stops qualifying. */
    bool on_broker;
};

static struct entry_s entries[BLE_SCAN_MAX];

static bool     ble_available = false;
static uint64_t next_window = 0;
static bool     window_open = false;
static bool     was_connected = false;

/* ------------------------------------------------------------------ helpers */

static bool publish_leaf(const char *key, const char *leaf, const char *value)
{
    char suffix[BLE_TOPIC_MAX];

    snprintf(suffix, sizeof(suffix), "ble/%s/%s", key, leaf);

    return ohez_mqtt_publish_value(suffix, value);
}

static void clear_leaf(const char *key, const char *leaf)
{
    char suffix[BLE_TOPIC_MAX];

    snprintf(suffix, sizeof(suffix), "ble/%s/%s", key, leaf);

    ohez_mqtt_clear_value(suffix);
}

/* An advertised name is UTF-8 from a device nobody here wrote, and it goes
 * into a payload someone will read in a dashboard. Control characters are
 * dropped -- they are not display, and a name is display or it is nothing. */
static void printable(const char *src, char *dst, size_t size)
{
    size_t n = 0;

    for (; *src != '\0' && n + 1 < size; src++)
        if ((unsigned char)*src >= 0x20 && (unsigned char)*src != 0x7F)
            dst[n++] = *src;

    dst[n] = '\0';
}

/* Whether this entry is one the settings say to publish. A plain BLE device is
 * not a beacon, and a room's worth of phones and watches would otherwise be
 * three hundred topics nobody asked for. */
static bool qualifies(const struct entry_s *e, const config_item_t &item)
{
    if (e->beacon.kind != BLE_BEACON_NONE)
        return true;

    return item.ble.publish_all;
}

/* Lowest score is evicted when the table is full. Beacons outrank plain
 * devices whatever their signal, because a strong phone is not more
 * interesting than a weak beacon; within a class, the stronger signal wins. */
static int32_t score(enum ble_beacon_kind_e kind, int8_t rssi)
{
    return ((kind == BLE_BEACON_NONE) ? 0 : 1000) + rssi;
}

static int32_t entry_score(const struct entry_s *e)
{
    return score(e->beacon.kind, e->have_rssi ? e->rssi : -127);
}

/* -------------------------------------------------------------- the table */

/* Remove everything this entry ever put on the broker. A zero-length retained
 * publish is how a retained message is deleted; without this an advertiser
 * carried out of the building would sit there at its last RSSI forever. */
static void entry_clear_topics(struct entry_s *e)
{
    static const char *const leaves[] = {"type", "id",       "name",    "power",
                                         "rssi", "distance", "battery", "temperature"};

    if (e->on_broker == false)
        return;

    for (size_t i = 0; i < sizeof(leaves) / sizeof(leaves[0]); i++)
        clear_leaf(e->key, leaves[i]);

    e->on_broker = false;
    e->identity_published = false;
}

static void entry_release(struct entry_s *e)
{
    entry_clear_topics(e);

#if CONFIG_OHEZ_DEBUG_BLE
    printf("ble_scan: forgetting %s\r\n", e->key);
#endif

    memset(e, 0, sizeof(*e));
}

/* The entry for this address, creating one if there is room or if something
 * less interesting can be evicted for it. NULL when the table is full of
 * better. */
static struct entry_s *entry_for(const char *key, enum ble_beacon_kind_e kind, int8_t rssi)
{
    struct entry_s *free_slot = NULL;
    struct entry_s *victim = NULL;

    for (size_t i = 0; i < BLE_SCAN_MAX; i++)
    {
        struct entry_s *e = &entries[i];

        if (e->used == false)
        {
            if (free_slot == NULL)
                free_slot = e;

            continue;
        }

        if (strcmp(e->key, key) == 0)
            return e;

        if (victim == NULL || entry_score(e) < entry_score(victim))
            victim = e;
    }

    if (free_slot == NULL)
    {
        /* Only for something the table would rather have. Without this test a
         * crowded room would churn the table endlessly, evicting a beacon for
         * a passing phone and back again, and nothing would ever accumulate a
         * window's worth of readings. */
        if (victim == NULL || score(kind, rssi) <= entry_score(victim))
            return NULL;

        entry_release(victim);
        free_slot = victim;
    }

    memset(free_slot, 0, sizeof(*free_slot));

    free_slot->used = true;
    strlcpy(free_slot->key, key, sizeof(free_slot->key));
    free_slot->identity_dirty = true;

    /* Not the zero a memset leaves: zero dBm is a legal power, and an entry
     * that starts out claiming one would publish it and derive a distance from
     * it. */
    free_slot->beacon.tx_power = BLE_BEACON_POWER_UNKNOWN;
    free_slot->beacon.ref_power = BLE_BEACON_POWER_UNKNOWN;

    return free_slot;
}

/* Fold one advertisement's worth of parse into what is already known. */
static void merge(struct entry_s *e, const struct ble_beacon_s *b)
{
    /* An identity only ever comes from a frame that carries one, so a TLM
     * frame -- or a name-only advertisement from a beacon -- cannot demote a
     * known beacon to a plain device. */
    if (b->kind != BLE_BEACON_NONE)
    {
        if (e->beacon.kind != b->kind || strcmp(e->beacon.id, b->id) != 0)
        {
            e->beacon.kind = b->kind;
            strlcpy(e->beacon.id, b->id, sizeof(e->beacon.id));
            e->identity_dirty = true;
        }
    }

    if (b->name[0] != '\0' && strcmp(e->beacon.name, b->name) != 0)
    {
        strlcpy(e->beacon.name, b->name, sizeof(e->beacon.name));
        e->identity_dirty = true;
    }

    if (b->tx_power != BLE_BEACON_POWER_UNKNOWN && e->beacon.tx_power != b->tx_power)
    {
        e->beacon.tx_power = b->tx_power;
        e->identity_dirty = true;
    }

    if (b->ref_power != BLE_BEACON_POWER_UNKNOWN && e->beacon.ref_power != b->ref_power)
    {
        e->beacon.ref_power = b->ref_power;
        e->identity_dirty = true;
    }

    if (b->have_telemetry == true)
    {
        e->beacon.have_telemetry = true;
        e->beacon.battery_mv = b->battery_mv;
        e->beacon.temperature_c = b->temperature_c;
    }
}

static void collect(const config_item_t &item)
{
    port_ble_adv_t adv;

    while (port_ble_adv_next(&adv) == true)
    {
        struct ble_beacon_s b;
        char                key[13];

        if (adv.rssi == PORT_BLE_RSSI_UNKNOWN || adv.rssi < item.ble.rssi_min)
            continue;

        snprintf(key, sizeof(key), "%02x%02x%02x%02x%02x%02x", adv.addr[0], adv.addr[1],
                 adv.addr[2], adv.addr[3], adv.addr[4], adv.addr[5]);

        ble_beacon_parse(adv.adv, adv.adv_len, &b);

        struct entry_s *e = entry_for(key, b.kind, adv.rssi);

        if (e == NULL)
            continue;

        merge(e, &b);

        e->last_seen = port_millis();
        e->rssi_sum += adv.rssi;
        e->rssi_count++;

#if CONFIG_OHEZ_DEBUG_BLE
        printf("ble_scan: %s %s rssi %d%s%s\r\n", key, ble_beacon_kind_name(b.kind),
               (int)adv.rssi, b.id[0] != '\0' ? " id " : "", b.id);
#endif
    }
}

/* ---------------------------------------------------------------- publish */

static void publish_identity(struct entry_s *e)
{
    char value[BLE_VALUE_MAX];
    bool ok = true;

    ok = publish_leaf(e->key, "type", ble_beacon_kind_name(e->beacon.kind)) && ok;

    /* Published even when empty, so that a subscriber sees the topic exist and
     * can tell "this advertiser has no id" from "this topic never arrived". */
    ok = publish_leaf(e->key, "id", e->beacon.id) && ok;

    printable(e->beacon.name, value, sizeof(value));
    ok = publish_leaf(e->key, "name", value) && ok;

    /* The calibrated one when there is one, and the advertised transmit power
     * otherwise: a reader of this topic wants "how loud is it", and the
     * difference between the two only matters for ranging -- which is why only
     * the calibrated one produces a `distance`. */
    int8_t power = (e->beacon.ref_power != BLE_BEACON_POWER_UNKNOWN) ? e->beacon.ref_power
                                                                    : e->beacon.tx_power;

    if (power != BLE_BEACON_POWER_UNKNOWN)
    {
        snprintf(value, sizeof(value), "%d", (int)power);
        ok = publish_leaf(e->key, "power", value) && ok;
    }
    else
    {
        clear_leaf(e->key, "power");
    }

    if (ok == true)
    {
        e->identity_published = true;
        e->identity_dirty = false;
        e->on_broker = true;
    }
}

static void publish_readings(struct entry_s *e)
{
    char value[BLE_VALUE_MAX];

    snprintf(value, sizeof(value), "%d", (int)e->rssi);

    if (publish_leaf(e->key, "rssi", value) == true)
        e->on_broker = true;

    /* From the calibrated power only. A transmit power would give a number in
     * the right units and the wrong order of magnitude, and nothing downstream
     * could tell which of the two it had been handed. */
    float distance = ble_beacon_distance(e->rssi, e->beacon.ref_power);

    if (distance >= 0.0f)
    {
        /* One decimal. The model is not good for two, and publishing three
         * would invite someone to believe them. */
        snprintf(value, sizeof(value), "%.1f", distance);
        publish_leaf(e->key, "distance", value);
    }
    else
    {
        clear_leaf(e->key, "distance");
    }

    if (e->beacon.have_telemetry == true)
    {
        snprintf(value, sizeof(value), "%u", (unsigned)e->beacon.battery_mv);
        publish_leaf(e->key, "battery", value);

        snprintf(value, sizeof(value), "%.2f", e->beacon.temperature_c);
        publish_leaf(e->key, "temperature", value);
    }
}

/* End of a window: collapse the readings, publish, and forget what has gone. */
static void window_close(const config_item_t &item)
{
    uint64_t now = port_millis();
    uint64_t expire_ms = (uint64_t)item.ble.expire * 1000;
    unsigned tracked = 0;
    char     value[BLE_VALUE_MAX];

    for (size_t i = 0; i < BLE_SCAN_MAX; i++)
    {
        struct entry_s *e = &entries[i];

        if (e->used == false)
            continue;

        if (e->rssi_count > 0)
        {
            /* Rounded, not truncated. These are negative dBm, so half the
             * divisor is *subtracted* before dividing: -67.5 becomes -68 and
             * not the -67 that integer division towards zero would give. */
            int32_t count = (int32_t)e->rssi_count;
            int32_t mean = (e->rssi_sum - count / 2) / count;

            e->rssi = (int8_t)mean;
            e->have_rssi = true;
            e->rssi_sum = 0;
            e->rssi_count = 0;
        }

        if (now - e->last_seen >= expire_ms)
        {
            entry_release(e);
            continue;
        }

        if (qualifies(e, item) == false)
        {
            /* Kept in the table but taken off the broker: the setting has since
             * been turned off, or something that had looked like a beacon
             * turned out not to be. Releasing the entry instead would have it
             * rediscovered on the very next window and cleared again, which is
             * eight publishes per device per window of pure churn. */
            entry_clear_topics(e);
            continue;
        }

        tracked++;

        if (e->identity_published == false || e->identity_dirty == true)
            publish_identity(e);

        if (e->have_rssi == true)
            publish_readings(e);
    }

    snprintf(value, sizeof(value), "%u", tracked);
    ohez_mqtt_publish_value("ble/count", value);

    snprintf(value, sizeof(value), "%u", (unsigned)port_ble_dropped());
    ohez_mqtt_publish_value("ble/dropped", value);
}

/* ---------------------------------------------------------------------- API */

void ble_scan_setup(Config &config)
{
    if (config.item.ble.enabled == false)
        return;

    ble_available = port_ble_init();

    if (ble_available == false)
    {
        /* Not an error to shout about on the host, where there is no radio at
         * all; port_ble_init() has already said which case this is. */
        ESP_LOGI(TAG, "no Bluetooth; not scanning");
        return;
    }

    /* The first window right away rather than one interval from now: a panel
     * that has just been told to scan should show something for it. */
    next_window = 0;
}

void ble_scan_loop(Config &config)
{
    const config_item_t &item = config.item;

    if (ble_available == false)
        return;

    /* A reconnected broker holds none of what the last session published, and
     * the identity topics are the ones that are otherwise never sent again. */
    bool connected = ohez_mqtt_connected();

    if (connected == true && was_connected == false)
        for (size_t i = 0; i < BLE_SCAN_MAX; i++)
            entries[i].identity_published = false;

    was_connected = connected;

    if (window_open == true)
    {
        /* Sampled before the drain, not after: a report queued between the two
         * would otherwise be dropped on the floor along with the window it
         * belongs to. */
        bool finished = (port_ble_scanning() == false);

        collect(item);

        if (finished == true)
        {
            window_open = false;
            window_close(item);
        }

        return;
    }

    if (port_millis() < next_window)
        return;

    uint32_t window_ms = (uint32_t)item.ble.window * 1000;

    next_window = port_millis() + (uint64_t)item.ble.interval * 1000;

    if (port_ble_scan_start(window_ms) == false)
    {
        ESP_LOGW(TAG, "could not start a scan window");
        return;
    }

    window_open = true;

#if CONFIG_OHEZ_DEBUG_BLE
    printf("ble_scan: window of %u ms open\r\n", (unsigned)window_ms);
#endif
}
