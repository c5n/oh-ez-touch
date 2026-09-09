#ifndef CONFIG_FIELDS_HPP
#define CONFIG_FIELDS_HPP

/**
 * The device settings, described exactly once.
 *
 * Every setting is one row of config_fields[]: where it lives in
 * Config::item, what C type it is, what range or character set it accepts, and
 * which part of the UI it belongs to. Two very different front ends walk this
 * one table -- the web form in webui.cpp and the touch settings screen in
 * ui_settings.cpp -- so a setting added here appears in both, with the same
 * label and the same validation, or in neither.
 *
 * This started out inside webui.cpp, where the AutoConnect version it replaced
 * had the field list written out three times (GET prefill, POST parse, echo
 * page) and they had drifted apart. The touch screen would have been a fourth
 * copy, hence this header.
 *
 * Nothing here touches LVGL or the network, so it compiles for the host
 * simulator and the unit tests as well as for the device.
 *
 * The SETTINGS_* macros and enumerators below keep that prefix on purpose,
 * where the lowercase names were renamed to config_*. CONFIG_ is Kconfig's
 * namespace: sdkconfig.h puts some three hundred CONFIG_* symbols in scope of
 * every translation unit here, so a CONFIG_TEXT or a CONFIG_TAB_WLAN would read
 * as build configuration rather than as a field kind or a settings tab -- and
 * could one day collide with one.
 */

#include "config.hpp"

#include <stddef.h>
#include <stdint.h>

enum config_field_kind_e
{
    SETTINGS_SECTION = 0, /* heading only, no field                          */
    SETTINGS_TEXT,        /* char[]                                          */
    SETTINGS_INT,         /* int                                             */
    SETTINGS_UINT,        /* unsigned int                                    */
    SETTINGS_ULONG,       /* unsigned long                                   */
    SETTINGS_BOOL,        /* bool, rendered as a checkbox or an ON/OFF row   */
    SETTINGS_ENUM         /* enum, rendered as a select over ->names         */
};

/* Which page of the touch settings screen a section belongs to. A field
 * inherits the tab of the section row above it, so only SEC() rows carry one.
 * The order is the tab order. */
enum settings_tab_e
{
    SETTINGS_TAB_WLAN = 0,
    SETTINGS_TAB_OPENHAB,
    SETTINGS_TAB_MQTT,
    SETTINGS_TAB_SENSORS,
    SETTINGS_TAB_OTHER,
    SETTINGS_TAB_INFO,
    SETTINGS_TAB_COUNT
};

/* Reject '/' and ':' -- this was the ^[^/:]*$ pattern on the AutoConnect
 * inputs, which the browser enforced and the firmware did not, so a
 * hand-written POST could put anything into Config. */
#define SETTINGS_F_HOSTCHARS 0x01u
/* Mark the label: the setting is only read during setup(), so it takes effect
 * after a restart. settings_apply_live() below is what keeps this list short --
 * only what genuinely cannot be re-applied at runtime is flagged. */
#define SETTINGS_F_RESTART 0x02u
/* A secret: never rendered in clear. The web form sends it as an
 * <input type=password> and the settings screen shows the row as asterisks and
 * edits it in a password textarea, the way the WLAN passphrase already is.
 *
 * "Never in clear" is as far as this goes, and it is worth being honest about
 * how far that is. The value is still prefilled into the form, so it is in the
 * page source of an interface that has no authentication at all -- but so is
 * the button that would rewrite it, and anyone who can read the one can use the
 * other. What the flag buys is that a settings page left open on a desk does
 * not display the broker password, and that saving the form does not require
 * retyping it. The MQTT client also refuses to publish a secret field's value
 * to the broker; see mqtt/ohez_mqtt.cpp. */
#define SETTINGS_F_SECRET 0x04u

struct config_field_s
{
    const char        *name;   /* POST argument name; [a-z0-9_] only        */
    const char        *label;
    const char *const *names;  /* SETTINGS_ENUM: the option names           */
    uint16_t           offset; /* byte offset into Config::item             */
    int32_t            min;
    int32_t            max;
    uint8_t            kind;
    uint8_t            size;   /* SETTINGS_TEXT: sizeof the destination     */
    uint8_t            count;  /* SETTINGS_ENUM: number of options          */
    uint8_t            flags;
    uint8_t            tab;    /* SETTINGS_SECTION only (enum settings_tab_e) */
};

/* Config itself is not standard-layout -- it mixes a private member (the name
 * of the file it was loaded from) with the public settings struct -- so
 * offsetof() on it would be ill-formed. Config::item is, and every offset in
 * the table is relative to it. */
typedef decltype(Config::item) config_item_t;

extern const struct config_field_s config_fields[];
extern const size_t                  config_field_count;

/* An upper bound on the table, asserted in config_fields.cpp where its size is
 * a constant expression.
 *
 * This used to be 32 because Arduino's WebServer stopped parsing a body after
 * WEBSERVER_MAX_POST_ARGS arguments and said so only through one log_e(), so a
 * form that outgrew the cap lost fields in silence. That framework is gone:
 * both transports in main/web/ buffer the body themselves and webui_arg()
 * walks it, so there is no argument count limit any more -- only a body length
 * one, WEBUI_BODY_MAX / REQUEST_BODY_MAX, which is 4096 bytes on both.
 *
 * The bound is kept because the *panel* still has one that is not a number in
 * a header: a tab of rows has to stay something a finger can scroll through.
 * 64 rows is roughly four screens per tab, and the settings form posts around
 * 30 bytes per row, so it also stays comfortably inside those 4096. */
#define SETTINGS_MAX_FIELDS 64

/* The tab of the section this row belongs to. Rows before the first section --
 * there are none today -- read as SETTINGS_TAB_OTHER. */
uint8_t config_field_tab(size_t index);

/* The numeric kinds, read and written through one int32_t so that callers do
 * not have to switch on the kind themselves. A SETTINGS_TEXT or
 * SETTINGS_SECTION row reads as 0 and ignores a write. */
int32_t config_field_read(const struct config_field_s *f, const config_item_t *item);
void    config_field_write(const struct config_field_s *f, config_item_t *item, int32_t value);

/* SETTINGS_TEXT only; returns "" for every other kind, never NULL. */
const char *config_field_text(const struct config_field_s *f, const config_item_t *item);

/* The field's current value as text: a SETTINGS_TEXT verbatim, a SETTINGS_BOOL
 * as ON or OFF, a SETTINGS_ENUM as its option name, every numeric kind in
 * decimal, and a SETTINGS_SECTION as "".
 *
 * Here rather than in either caller because there are two: the settings
 * screen's rows and the MQTT client's config/ topics. The screen then puts its
 * own gloss on the result -- "--" for an empty text field, asterisks for a
 * SETTINGS_F_SECRET one -- which is presentation and stays there. */
void config_field_value_text(const struct config_field_s *f, const config_item_t *item,
                             char *buffer, size_t size);

/* Copies at most f->size - 1 characters, so an over-long value is truncated
 * rather than rejected. Returns false, and stores nothing, when the row is
 * SETTINGS_F_HOSTCHARS and the value contains '/' or ':' -- enforced here and
 * not only by the browser's pattern attribute, because a hand-written POST
 * does not run the browser's. */
bool config_field_set_text(const struct config_field_s *f, config_item_t *item, const char *value);

/* Clamps to [f->min, f->max] rather than rejecting. */
void config_field_set_number(const struct config_field_s *f, config_item_t *item, long value);

/* SETTINGS_ENUM: the index of an option name, compared case-insensitively.
 * Unknown names resolve to the first option, the same fallback
 * ui_theme_from_name() applies to the config file. */
int32_t config_field_enum_from_name(const struct config_field_s *f, const char *name);

/* Whether any SETTINGS_F_RESTART field differs between two copies of the
 * settings. On a difference, *label_out is set to that field's label so the
 * caller can name it; pass NULL if it does not care. */
bool settings_restart_needed(const config_item_t *before, const config_item_t *after,
                             const char **label_out);

/* Re-apply everything that can be applied without a reboot: the theme, the
 * openHAB endpoint, the backlight timings and the beeper. Both save paths --
 * the web form and the touch screen -- call this, which is what lets
 * SETTINGS_F_RESTART stay down to the two fields that really need it.
 *
 * Defined in main.cpp, which owns the backlight and beeper globals. Safe to
 * call from the web handler, where lv_timer_handler() is not being pumped:
 * the theme change goes through openhab_ui_request_theme(), which only records
 * a request for openhab_ui_loop() to carry out. */
void settings_apply_live(Config *config);

#endif // CONFIG_FIELDS_HPP
