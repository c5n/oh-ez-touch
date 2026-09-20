#ifndef UI_THEME_HPP
#define UI_THEME_HPP

/* The identity of a UI theme: which look, and whether its night variant is in
 * effect. Deliberately free of every dependency -- no lvgl, no Arduino.
 *
 * The theme has to be named in four places that have nothing else in common:
 * the config file, the web form, the simulator environment, and the style table
 * in ui_style.cpp. Putting the enum in ui_style.hpp would drag <lvgl.h> into
 * config.hpp, and from there into webui.cpp, the sensor translation units and
 * the host tests. Hence this header, which all four can include cheaply. */

#include <stddef.h>  /* NULL: the lookups below take an unset name */
#include <strings.h> /* strcasecmp(): POSIX, present in both newlib and glibc */

#define UI_THEME_NAME_MATERIAL "Material"
#define UI_THEME_NAME_LCARS    "LCARS"
#define UI_THEME_NAME_JARVIS   "JARVIS"
#define UI_THEME_NAME_CLASSIC  "Classic"

#define UI_NIGHT_NAME_OFF  "off"
#define UI_NIGHT_NAME_ON   "on"
#define UI_NIGHT_NAME_AUTO "auto"

/* Entry 0 is deliberately a real theme rather than a "none": Config is a
 * global, and a loadConfig() that bails out early leaves item zero-initialised
 * -- that still has to name something drawable. The order must match
 * ui_theme_names[] and the web dropdown. */
enum ui_theme_family_e
{
    UI_THEME_MATERIAL = 0,
    UI_THEME_LCARS,
    UI_THEME_JARVIS,
    /* Appended rather than put where it belongs chronologically, and that is
     * the whole reason it is last: entry 0 is what an unknown name resolves to
     * and what a zero-initialised Config names, so moving anything already in
     * this list would change what an existing config.json means. */
    UI_THEME_CLASSIC,
    UI_THEME_FAMILY_COUNT
};

/* Spelled separately from UI_THEME_MATERIAL wherever what is meant is "the one
 * an unknown name lands on" rather than "this particular look". They are the
 * same family and were the same word until it was renamed; keeping the two
 * meanings apart is what stops a future reordering from quietly making the
 * fallback whichever theme happens to be first. */
#define UI_THEME_FALLBACK UI_THEME_MATERIAL

enum ui_night_mode_e
{
    UI_NIGHT_OFF = 0,
    UI_NIGHT_ON,
    UI_NIGHT_AUTO,
    UI_NIGHT_MODE_COUNT
};

static const char *const ui_theme_names[UI_THEME_FAMILY_COUNT] = {
    UI_THEME_NAME_MATERIAL,
    UI_THEME_NAME_LCARS,
    UI_THEME_NAME_JARVIS,
    UI_THEME_NAME_CLASSIC};

static const char *const ui_night_mode_names[UI_NIGHT_MODE_COUNT] = {
    UI_NIGHT_NAME_OFF,
    UI_NIGHT_NAME_ON,
    UI_NIGHT_NAME_AUTO};

static inline const char *ui_theme_name(enum ui_theme_family_e family)
{
    if ((unsigned)family >= UI_THEME_FAMILY_COUNT)
        return ui_theme_names[UI_THEME_FALLBACK];

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
 * case-insensitive to match the web form, whose POST carries the option text
 * and looks it up the same way -- the two halves have to agree. */
static inline enum ui_theme_family_e ui_theme_from_name(const char *name)
{
    if (name != NULL)
        for (int i = 0; i < UI_THEME_FAMILY_COUNT; i++)
            if (strcasecmp(name, ui_theme_names[i]) == 0)
                return (enum ui_theme_family_e)i;

    return UI_THEME_FALLBACK;
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
