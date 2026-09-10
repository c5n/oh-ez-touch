#include "config_fields.hpp"

#include "ui/ui_theme.hpp"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

/* The target has to be an offset rather than a pointer or a lambda: a table of
 * pointers into a global would need dynamic initialisation and would land in
 * RAM, where this one stays in flash. Same reasoning as the uint32_t colours in
 * ui_style.hpp. */
#define OFF(path) ((uint16_t)offsetof(config_item_t, path))
#define SZ(path) ((uint8_t)sizeof(((config_item_t *)0)->path))

/* Shorthand, so that a row fits on one line and the table can be read against
 * the settings tables in README.md. Only SEC() names a tab; the rows under it
 * inherit it.
 *
 * Every value row now also carries where it lives in config.json (`jp`/`jk`)
 * and what it is worth when the file does not say (`dv`). Those two used to
 * be a second and third hand-written copy of this list, in Config::loadConfig()
 * and Config::saveConfig(); they are read straight off these rows instead, so
 * a setting added here is stored and restored without touching config.cpp.
 *
 *   nm   POST argument name        lbl  label shown in both front ends
 *   fld  the member of Config::item jp   containing JSON object, '/' nests
 *   jk   key inside that object     dv   the built-in default
 *   lo/hi  accepted range, clamped  fl   SETTINGS_F_* flags
 */
#define SEC(lbl, tb) \
    {NULL, (lbl), NULL, NULL, NULL, NULL, 0, 0, 0, 0, SETTINGS_SECTION, 0, 0, 0, (tb)}
#define TXT(nm, lbl, fld, jp, jk, dv, fl) \
    {(nm), (lbl), NULL, (jp), (jk), (dv), 0, OFF(fld), 0, 0, SETTINGS_TEXT, SZ(fld), 0, (fl), 0}
#define SINT(nm, lbl, fld, jp, jk, dv, lo, hi) \
    {(nm), (lbl), NULL, (jp), (jk), NULL, (dv), OFF(fld), (lo), (hi), SETTINGS_INT, 0, 0, 0, 0}
#define UINT(nm, lbl, fld, jp, jk, dv, lo, hi) \
    {(nm), (lbl), NULL, (jp), (jk), NULL, (dv), OFF(fld), (lo), (hi), SETTINGS_UINT, 0, 0, 0, 0}
#define ULNG(nm, lbl, fld, jp, jk, dv, lo, hi) \
    {(nm), (lbl), NULL, (jp), (jk), NULL, (dv), OFF(fld), (lo), (hi), SETTINGS_ULONG, 0, 0, 0, 0}
#define CHK(nm, lbl, fld, jp, jk, dv, fl) \
    {(nm), (lbl), NULL, (jp), (jk), NULL, (dv), OFF(fld), 0, 1, SETTINGS_BOOL, 0, 0, (fl), 0}
#define SEL(nm, lbl, fld, jp, jk, dv, tbl, n) \
    {(nm), (lbl), (tbl), (jp), (jk), (dv), 0, OFF(fld), 0, (n) - 1, SETTINGS_ENUM, 0, (n), 0, 0}

/* The SETTINGS_F_RESTART flags say what the code actually does, which is not
 * what they used to say. settings_apply_live() re-applies the openHAB endpoint,
 * the backlight timings, the beeper, the theme and the MQTT client; the NTP
 * host, offset and DST are live already, because openhab_ui_loop() re-issues
 * configTime() off the live Config every NTP_TIME_UPDATE_INTERVAL. That leaves
 * two:
 *
 *   hostname -- WiFi.setHostname() runs before WiFi.mode() in wlan_setup(),
 *               and the name doubles as the setup access point's SSID.
 *   bme_use  -- honoured only in sensor_main_setup().
 *   ble_use  -- honoured only in ble_scan_setup(), and for a harder reason
 *               than the other two: bringing the Bluetooth controller up
 *               claims tens of kilobytes of RAM that stopping it does not
 *               give back, so a panel that is not scanning must never have
 *               started it.
 *
 * Getting this right matters more than it used to: the touch screen offers a
 * restart when a flagged field changes, and a flag on a field that is in fact
 * live would ask for a reboot on nearly every save. */
const struct config_field_s config_fields[] = {

    SEC("Device", SETTINGS_TAB_DEVICE),
    TXT("hostname", "Hostname", general.hostname, "general", "hostname", "oheztouch-new",
        SETTINGS_F_HOSTCHARS | SETTINGS_F_RESTART),

    SEC("NTP Time", SETTINGS_TAB_TIME),
    TXT("ntp_host", "Host", ntp.hostname, "ntp", "hostname", "pool.ntp.org",
        SETTINGS_F_HOSTCHARS),
    SINT("ntp_gmt", "GMT offset [h]", ntp.gmt_offset, "ntp", "gmt_offset", 1, -12, 14),
    CHK("ntp_dst", "Daylight saving (+1h)", ntp.daylightsaving, "ntp", "daylightsaving", 0, 0),

    SEC("Appearance", SETTINGS_TAB_THEME),
    /* The option names come straight from ui_theme.hpp, so the dropdown, the
     * config file and the simulator's environment variables cannot drift
     * apart. Unlike the AutoConnect version this needs no 1-based index
     * arithmetic: the POST carries the name, and the lookup owns the
     * fallback.
     *
     * Stored by name for the same reason: a file written by one firmware and
     * read by another that has since gained a theme must not silently select
     * a different one because the numbering moved. */
    SEL("theme", "Theme", ui.theme, "ui", "theme", UI_THEME_NAME_DEFAULT,
        ui_theme_names, UI_THEME_FAMILY_COUNT),
    SEL("night_mode", "Night mode", ui.night_mode, "ui", "night_mode", UI_NIGHT_NAME_OFF,
        ui_night_mode_names, UI_NIGHT_MODE_COUNT),
    UINT("night_from", "Night from [h]", ui.night_from, "ui", "night_from", 22, 0, 23),
    UINT("night_to", "Night to [h]", ui.night_to, "ui", "night_to", 6, 0, 23),

    SEC("LCD Backlight Dimming", SETTINGS_TAB_THEME),
    ULNG("bl_timeout", "Activity timeout [s] (0=off)", backlight.activity_timeout,
         "backlight", "activity_timeout", 60, 0, 86400),
    UINT("bl_normal", "Normal brightness [%]", backlight.normal_brightness,
         "backlight", "normal_brightness", 100, 0, 100),
    UINT("bl_dim", "Dim brightness [%]", backlight.dim_brightness,
         "backlight", "dim_brightness", 40, 0, 100),

    SEC("Beeper", SETTINGS_TAB_AUDIO),
    CHK("beeper", "Enable beeper", beeper.enabled, "beeper", "enabled", 1, 0),

    SEC("OpenHAB Server", SETTINGS_TAB_OPENHAB),
    TXT("oh_host", "Host", openhab.hostname, "openhab", "hostname", "openhabian",
        SETTINGS_F_HOSTCHARS),
    SINT("oh_port", "Port", openhab.port, "openhab", "port", 8080, 1, 65535),
    TXT("oh_sitemap", "Sitemap", openhab.sitemap, "openhab", "sitemap", "setme_sitemap",
        SETTINGS_F_HOSTCHARS),

    SEC("MQTT Broker", SETTINGS_TAB_MQTT),
    /* Off by default: a device that has never been told about a broker must
     * not spend every boot resolving "mosquitto" and logging the failure. */
    CHK("mqtt_use", "Enable MQTT", mqtt.enabled, "mqtt", "enabled", 0, 0),
    TXT("mqtt_host", "Host", mqtt.hostname, "mqtt", "hostname", "mosquitto",
        SETTINGS_F_HOSTCHARS),
    SINT("mqtt_port", "Port", mqtt.port, "mqtt", "port", 1883, 1, 65535),
    TXT("mqtt_user", "User (empty: none)", mqtt.user, "mqtt", "user", "", 0),
    TXT("mqtt_pass", "Password", mqtt.password, "mqtt", "password", "", SETTINGS_F_SECRET),

    SEC("MQTT Publishing", SETTINGS_TAB_MQTT),
    /* No SETTINGS_F_HOSTCHARS: a base topic of "home/panels" is a reasonable
     * thing to want, and '/' is what makes it one. The client rejects the two
     * characters that would actually break a topic -- '+' and '#', the
     * subscription wildcards -- when it assembles the prefix. */
    TXT("mqtt_topic", "Base topic", mqtt.topic, "mqtt", "topic", "oheztouch", 0),
    SINT("mqtt_interval", "Publish interval [s]", mqtt.interval, "mqtt", "interval",
         60, 5, 86400),
    CHK("mqtt_retain", "Retain published values", mqtt.retain, "mqtt", "retain", 1, 0),

    SEC("Sensors", SETTINGS_TAB_SENSORS),
    /* The one two-level path in the table: the file has always nested the
     * BME280 under "sensors", against the day a second chip joins it. */
    CHK("bme_use", "Use BME280 sensor", sensors.bme280.use, "sensors/bme280", "use", 0,
        SETTINGS_F_RESTART),
    SINT("bme_interval", "Update interval [s]", sensors.bme280.interval,
         "sensors/bme280", "interval", 180, 1, 86400),

    /* On the Sensors page rather than one of its own: a beacon scanner is a
     * presence sensor, which is what that page is for. */
    SEC("Bluetooth LE Beacons", SETTINGS_TAB_SENSORS),
    CHK("ble_use", "Scan for BLE beacons", ble.enabled, "ble", "enabled", 0,
        SETTINGS_F_RESTART),
    SINT("ble_interval", "Scan every [s]", ble.interval, "ble", "interval", 30, 5, 3600),
    SINT("ble_window", "Scan for [s]", ble.window, "ble", "window", 5, 1, 60),
    SINT("ble_rssi", "Ignore weaker than [dBm]", ble.rssi_min, "ble", "rssi_min",
         -90, -100, 0),
    SINT("ble_expire", "Forget after [s]", ble.expire, "ble", "expire", 120, 10, 86400),
    CHK("ble_all", "Publish non-beacon devices", ble.publish_all, "ble", "publish_all", 0, 0),
};

const size_t config_field_count = sizeof(config_fields) / sizeof(config_fields[0]);

/* The generic accessors below reach into Config by offset, so a field whose C
 * type stops matching its kind would corrupt its neighbours rather than fail
 * to compile. These are the checks that keep the table honest. */
static_assert(sizeof(int) == sizeof(int32_t), "SETTINGS_INT width");
static_assert(sizeof(enum ui_theme_family_e) == sizeof(unsigned int), "SETTINGS_ENUM width");
static_assert(sizeof(enum ui_night_mode_e) == sizeof(unsigned int), "SETTINGS_ENUM width");

static_assert(sizeof(config_fields) / sizeof(config_fields[0]) <= SETTINGS_MAX_FIELDS,
              "more settings rows than the settings screen and the web form are sized for");

uint8_t config_field_tab(size_t index)
{
    /* Walk back to the nearest section rather than storing the tab on every
     * row: a section and its fields cannot then disagree. */
    for (size_t i = index + 1; i > 0; i--)
        if (config_fields[i - 1].kind == SETTINGS_SECTION)
            return config_fields[i - 1].tab;

    return SETTINGS_TAB_COUNT;
}

int32_t config_field_read(const struct config_field_s *f, const config_item_t *item)
{
    const void *p = (const uint8_t *)item + f->offset;

    switch (f->kind)
    {
    case SETTINGS_INT:
        return (int32_t) * (const int *)p;
    case SETTINGS_UINT:
    case SETTINGS_ENUM:
        return (int32_t) * (const unsigned int *)p;
    case SETTINGS_ULONG:
        return (int32_t) * (const unsigned long *)p;
    case SETTINGS_BOOL:
        return *(const bool *)p ? 1 : 0;
    default:
        return 0;
    }
}

void config_field_write(const struct config_field_s *f, config_item_t *item, int32_t value)
{
    void *p = (uint8_t *)item + f->offset;

    switch (f->kind)
    {
    case SETTINGS_INT:
        *(int *)p = (int)value;
        break;
    case SETTINGS_UINT:
    case SETTINGS_ENUM:
        *(unsigned int *)p = (unsigned int)value;
        break;
    case SETTINGS_ULONG:
        *(unsigned long *)p = (unsigned long)value;
        break;
    case SETTINGS_BOOL:
        *(bool *)p = (value != 0);
        break;
    default:
        break;
    }
}

const char *config_field_text(const struct config_field_s *f, const config_item_t *item)
{
    if (f->kind != SETTINGS_TEXT)
        return "";

    return (const char *)((const uint8_t *)item + f->offset);
}

void config_field_value_text(const struct config_field_s *f, const config_item_t *item,
                             char *buffer, size_t size)
{
    switch (f->kind)
    {
    case SETTINGS_TEXT:
        snprintf(buffer, size, "%s", config_field_text(f, item));
        break;

    /* ON and OFF rather than 1 and 0: they are what the panel's rows have
     * always shown, and what an openHAB Switch item takes. */
    case SETTINGS_BOOL:
        snprintf(buffer, size, "%s", config_field_read(f, item) ? "ON" : "OFF");
        break;

    case SETTINGS_ENUM:
    {
        int32_t index = config_field_read(f, item);

        if (index < 0 || index >= (int32_t)f->count)
            index = 0;

        snprintf(buffer, size, "%s", f->names[index]);
        break;
    }

    case SETTINGS_SECTION:
        snprintf(buffer, size, "%s", "");
        break;

    default:
        snprintf(buffer, size, "%ld", (long)config_field_read(f, item));
        break;
    }
}

bool config_field_set_text(const struct config_field_s *f, config_item_t *item, const char *value)
{
    if (f->kind != SETTINGS_TEXT)
        return false;

    if ((f->flags & SETTINGS_F_HOSTCHARS) && strpbrk(value, "/:") != NULL)
        return false;

    strlcpy((char *)((uint8_t *)item + f->offset), value, f->size);
    return true;
}

void config_field_set_number(const struct config_field_s *f, config_item_t *item, long value)
{
    if (value < f->min)
        value = f->min;
    if (value > f->max)
        value = f->max;

    config_field_write(f, item, (int32_t)value);
}

const struct config_field_s *config_field_by_name(const char *name)
{
    if (name == NULL)
        return NULL;

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

void config_fields_set_defaults(config_item_t *item)
{
    for (size_t i = 0; i < config_field_count; i++)
    {
        const struct config_field_s *f = &config_fields[i];

        switch (f->kind)
        {
        case SETTINGS_SECTION:
            break;

        case SETTINGS_TEXT:
            /* Straight in, not through config_field_set_text(): a default is
             * ours and does not need the character check a submitted value
             * gets. */
            strlcpy((char *)((uint8_t *)item + f->offset), f->def_text, f->size);
            break;

        case SETTINGS_ENUM:
            /* By name, like everything else that names a theme. An unknown
             * one resolves to the first option. */
            config_field_write(f, item, config_field_enum_from_name(f, f->def_text));
            break;

        default:
            config_field_write(f, item, f->def_num);
            break;
        }
    }
}

int32_t config_field_enum_from_name(const struct config_field_s *f, const char *name)
{
    if (f->kind != SETTINGS_ENUM || name == NULL)
        return 0;

    for (uint8_t n = 0; n < f->count; n++)
        if (strcasecmp(name, f->names[n]) == 0)
            return (int32_t)n;

    return 0;
}

bool settings_restart_needed(const config_item_t *before, const config_item_t *after,
                             const char **label_out)
{
    for (size_t i = 0; i < config_field_count; i++)
    {
        const struct config_field_s *f = &config_fields[i];

        if (f->kind == SETTINGS_SECTION || (f->flags & SETTINGS_F_RESTART) == 0)
            continue;

        bool changed;

        if (f->kind == SETTINGS_TEXT)
            changed = strcmp(config_field_text(f, before), config_field_text(f, after)) != 0;
        else
            changed = config_field_read(f, before) != config_field_read(f, after);

        if (changed == true)
        {
            if (label_out != NULL)
                *label_out = f->label;

            return true;
        }
    }

    return false;
}
