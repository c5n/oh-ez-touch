#ifndef UI_PIN_HPP
#define UI_PIN_HPP

#include <lvgl.h>

#include "config/pin_store.h"

/* The PIN pad, and the gate in front of whatever a PIN protects.
 *
 * One gate for every scope pin_store knows: the System part of the settings
 * and the sitemap tiles tagged PIN_ITEM_TAG. A caller does not ask whether a
 * PIN is needed; it hands over what it was about to do, and the gate either
 * does it at once -- no PIN set, or the scope already unlocked -- or puts the
 * pad up and does it after a correct entry. A cancel drops it. That is what
 * keeps "is there a PIN" out of every caller, and what lets a new protected
 * thing be one call rather than a dialogue of its own.
 *
 * Each scope unlocks on its own: entering the Item PIN opens tagged tiles and
 * nothing else. An unlock lasts until the panel goes idle -- the backlight
 * dims, or a minute passes without a touch, whichever comes first -- and
 * ui_pin_loop() is where that is noticed.
 *
 * The pad is a full-screen object on lv_layer_top(), the layer the message
 * boxes use, so it covers the page, a pushed item screen or the settings alike
 * and needs no screen of its own on the ui_screen stack. Its keys are themed
 * buttons, so every family draws them its own way.
 */

typedef void (*ui_pin_then_cb)(void *arg);

/* Runs `then(arg)` now if `scope` has no PIN or is unlocked; otherwise shows
 * the pad and runs it after a correct entry. One request at a time: a second
 * guard while the pad is up replaces the first. */
void ui_pin_guard(enum pin_scope_e scope, ui_pin_then_cb then, void *arg);

/* Collect a new PIN for `scope` -- twice, the second time to confirm -- and
 * store it. For the settings screen, which only gets here behind the System
 * PIN, so the old PIN is not asked for again.
 *
 * The pad deals with the outcomes the person in front of it can fix: two
 * entries that differ, or a PIN that is the other scope's, are said on the pad
 * and the entry starts over. `done` hears the rest -- PIN_SET_OK, or
 * PIN_SET_IO when NVS refused the write -- after the pad has gone. A cancel
 * calls nothing. A stored PIN leaves its scope unlocked: whoever set it has
 * just shown they know it. */
typedef void (*ui_pin_changed_cb)(enum pin_set_result_e result, void *arg);

void ui_pin_change(enum pin_scope_e scope, ui_pin_changed_cb done, void *arg);

bool ui_pin_is_unlocked(enum pin_scope_e scope);

/* Clear a scope's PIN on behalf of another task -- the web interface's,
 * which must not touch the pad or race the LVGL task's reads of the store.
 * Recorded here and carried out by the next ui_pin_loop(), a few
 * milliseconds later; the pad, if it is up for that scope, goes with it. */
void ui_pin_request_clear(enum pin_scope_e scope);
void ui_pin_lock_all(void);

/* Re-locks on idle and keeps the lockout countdown on the pad current. Called
 * from the main loop, unconditionally. */
void ui_pin_loop(void);

bool             ui_pin_is_open(void);
enum pin_scope_e ui_pin_open_scope(void); /* meaningful while ui_pin_is_open() */

/* Where a key of the open pad is, in panel pixels: '0' to '9', 'b' for
 * backspace, 'k' for OK and 'x' for cancel. For the control interface, so a
 * script taps what was drawn rather than coordinates copied from a layout. */
bool ui_pin_key_area(char key, lv_area_t *out);

#endif /* UI_PIN_HPP */
