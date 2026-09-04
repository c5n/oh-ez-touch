#ifndef SETTINGS_FIELDS_HPP
#define SETTINGS_FIELDS_HPP

/**
 * The device settings, described exactly once.
 *
 * Every setting is one row of settings_fields[]: where it lives in
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
 */

#include "config.hpp"

#include <stddef.h>
#include <stdint.h>

enum settings_kind_e
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

struct settings_field_s
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

/* Config itself is not standard-layout -- it mixes a private String with the
 * public settings struct -- so offsetof() on it would be ill-formed.
 * Config::item is, and every offset in the table is relative to it. */
typedef decltype(Config::item) settings_item_t;

extern const struct settings_field_s settings_fields[];
extern const size_t                  settings_field_count;

/* Arduino WebServer stops parsing a body after WEBSERVER_MAX_POST_ARGS
 * arguments and says so only through one log_e(), so a form that outgrows the
 * cap loses fields in silence. The cap lives in the framework's Parsing.cpp
 * rather than in a header, so it cannot be asserted against directly; this
 * mirrors the framework default, and should the framework ever raise it, this
 * stays wrong in the harmless direction. Asserted in settings_fields.cpp,
 * where the table's size is a constant expression.
 *
 * The web form posts one argument per non-section row. The WLAN credentials
 * are a form of its own partly for this reason. */
#define SETTINGS_MAX_POST_ARGS 32

/* The tab of the section this row belongs to. Rows before the first section --
 * there are none today -- read as SETTINGS_TAB_OTHER. */
uint8_t settings_field_tab(size_t index);

/* The numeric kinds, read and written through one int32_t so that callers do
 * not have to switch on the kind themselves. A SETTINGS_TEXT or
 * SETTINGS_SECTION row reads as 0 and ignores a write. */
int32_t settings_field_read(const struct settings_field_s *f, const settings_item_t *item);
void    settings_field_write(const struct settings_field_s *f, settings_item_t *item, int32_t value);

/* SETTINGS_TEXT only; returns "" for every other kind, never NULL. */
const char *settings_field_text(const struct settings_field_s *f, const settings_item_t *item);

/* Copies at most f->size - 1 characters, so an over-long value is truncated
 * rather than rejected. Returns false, and stores nothing, when the row is
 * SETTINGS_F_HOSTCHARS and the value contains '/' or ':' -- enforced here and
 * not only by the browser's pattern attribute, because a hand-written POST
 * does not run the browser's. */
bool settings_field_set_text(const struct settings_field_s *f, settings_item_t *item, const char *value);

/* Clamps to [f->min, f->max] rather than rejecting. */
void settings_field_set_number(const struct settings_field_s *f, settings_item_t *item, long value);

/* SETTINGS_ENUM: the index of an option name, compared case-insensitively.
 * Unknown names resolve to the first option, the same fallback
 * ui_theme_from_name() applies to the config file. */
int32_t settings_field_enum_from_name(const struct settings_field_s *f, const char *name);

/* Whether any SETTINGS_F_RESTART field differs between two copies of the
 * settings. On a difference, *label_out is set to that field's label so the
 * caller can name it; pass NULL if it does not care. */
bool settings_restart_needed(const settings_item_t *before, const settings_item_t *after,
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

#endif // SETTINGS_FIELDS_HPP
