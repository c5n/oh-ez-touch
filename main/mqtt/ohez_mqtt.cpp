/**
 * @file ohez_mqtt.cpp
 *
 * See ohez_mqtt.hpp.
 *
 * ------------------------------------------------------------------- topics
 *
 * Every topic starts with `<base topic>/<hostname>`, both settings, by default
 * `oheztouch/oheztouch-new`. Two segments rather than one because the default
 * has to be safe: a second panel out of the box would otherwise publish over
 * the first, and the symptom -- an uptime that jumps around -- says nothing
 * about the cause.
 *
 *   <prefix>/status                  online, and offline as the last will
 *   <prefix>/system/name             the hostname
 *   <prefix>/system/target           which board this firmware is for
 *   <prefix>/system/version          e.g. "0.20"
 *   <prefix>/system/build            compiler date and time
 *   <prefix>/system/git              the commit this was built from
 *   <prefix>/system/uptime           seconds since boot
 *   <prefix>/system/heap             free heap in bytes
 *   <prefix>/system/ip               the station address
 *   <prefix>/system/ssid             the network, or the interface name
 *   <prefix>/system/rssi             dBm -- absent on a wired host
 *   <prefix>/system/quality          the same as a percentage
 *   <prefix>/ui/night                ON while the night variant is in effect
 *   <prefix>/sensor/temperature      the BME280, in C, %rH and hPa
 *   <prefix>/sensor/humidity
 *   <prefix>/sensor/pressure
 *   <prefix>/config/<field>          one topic per settings field
 *
 * plus a `<prefix>/ble/...` subtree that is not a fixed list and so is not
 * described here: it is one group of topics per BLE advertiser in range, and
 * ble/ble_scan.cpp owns both its shape and its lifetime through
 * ohez_mqtt_publish_value() and ohez_mqtt_clear_value().
 *
 * And one subscription:
 *
 *   <prefix>/config/<field>/set      write that field
 *
 * The config/ half is not a hand-written list. It is config_fields[] -- the
 * same table the web form and the panel's settings screen walk -- so MQTT is a
 * third front end onto the settings rather than a second copy of them, and the
 * theme is settable over MQTT for the same reason its dropdown exists in the
 * browser. Publishing `<prefix>/config/theme` = "LCARS" changes the look of the
 * panel and is saved; the values, the ranges and the character rules are the
 * ones config_fields.cpp already enforces on the other two front ends.
 *
 * Two rules on that half. A SETTINGS_F_SECRET field is neither published nor
 * settable -- the broker password has no business travelling through the broker
 * it authenticates to. And a SETTINGS_F_RESTART field is stored and saved like
 * any other, but only takes effect on the next boot, exactly as it does from
 * the web form.
 *
 * ------------------------------------------------------------- both targets
 *
 * esp-mqtt, on the panel and in the simulator both. components/mqtt compiles
 * for the linux target as it stands, and every socket wait underneath it goes
 * through select(), which is the one thing the FreeRTOS simulator wraps -- so
 * this is the same client on both, the way openhab_http.cpp is the same HTTP
 * client on both. The one symbol IDF does not provide there is supplied by
 * port/linux/port_esp_timer.c, which is a good deal less than a second MQTT
 * implementation would have been.
 *
 * -------------------------------------------------------------------- tasks
 *
 * esp-mqtt runs a task of its own and calls the event handler on it. That task
 * must not touch Config, LVGL or the display, so the handler below does two
 * things and no more: it sets a flag, or it puts an arrived message on a queue.
 * Everything else happens in ohez_mqtt_loop(), on the task that owns all three.
 * Same reasoning, and the same shape, as openhab_ui_request_theme().
 *
 * TLS is deliberately absent: CONFIG_MQTT_TRANSPORT_SSL is off, and the client
 * connects over plain TCP. Adding it means a certificate to store and a place
 * to configure it from, which is a change to the settings and not just to this
 * file.
 */

#include "ohez_mqtt.hpp"

#include "config/config_fields.hpp"
#include "debug.h"
#include "port/port_net.h"
#include "port/port_sys.h"
#include "ui/openhab_ui.hpp"
#include "version.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include "esp_log.h"
#include "mqtt_client.h"

/* Set by the top-level CMakeLists.txt; the fallback is main.cpp's. */
#ifndef TARGET_NAME
#define TARGET_NAME "unknown"
#endif

static const char *TAG = "ohez_mqtt";

/* The prefix is `<base topic>/<hostname>`, both char[32] in Config. */
#define MQTT_PREFIX_MAX 72

/* A whole topic: the prefix, and the longest suffix below -- which is
 * "config/<field name>/set". Sized with room to spare, and separately from the
 * prefix so that appending a suffix to a full-length one is provably not a
 * truncation; the build treats -Wformat-truncation as an error. */
#define MQTT_TOPIC_MAX 128

/* Wide enough for every value published here: the longest is the build stamp,
 * "Mmm dd yyyy hh:mm:ss", and the longest that can arrive is a 63 character
 * text field. */
#define MQTT_VALUE_MAX 80

/* Arrived messages waiting for the application task. Eight is well past what
 * anyone sets by hand; the depth exists so that a broker replaying a handful of
 * retained commands at connect time does not lose any. */
#define MQTT_COMMAND_QUEUE_DEPTH 8

/* Half a minute, against esp-mqtt's 120 second default. The broker declares us
 * dead after one and a half of these, and a panel that has silently dropped off
 * the network should not still read as online for three minutes. */
#define MQTT_KEEPALIVE_S 30

/* One field name plus the value published to it. */
struct mqtt_command_s
{
    char field[24];
    char value[MQTT_VALUE_MAX];
};

/* The settings the running client is working from: a snapshot rather than a
 * pointer into Config, so that publishing from this task cannot read a field
 * that the web server's task is halfway through writing.
 *
 * general is in here for its hostname, which is half of every topic and the
 * client id besides. */
struct mqtt_applied_s
{
    decltype(Config::item.mqtt)    mqtt;
    decltype(Config::item.general) general;
};

/* Whether the two would need different connections.
 *
 * Field by field rather than memcmp(), because the answer is not "did anything
 * change": the publish interval and the retain flag are read on every publish
 * and change nothing about the connection, so a save that touches only those
 * must not drop it. Everything else here either goes into the CONNECT packet or
 * into the topic names, and there is no way to change one of those without
 * reconnecting. */
static bool connection_differs(const struct mqtt_applied_s &a, const struct mqtt_applied_s &b)
{
    return a.mqtt.enabled != b.mqtt.enabled || a.mqtt.port != b.mqtt.port
           || strcmp(a.mqtt.hostname, b.mqtt.hostname) != 0
           || strcmp(a.mqtt.user, b.mqtt.user) != 0
           || strcmp(a.mqtt.password, b.mqtt.password) != 0
           || strcmp(a.mqtt.topic, b.mqtt.topic) != 0
           || strcmp(a.general.hostname, b.general.hostname) != 0;
}

static Config *mqtt_config = NULL;

static esp_mqtt_client_handle_t mqtt_client = NULL;
static struct mqtt_applied_s    applied;
static bool                     applied_valid = false;

static char mqtt_prefix[MQTT_PREFIX_MAX];
static char mqtt_will_topic[MQTT_TOPIC_MAX];
static char mqtt_command_filter[MQTT_TOPIC_MAX];

static QueueHandle_t mqtt_commands = NULL;

/* Written by the client's task, read by the application's. volatile because
 * that hand-off is the whole of what they share: without it the loop's read is
 * a candidate for being hoisted out of the loop it sits in. */
static volatile bool mqtt_online = false;
static volatile bool mqtt_announce_pending = false;

/* Set by ohez_mqtt_request_reconfigure(), which is reachable from the web
 * server's task. */
static volatile bool mqtt_reconfigure_pending = false;

static uint64_t mqtt_system_deadline = 0;

/* ------------------------------------------------------------------- topics */

/* Make `s` usable as a published topic name, in place.
 *
 * '+' and '#' are the subscription wildcards; a broker rejects a PUBLISH whose
 * topic contains either, so a base topic with one in it would stop everything
 * with no obvious cause. Runs of '/' and a leading or trailing one go the same
 * way: they are legal MQTT, addressing an empty segment, and nobody who typed
 * "home/panels/" meant to.
 */
static void topic_sanitise(char *s)
{
    char *w = s;
    bool  after_slash = true; /* true at the start, so a leading '/' is dropped */

    for (const char *r = s; *r != '\0'; r++)
    {
        if (*r == '+' || *r == '#')
            continue;

        if (*r == '/')
        {
            if (after_slash == true)
                continue;

            after_slash = true;
        }
        else
        {
            after_slash = false;
        }

        *w++ = *r;
    }

    while (w > s && w[-1] == '/')
        w--;

    *w = '\0';
}

static void topics_build(const struct mqtt_applied_s &a)
{
    snprintf(mqtt_prefix, sizeof(mqtt_prefix), "%s/%s", a.mqtt.topic, a.general.hostname);
    topic_sanitise(mqtt_prefix);

    /* Both settings can be emptied, and a topic name may not be empty. */
    if (mqtt_prefix[0] == '\0')
        strlcpy(mqtt_prefix, "oheztouch", sizeof(mqtt_prefix));

    snprintf(mqtt_will_topic, sizeof(mqtt_will_topic), "%s/status", mqtt_prefix);
    snprintf(mqtt_command_filter, sizeof(mqtt_command_filter), "%s/config/+/set", mqtt_prefix);
}

/* ---------------------------------------------------------------- publishing */

/* QoS 0 throughout, and retained when the setting says so.
 *
 * Nothing published here is a command or an event: every topic carries the
 * current value of something, so a subscriber that missed an update wants the
 * newest one and not the one it missed. That is what retain gives it, at the
 * broker, for free. A QoS 1 outbox would instead hold readings back to
 * redeliver them, and a reading redelivered a minute late is worse than one
 * dropped -- it is indistinguishable from a fresh one.
 */
static bool publish_raw(const char *suffix, const char *value, bool retain)
{
    char topic[MQTT_TOPIC_MAX];

    if (mqtt_client == NULL)
        return false;

    snprintf(topic, sizeof(topic), "%s/%s", mqtt_prefix, suffix);

#if CONFIG_OHEZ_DEBUG_MQTT
    printf("ohez_mqtt: %s = %s\r\n", topic, value);
#endif

    /* A length of 0 tells esp-mqtt to take strlen(), which for the empty
     * string a clear sends is a genuinely zero-length payload -- the thing
     * that removes a retained message. */
    if (esp_mqtt_client_publish(mqtt_client, topic, value, 0, 0, retain ? 1 : 0) < 0)
    {
        ESP_LOGW(TAG, "cannot publish %s", topic);
        return false;
    }

    return true;
}

static void publish(const char *suffix, const char *value)
{
    publish_raw(suffix, value, applied.mqtt.retain);
}

/* The settings, as the browser and the panel show them. Secrets excluded --
 * see SETTINGS_F_SECRET. */
static void publish_config(const config_item_t &item)
{
    for (size_t i = 0; i < config_field_count; i++)
    {
        const struct config_field_s *f = &config_fields[i];
        char                         suffix[40];
        char                         value[MQTT_VALUE_MAX];

        if (f->kind == SETTINGS_SECTION || (f->flags & SETTINGS_F_SECRET))
            continue;

        snprintf(suffix, sizeof(suffix), "config/%s", f->name);
        config_field_value_text(f, &item, value, sizeof(value));
        publish(suffix, value);
    }
}

/* What does not change while the firmware runs, plus the "online" that pairs
 * with the last will. Published once per connection. */
static void publish_identity(const config_item_t &item)
{
    char value[MQTT_VALUE_MAX];

    publish("status", "online");
    publish("system/name", item.general.hostname);
    publish("system/target", TARGET_NAME);

    snprintf(value, sizeof(value), "%u.%02u", VERSION_MAJOR, VERSION_MINOR);
    publish("system/version", value);

    snprintf(value, sizeof(value), "%s %s", __DATE__, __TIME__);
    publish("system/build", value);

    publish("system/git", VERSION_GIT_HASH);
}

static void publish_system(Config &config)
{
    char            value[MQTT_VALUE_MAX];
    port_net_info_t net;

    /* port_millis() is the uptime: monotonic since boot, and nothing resets
     * it. Same source as the Info tab's row and the web status page's. */
    snprintf(value, sizeof(value), "%llu", (unsigned long long)(port_millis() / 1000));
    publish("system/uptime", value);

    snprintf(value, sizeof(value), "%u", (unsigned)port_free_heap());
    publish("system/heap", value);

    port_net_info(&net);

    publish("system/ip", net.ip);
    publish("system/ssid", net.ssid);

    /* Skipped rather than faked on a wired host. The topics are typed by
     * whatever subscribes to them -- an openHAB Number item, most likely -- and
     * a "wired" or a plausible 0 dBm arriving there is worse than the topic
     * simply not existing. port_net.h makes the same argument. */
    if (net.rssi != PORT_NET_RSSI_WIRED)
    {
        snprintf(value, sizeof(value), "%d", (int)net.rssi);
        publish("system/rssi", value);

        snprintf(value, sizeof(value), "%u", (unsigned)openhab_ui_signal_quality(net.rssi));
        publish("system/quality", value);
    }

    /* Not a setting, which is why it is not under config/: with the night mode
     * on "auto" this follows the clock, and a dashboard showing what the panel
     * looks like right now wants the answer rather than the rule. */
    publish("ui/night", openhab_ui_night_active(&config) ? "ON" : "OFF");
}

void ohez_mqtt_publish_bme280(float temperature_c, float humidity_pct, float pressure_hpa)
{
    char value[MQTT_VALUE_MAX];

    if (mqtt_client == NULL || mqtt_online == false)
        return;

    /* "%.3f": three decimals is past what the chip resolves in any of the
     * three, and the broker is the only sink now, so this is the one place a
     * reading is formatted. */
    snprintf(value, sizeof(value), "%.3f", temperature_c);
    publish("sensor/temperature", value);

    snprintf(value, sizeof(value), "%.3f", humidity_pct);
    publish("sensor/humidity", value);

    snprintf(value, sizeof(value), "%.3f", pressure_hpa);
    publish("sensor/pressure", value);
}

bool ohez_mqtt_connected(void)
{
    return mqtt_client != NULL && mqtt_online;
}

bool ohez_mqtt_publish_value(const char *suffix, const char *value)
{
    if (mqtt_online == false)
        return false;

    return publish_raw(suffix, value, applied.mqtt.retain);
}

bool ohez_mqtt_clear_value(const char *suffix)
{
    if (mqtt_online == false)
        return false;

    return publish_raw(suffix, "", true);
}

/* ------------------------------------------------------------------ commands */

static const struct config_field_s *field_by_name(const char *name)
{
    for (size_t i = 0; i < config_field_count; i++)
    {
        /* A section row has no name at all, so the comparison has to be
         * skipped and not merely fail. */
        if (config_fields[i].kind == SETTINGS_SECTION)
            continue;

        if (strcmp(config_fields[i].name, name) == 0)
            return &config_fields[i];
    }

    return NULL;
}

/* A checkbox over MQTT. Accepts what a broker's other publishers are likely to
 * be sending: openHAB's ON/OFF, a JSON true/false, and a bare 1/0. Anything
 * else is off, which is the same direction the web form's absent checkbox
 * takes. */
static bool value_is_on(const char *value)
{
    return strcasecmp(value, "ON") == 0 || strcasecmp(value, "true") == 0
           || strcasecmp(value, "yes") == 0 || strcmp(value, "1") == 0;
}

/* Runs on the client's task. Copies the message onto the queue and returns;
 * the field lookup and the write to Config happen in ohez_mqtt_loop().
 *
 * mqtt_prefix is read here and written by topics_build(), on the other task --
 * safely, because topics_build() only runs inside client_start(), and by then
 * the client that would have been calling this has been destroyed. */
static void command_enqueue(esp_mqtt_event_handle_t event)
{
    struct mqtt_command_s cmd;
    char                  topic[MQTT_TOPIC_MAX];
    size_t                prefix_len = strlen(mqtt_prefix);
    size_t                len;

    /* A message split over several events carries its topic only in the first
     * one. Nothing published to these topics is anywhere near the 1 KB buffer,
     * so a continuation is a sign of something else entirely. */
    if (event->topic_len <= 0)
    {
        ESP_LOGW(TAG, "ignoring a message fragment (%d bytes)", event->data_len);
        return;
    }

    len = (size_t)event->topic_len;

    if (len >= sizeof(topic))
    {
        ESP_LOGW(TAG, "ignoring a %u byte topic", (unsigned)len);
        return;
    }

    memcpy(topic, event->topic, len);
    topic[len] = '\0';

    /* The subscription is `<prefix>/config/+/set`, so this only fails if a
     * broker sends something that does not match what it was asked for. */
    if (strncmp(topic, mqtt_prefix, prefix_len) != 0
        || strncmp(topic + prefix_len, "/config/", 8) != 0)
    {
        ESP_LOGW(TAG, "unexpected topic %s", topic);
        return;
    }

    {
        const char *name = topic + prefix_len + 8;
        const char *tail = strrchr(name, '/');

        if (tail == NULL || strcmp(tail, "/set") != 0
            || (size_t)(tail - name) >= sizeof(cmd.field))
        {
            ESP_LOGW(TAG, "unexpected topic %s", topic);
            return;
        }

        memcpy(cmd.field, name, (size_t)(tail - name));
        cmd.field[tail - name] = '\0';
    }

    len = (event->data_len > 0) ? (size_t)event->data_len : 0;

    if (len >= sizeof(cmd.value))
        len = sizeof(cmd.value) - 1;

    memcpy(cmd.value, event->data, len);
    cmd.value[len] = '\0';

    /* Dropped rather than waited for: this is the client's task, and blocking
     * it would stop the very loop that empties the queue. */
    if (mqtt_commands == NULL || xQueueSend(mqtt_commands, &cmd, 0) != pdTRUE)
        ESP_LOGW(TAG, "command queue full, dropped %s", cmd.field);
}

/* Apply everything that arrived since the last call. Returns true if any
 * setting actually changed, which is what decides whether the file is
 * rewritten -- a broker replaying a retained command on every reconnect must
 * not mean a flash write on every reconnect. */
static bool commands_apply(Config &config)
{
    struct mqtt_command_s cmd;
    bool                  changed = false;

    if (mqtt_commands == NULL)
        return false;

    while (xQueueReceive(mqtt_commands, &cmd, 0) == pdTRUE)
    {
        const struct config_field_s *f = field_by_name(cmd.field);
        char                         before[MQTT_VALUE_MAX];
        char                         after[MQTT_VALUE_MAX];

        if (f == NULL)
        {
            ESP_LOGW(TAG, "no setting called %s", cmd.field);
            continue;
        }

        if (f->flags & SETTINGS_F_SECRET)
        {
            ESP_LOGW(TAG, "%s is a secret and is not settable over MQTT", cmd.field);
            continue;
        }

        config.lock();

        config_field_value_text(f, &config.item, before, sizeof(before));

        switch (f->kind)
        {
        case SETTINGS_TEXT:
            /* False means the value contained '/' or ':' on a row that forbids
             * them, and the stored value is left alone -- the same silent drop
             * the web form and the settings screen apply. */
            config_field_set_text(f, &config.item, cmd.value);
            break;

        case SETTINGS_BOOL:
            config_field_write(f, &config.item, value_is_on(cmd.value) ? 1 : 0);
            break;

        case SETTINGS_ENUM:
            config_field_set_number(f, &config.item,
                                    config_field_enum_from_name(f, cmd.value));
            break;

        default:
            config_field_set_number(f, &config.item, strtol(cmd.value, NULL, 10));
            break;
        }

        config_field_value_text(f, &config.item, after, sizeof(after));

        config.unlock();

        if (strcmp(before, after) != 0)
        {
            ESP_LOGI(TAG, "%s: %s -> %s", cmd.field, before, after);
            changed = true;
        }
    }

    return changed;
}

/* ------------------------------------------------------------------- events */

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id,
                               void *event_data)
{
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;

    (void)handler_args;
    (void)base;

    switch ((esp_mqtt_event_id_t)event_id)
    {
    case MQTT_EVENT_CONNECTED:
        /* Subscribed from here and not once at start-up, because a
         * subscription belongs to the session: it does not survive a
         * reconnect, and esp-mqtt reconnects by itself. */
        if (esp_mqtt_client_subscribe(event->client, mqtt_command_filter, 0) < 0)
            ESP_LOGW(TAG, "cannot subscribe to %s", mqtt_command_filter);

        mqtt_online = true;
        mqtt_announce_pending = true;
        break;

    case MQTT_EVENT_DISCONNECTED:
        mqtt_online = false;
        break;

    case MQTT_EVENT_DATA:
        command_enqueue(event);
        break;

    case MQTT_EVENT_ERROR:
        /* One line, not the whole esp_mqtt_error_codes_t: the client retries by
         * itself, so this is a breadcrumb for "why is nothing arriving" rather
         * than something to act on. */
        ESP_LOGW(TAG, "transport error (type %d)", (int)event->error_handle->error_type);
        break;

    default:
        break;
    }
}

/* --------------------------------------------------------------- the client */

static void client_stop(void)
{
    if (mqtt_client == NULL)
        return;

    /* A clean disconnect does not fire the last will, so the status topic would
     * otherwise stay "online" after the client was switched off on purpose. */
    if (mqtt_online == true)
        publish("status", "offline");

    esp_mqtt_client_stop(mqtt_client);
    esp_mqtt_client_destroy(mqtt_client);

    mqtt_client = NULL;
    mqtt_online = false;
    mqtt_announce_pending = false;
}

/* Takes the applied snapshot rather than Config, and takes it by reference to
 * the static copy: esp-mqtt does strdup() what it is given, but the client is
 * also the thing that decides when it is time to hand these settings back, and
 * a start from a caller's stack frame would make that a question. */
static void client_start(const struct mqtt_applied_s &a)
{
    esp_mqtt_client_config_t cfg = {};

    topics_build(a);

    cfg.broker.address.hostname = a.mqtt.hostname;
    cfg.broker.address.port = (uint32_t)a.mqtt.port;
    cfg.broker.address.transport = MQTT_TRANSPORT_OVER_TCP;

    /* The panel's own name, so that a broker's client list reads like the
     * installation rather than like a list of chip revisions -- esp-mqtt's
     * default is "ESP32_<3 MAC bytes>", and the simulator has no MAC to build
     * one from at all. */
    cfg.credentials.client_id = a.general.hostname;

    /* Left NULL when unset: esp-mqtt sends a CONNECT with no user name flag,
     * which is what an anonymous broker expects -- an empty string is a
     * zero-length user name, and some brokers reject that. */
    if (a.mqtt.user[0] != '\0')
        cfg.credentials.username = a.mqtt.user;

    if (a.mqtt.password[0] != '\0')
        cfg.credentials.authentication.password = a.mqtt.password;

    /* Always retained, whatever the retain setting says. The point of a last
     * will is to be found by a subscriber that connects after the panel died,
     * and an unretained one is delivered to nobody. */
    cfg.session.last_will.topic = mqtt_will_topic;
    cfg.session.last_will.msg = "offline";
    cfg.session.last_will.msg_len = (int)strlen("offline");
    cfg.session.last_will.qos = 0;
    cfg.session.last_will.retain = 1;
    cfg.session.keepalive = MQTT_KEEPALIVE_S;

    mqtt_client = esp_mqtt_client_init(&cfg);

    if (mqtt_client == NULL)
    {
        ESP_LOGE(TAG, "cannot create a client");
        return;
    }

    esp_mqtt_client_register_event(mqtt_client, MQTT_EVENT_ANY, mqtt_event_handler, NULL);

    esp_err_t err = esp_mqtt_client_start(mqtt_client);

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "cannot start: %s", esp_err_to_name(err));
        esp_mqtt_client_destroy(mqtt_client);
        mqtt_client = NULL;
        return;
    }

    ESP_LOGI(TAG, "publishing to %s under %s", a.mqtt.hostname, mqtt_prefix);
}

/* Bring the client into line with the settings. Restarts it only when
 * something it was started with actually changed: settings_apply_live() runs on
 * every save from either front end, and a save on the openHAB tab must not
 * bounce the broker connection. */
static void reconfigure(Config &config)
{
    struct mqtt_applied_s wanted;

    config.lock();
    memcpy(&wanted.mqtt, &config.item.mqtt, sizeof(wanted.mqtt));
    memcpy(&wanted.general, &config.item.general, sizeof(wanted.general));
    config.unlock();

    bool restart = (applied_valid == false) || connection_differs(applied, wanted);

    /* Before the snapshot is overwritten: the goodbye is published with the
     * prefix and the retain flag the connection was made with. */
    if (restart == true)
        client_stop();

    memcpy(&applied, &wanted, sizeof(applied));
    applied_valid = true;

    if (restart == false)
    {
        /* The connection stays up -- but the save may well have changed a
         * setting that is published, and the broker is holding the old value as
         * a retained message. */
        mqtt_announce_pending = mqtt_online;
        return;
    }

    if (applied.mqtt.enabled == false)
    {
        ESP_LOGI(TAG, "disabled");
        return;
    }

    if (applied.mqtt.hostname[0] == '\0')
    {
        ESP_LOGW(TAG, "enabled with no broker host; not connecting");
        return;
    }

    client_start(applied);
}

/* ---------------------------------------------------------------------- API */

void ohez_mqtt_setup(Config *config)
{
    mqtt_config = config;

    if (mqtt_commands == NULL)
        mqtt_commands = xQueueCreate(MQTT_COMMAND_QUEUE_DEPTH, sizeof(struct mqtt_command_s));

    if (mqtt_commands == NULL)
        ESP_LOGE(TAG, "no command queue; the broker cannot change settings");

    /* Connecting is left to the first loop, which main.cpp only reaches with
     * the link up. Starting here would mean a name resolution failure per
     * second from before the radio has associated. */
    mqtt_reconfigure_pending = true;
}

void ohez_mqtt_request_reconfigure(void)
{
    mqtt_reconfigure_pending = true;
}

void ohez_mqtt_loop(Config &config)
{
    if (mqtt_config == NULL)
        return;

    if (mqtt_reconfigure_pending == true)
    {
        mqtt_reconfigure_pending = false;
        reconfigure(config);
    }

    if (mqtt_client == NULL)
        return;

    /* Before the online check: a command that arrived just before the link
     * dropped is still worth applying, and dropping the queue on a
     * disconnection would lose it. */
    if (commands_apply(config) == true)
    {
        config.lock();
        settings_apply_live(&config);
        config.saveConfig();
        config.unlock();

        /* settings_apply_live() has already asked for a reconfigure, which is
         * what republishes the settings and restarts the client if the broker
         * itself was what changed. */
    }

    if (mqtt_online == false)
        return;

    if (mqtt_announce_pending == true)
    {
        mqtt_announce_pending = false;

        config.lock();
        publish_identity(config.item);
        publish_config(config.item);
        config.unlock();

        /* Straight away rather than a minute from now: a subscriber that has
         * just seen "online" appear should not have to wait for the first
         * uptime. */
        mqtt_system_deadline = 0;
    }

    if (port_millis() >= mqtt_system_deadline)
    {
        int interval = applied.mqtt.interval;

        mqtt_system_deadline = port_millis() + (uint64_t)(interval > 0 ? interval : 60) * 1000;

        publish_system(config);
    }
}
