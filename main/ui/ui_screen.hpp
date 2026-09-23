#ifndef UI_SCREEN_HPP
#define UI_SCREEN_HPP

#include <lvgl.h>

/* Where the UI is, and how it gets somewhere else.
 *
 * The panel has one openHAB page and exactly two things that can cover it: an
 * item control screen and the settings screen. This is therefore a root plus at
 * most one pushed screen, not a general stack -- encoding what is actually true
 * removes a class of bug, and it matches what the code already did by hand (the
 * single `open_window` slot in openhab_ui.cpp, and ui_settings_open() treating a
 * second open as a request for another tab).
 *
 * Three mechanisms used to do this job and disagreed about all of it: an instant
 * lv_screen_load() for the settings screen, a create-and-delete_async overlay
 * for item windows, and a silent in-place rebuild for sitemap pages. They now
 * share one, so "deeper" and "back" mean the same thing everywhere.
 *
 * On animation, and why there is so little of it here: lv_screen_load_anim()
 * moves the screen object, so every frame invalidates the full 320x240 twice and
 * LVGL joins that into one whole-screen repaint. At 40 MHz that is 30.7 ms of
 * SPI per frame -- a 200 ms slide is six frames. Whole-screen motion is
 * therefore not affordable on any path the user walks often, and the fluid feel
 * comes from staggering the *contents* of a screen instead (see ui_motion.hpp).
 * The settings screen is the one exception: it is rare, and the stepping reads
 * as "you have gone somewhere else" rather than as jank. */

enum ui_screen_id_e
{
    UI_SCREEN_NONE = 0, /* only the root is up */
    UI_SCREEN_ITEM,
    UI_SCREEN_SETTINGS,
    /* The screensaver: pushed over everything when the backlight dims with
     * the clock setting on, popped by the waking tap. */
    UI_SCREEN_CLOCK
};

/* Create the root screen and make it active. Call once, before any widget --
 * ui_style_init() styles whatever lv_screen_active() returns. */
void ui_screen_setup(void);

lv_obj_t *ui_screen_root(void);

/* A bare screen carrying the theme's background. Callers fill it and push it. */
lv_obj_t *ui_screen_create(void);

/* Show `screen`, remembering it as `id`. The root stays alive underneath.
 * `anim_ms` of 0 loads instantly. */
void ui_screen_push(lv_obj_t *screen, enum ui_screen_id_e id, uint32_t anim_ms);

/* Back to the root. The pushed screen is deleted once it is off the display. */
void ui_screen_pop(uint32_t anim_ms);

/* Force any transition in flight to its end state. A screen-load animation owns
 * two screens at once, so anything about to delete a screen -- a theme change,
 * most of all -- has to call this first. */
void ui_screen_settle(void);

enum ui_screen_id_e ui_screen_top(void);

/* Re-apply the banner policy. main.cpp can raise a WLAN banner on the top layer
 * at any moment, including while a screen is pushed over the page it belongs
 * to, so this is repeated from the loop rather than only at push time. */
void ui_screen_loop(void);

#endif /* UI_SCREEN_HPP */
