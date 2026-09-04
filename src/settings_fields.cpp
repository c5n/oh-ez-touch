#include "settings_fields.hpp"

#include "ui_theme.hpp"

#include <stdlib.h>
#include <string.h>
#include <strings.h>

/* The target has to be an offset rather than a pointer or a lambda: a table of
 * pointers into a global would need dynamic initialisation and would land in
 * RAM, where this one stays in flash. Same reasoning as the uint32_t colours in
 * ui_style.hpp. */
#define OFF(path) ((uint16_t)offsetof(settings_item_t, path))
#define SZ(path) ((uint8_t)sizeof(((settings_item_t *)0)->path))

/* Shorthand, so that a row fits on one line and the table can be read against
 * the settings tables in README.md. Only SEC() names a tab; the rows under it
 * inherit it. */
#define SEC(lbl, tb)                {NULL, (lbl), NULL, 0,          0,     0, SETTINGS_SECTION, 0,        0,   0,     (tb)}
#define TXT(nm, lbl, path, fl)      {(nm), (lbl), NULL, OFF(path),  0,     0, SETTINGS_TEXT,    SZ(path), 0,   (fl),  0}
#define SINT(nm, lbl, path, lo, hi) {(nm), (lbl), NULL, OFF(path), (lo), (hi), SETTINGS_INT,    0,        0,   0,     0}
#define UINT(nm, lbl, path, lo, hi) {(nm), (lbl), NULL, OFF(path), (lo), (hi), SETTINGS_UINT,   0,        0,   0,     0}
#define ULNG(nm, lbl, path, lo, hi) {(nm), (lbl), NULL, OFF(path), (lo), (hi), SETTINGS_ULONG,  0,        0,   0,     0}
#define CHK(nm, lbl, path, fl)      {(nm), (lbl), NULL, OFF(path),  0,     0, SETTINGS_BOOL,    0,        0,   (fl),  0}
#define SEL(nm, lbl, path, tbl, n)  {(nm), (lbl), (tbl), OFF(path), 0, (n) - 1, SETTINGS_ENUM,  0,      (n),   0,     0}

/* The SETTINGS_F_RESTART flags say what the code actually does, which is not
 * what they used to say. settings_apply_live() re-applies the openHAB endpoint,
 * the backlight timings, the beeper and the theme; the NTP host, offset and DST
 * are live already, because openhab_ui_loop() re-issues configTime() off the
 * live Config every NTP_TIME_UPDATE_INTERVAL. That leaves two:
 *
 *   hostname -- WiFi.setHostname() runs before WiFi.mode() in wlan_setup(),
 *               and the name doubles as the setup access point's SSID.
 *   bme_use  -- honoured only in openhab_sensor_main_setup().
 *
 * Getting this right matters more than it used to: the touch screen offers a
 * restart when a flagged field changes, and a flag on a field that is in fact
 * live would ask for a reboot on nearly every save. */
const struct settings_field_s settings_fields[] = {

    SEC("General", SETTINGS_TAB_OTHER),
    TXT("hostname", "Hostname", general.hostname, SETTINGS_F_HOSTCHARS | SETTINGS_F_RESTART),

    SEC("NTP Time", SETTINGS_TAB_OTHER),
    TXT("ntp_host", "Host", ntp.hostname, SETTINGS_F_HOSTCHARS),
    SINT("ntp_gmt", "GMT offset [h]", ntp.gmt_offset, -12, 14),
    CHK("ntp_dst", "Daylight saving (+1h)", ntp.daylightsaving, 0),

    SEC("Appearance", SETTINGS_TAB_OTHER),
    /* The option names come straight from ui_theme.hpp, so the dropdown, the
     * config file and the simulator's environment variables cannot drift
     * apart. Unlike the AutoConnect version this needs no 1-based index
     * arithmetic: the POST carries the name, and the lookup owns the
     * fallback. */
    SEL("theme", "Theme", ui.theme, ui_theme_names, UI_THEME_FAMILY_COUNT),
    SEL("night_mode", "Night mode", ui.night_mode, ui_night_mode_names, UI_NIGHT_MODE_COUNT),
    UINT("night_from", "Night from [h]", ui.night_from, 0, 23),
    UINT("night_to", "Night to [h]", ui.night_to, 0, 23),

    SEC("LCD Backlight Dimming", SETTINGS_TAB_OTHER),
    ULNG("bl_timeout", "Activity timeout [s] (0=off)", backlight.activity_timeout, 0, 86400),
    UINT("bl_normal", "Normal brightness [%]", backlight.normal_brightness, 0, 100),
    UINT("bl_dim", "Dim brightness [%]", backlight.dim_brightness, 0, 100),

    SEC("Beeper", SETTINGS_TAB_OTHER),
    CHK("beeper", "Enable beeper", beeper.enabled, 0),

    SEC("OpenHAB Server", SETTINGS_TAB_OPENHAB),
    TXT("oh_host", "Host", openhab.hostname, SETTINGS_F_HOSTCHARS),
    SINT("oh_port", "Port", openhab.port, 1, 65535),
    TXT("oh_sitemap", "Sitemap", openhab.sitemap, SETTINGS_F_HOSTCHARS),

    SEC("Sensors", SETTINGS_TAB_SENSORS),
    CHK("bme_use", "Use BME280 sensor", openhab.sensors.bme280.use, SETTINGS_F_RESTART),
    SINT("bme_interval", "Update interval [s]", openhab.sensors.bme280.interval, 1, 86400),
    TXT("bme_temp", "Temperature item", openhab.sensors.bme280.items.temperature, 0),
    TXT("bme_hum", "Humidity item", openhab.sensors.bme280.items.humidity, 0),
    TXT("bme_press", "Pressure item", openhab.sensors.bme280.items.pressure, 0),
};

const size_t settings_field_count = sizeof(settings_fields) / sizeof(settings_fields[0]);

/* The generic accessors below reach into Config by offset, so a field whose C
 * type stops matching its kind would corrupt its neighbours rather than fail
 * to compile. These are the checks that keep the table honest. */
static_assert(sizeof(int) == sizeof(int32_t), "SETTINGS_INT width");
static_assert(sizeof(enum ui_theme_family_e) == sizeof(unsigned int), "SETTINGS_ENUM width");
static_assert(sizeof(enum ui_night_mode_e) == sizeof(unsigned int), "SETTINGS_ENUM width");

static_assert(sizeof(settings_fields) / sizeof(settings_fields[0]) <= SETTINGS_MAX_POST_ARGS,
              "the web settings form would exceed WEBSERVER_MAX_POST_ARGS");

uint8_t settings_field_tab(size_t index)
{
    /* Walk back to the nearest section rather than storing the tab on every
     * row: a section and its fields cannot then disagree. */
    for (size_t i = index + 1; i > 0; i--)
        if (settings_fields[i - 1].kind == SETTINGS_SECTION)
            return settings_fields[i - 1].tab;

    return SETTINGS_TAB_OTHER;
}

int32_t settings_field_read(const struct settings_field_s *f, const settings_item_t *item)
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

void settings_field_write(const struct settings_field_s *f, settings_item_t *item, int32_t value)
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

const char *settings_field_text(const struct settings_field_s *f, const settings_item_t *item)
{
    if (f->kind != SETTINGS_TEXT)
        return "";

    return (const char *)((const uint8_t *)item + f->offset);
}

bool settings_field_set_text(const struct settings_field_s *f, settings_item_t *item, const char *value)
{
    if (f->kind != SETTINGS_TEXT)
        return false;

    if ((f->flags & SETTINGS_F_HOSTCHARS) && strpbrk(value, "/:") != NULL)
        return false;

    strlcpy((char *)((uint8_t *)item + f->offset), value, f->size);
    return true;
}

void settings_field_set_number(const struct settings_field_s *f, settings_item_t *item, long value)
{
    if (value < f->min)
        value = f->min;
    if (value > f->max)
        value = f->max;

    settings_field_write(f, item, (int32_t)value);
}

int32_t settings_field_enum_from_name(const struct settings_field_s *f, const char *name)
{
    if (f->kind != SETTINGS_ENUM || name == NULL)
        return 0;

    for (uint8_t n = 0; n < f->count; n++)
        if (strcasecmp(name, f->names[n]) == 0)
            return (int32_t)n;

    return 0;
}

bool settings_restart_needed(const settings_item_t *before, const settings_item_t *after,
                             const char **label_out)
{
    for (size_t i = 0; i < settings_field_count; i++)
    {
        const struct settings_field_s *f = &settings_fields[i];

        if (f->kind == SETTINGS_SECTION || (f->flags & SETTINGS_F_RESTART) == 0)
            continue;

        bool changed;

        if (f->kind == SETTINGS_TEXT)
            changed = strcmp(settings_field_text(f, before), settings_field_text(f, after)) != 0;
        else
            changed = settings_field_read(f, before) != settings_field_read(f, after);

        if (changed == true)
        {
            if (label_out != NULL)
                *label_out = f->label;

            return true;
        }
    }

    return false;
}
