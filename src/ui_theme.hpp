#ifndef UI_THEME_HPP
#define UI_THEME_HPP

/* The identity of a UI theme: which look, and whether its night variant is in
 * effect. Deliberately free of every dependency -- no lvgl, no Arduino.
 *
 * The theme has to be named in four places that have nothing else in common:
 * the config file, the web form, the simulator environment, and the style table
 * in ui_style.cpp. Putting the enum in ui_style.hpp would drag <lvgl.h> into
 * config.hpp, and from there into ac_main.cpp, the sensor translation units and
 * the host tests. Hence this header, which all four can include cheaply. */

#include <strings.h> /* strcasecmp(): POSIX, present in both newlib and glibc */

#define UI_THEME_NAME_DEFAULT "Default"
#define UI_THEME_NAME_LCARS   "LCARS"
#define UI_THEME_NAME_JARVIS  "JARVIS"

#define UI_NIGHT_NAME_OFF  "off"
#define UI_NIGHT_NAME_ON   "on"
#define UI_NIGHT_NAME_AUTO "auto"

/* UI_THEME_DEFAULT is deliberately 0: Config is a global, and a loadConfig()
 * that bails out early leaves item zero-initialised -- that still has to name a
 * valid theme. The order must match ui_theme_names[] and the web dropdown. */
enum ui_theme_family_e
{
    UI_THEME_DEFAULT = 0,
    UI_THEME_LCARS,
    UI_THEME_JARVIS,
    UI_THEME_FAMILY_COUNT
};

enum ui_night_mode_e
{
    UI_NIGHT_OFF = 0,
    UI_NIGHT_ON,
    UI_NIGHT_AUTO,
    UI_NIGHT_MODE_COUNT
};

static const char *const ui_theme_names[UI_THEME_FAMILY_COUNT] = {
    UI_THEME_NAME_DEFAULT,
    UI_THEME_NAME_LCARS,
    UI_THEME_NAME_JARVIS};

static const char *const ui_night_mode_names[UI_NIGHT_MODE_COUNT] = {
    UI_NIGHT_NAME_OFF,
    UI_NIGHT_NAME_ON,
    UI_NIGHT_NAME_AUTO};

static inline const char *ui_theme_name(enum ui_theme_family_e family)
{
    if ((unsigned)family >= UI_THEME_FAMILY_COUNT)
        return ui_theme_names[UI_THEME_DEFAULT];

    return ui_theme_names[family];
}

static inline const char *ui_night_mode_name(enum ui_night_mode_e mode)
{
    if ((unsigned)mode >= UI_NIGHT_MODE_COUNT)
        return ui_night_mode_names[UI_NIGHT_OFF];

    return ui_night_mode_names[mode];
}

/* Anything unknown -- a typo in a hand-edited config.json, a name written by a
 * newer firmware, or the NULL that getenv() returns for an unset variable --
 * resolves to the first entry rather than to nothing. The comparison is
 * case-insensitive because AutoConnect's own select() matches its option text
 * with equalsIgnoreCase(), and the two halves have to agree. */
static inline enum ui_theme_family_e ui_theme_from_name(const char *name)
{
    if (name != NULL)
        for (int i = 0; i < UI_THEME_FAMILY_COUNT; i++)
            if (strcasecmp(name, ui_theme_names[i]) == 0)
                return (enum ui_theme_family_e)i;

    return UI_THEME_DEFAULT;
}

static inline enum ui_night_mode_e ui_night_mode_from_name(const char *name)
{
    if (name != NULL)
        for (int i = 0; i < UI_NIGHT_MODE_COUNT; i++)
            if (strcasecmp(name, ui_night_mode_names[i]) == 0)
                return (enum ui_night_mode_e)i;

    return UI_NIGHT_OFF;
}

#endif
