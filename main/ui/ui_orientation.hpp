#ifndef UI_ORIENTATION_HPP
#define UI_ORIENTATION_HPP

/* Which way up the panel is mounted: the wide 320x240 layout the UI was built
 * for, or an upright 240x320 one.
 *
 * Deliberately free of every dependency, the way ui_theme.hpp is: the
 * orientation is named in the same four places the theme is -- the config
 * file, the web form, the simulator environment, and the style table -- so it
 * gets a header every one of them can include cheaply.
 *
 * It is not part of ui_theme.hpp on purpose: a theme is an identity with a
 * day and a night variant, orientation is a property of the mount, and the
 * two combine freely -- every family draws both ways. */

#include <stddef.h>  /* NULL: the lookup below takes an unset name */
#include <strings.h> /* strcasecmp(): POSIX, present in both newlib and glibc */

#define UI_ORIENTATION_NAME_LANDSCAPE "landscape"
#define UI_ORIENTATION_NAME_PORTRAIT  "portrait"

/* Entry 0 is the landscape every existing config.json means by not naming an
 * orientation at all, and what a zero-initialised Config names. The order must
 * match ui_orientation_names[] and the web dropdown. */
enum ui_orientation_e
{
    UI_ORIENTATION_LANDSCAPE = 0,
    UI_ORIENTATION_PORTRAIT,
    UI_ORIENTATION_COUNT
};

#define UI_ORIENTATION_FALLBACK UI_ORIENTATION_LANDSCAPE

static const char *const ui_orientation_names[UI_ORIENTATION_COUNT] = {
    UI_ORIENTATION_NAME_LANDSCAPE,
    UI_ORIENTATION_NAME_PORTRAIT};

static inline const char *ui_orientation_name(enum ui_orientation_e orientation)
{
    if ((unsigned)orientation >= UI_ORIENTATION_COUNT)
        return ui_orientation_names[UI_ORIENTATION_FALLBACK];

    return ui_orientation_names[orientation];
}

/* Anything unknown resolves to landscape, the same fallback
 * ui_theme_from_name() applies: a typo in a hand-edited config.json selects
 * the layout the panel has always had rather than none. Case-insensitive to
 * match the web form, whose POST carries the option text. */
static inline enum ui_orientation_e ui_orientation_from_name(const char *name)
{
    if (name != NULL)
        for (int i = 0; i < UI_ORIENTATION_COUNT; i++)
            if (strcasecmp(name, ui_orientation_names[i]) == 0)
                return (enum ui_orientation_e)i;

    return UI_ORIENTATION_FALLBACK;
}

#endif
