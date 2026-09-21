#ifndef UI_CALIBRATION_HPP
#define UI_CALIBRATION_HPP

#include <stdbool.h>
#include <stdint.h>

#include "port/touch_cal.h"

/* The touchscreen calibration, in the settings screen's overlay slot.
 *
 * Four corner targets, then a result the user accepts or throws away. The
 * interesting decision is that a target tap is not required to land anywhere
 * near its cross: the calibration being replaced is still in force while this
 * runs, so on the panel that most needs this the reported position is nowhere
 * near the finger. Only the raw reading and the known position of the cross are
 * used, which is what lets the procedure rescue a panel rather than only tune
 * an already-working one.
 *
 * It is an overlay and not a pushed screen because ui_screen allows exactly one
 * screen over the root and the settings screen is already it.
 */

/* Start at the first target. Does nothing if the settings screen is not open,
 * or if this panel has no calibration (see port_indev_calibratable()). */
void ui_calibration_open(void);

bool ui_calibration_is_open(void);

/* Where the crosses are, as x,y pairs in panel pixels, for the control
 * interface. A script that taps these rather than four numbers of its own
 * cannot be made to pass by a layout change that moved them -- the same
 * reasoning as the `screen` dump's rectangles. Returns the number of pairs
 * written, 0 when no target is showing. */
unsigned ui_calibration_targets(int32_t *pairs, unsigned max_pairs);

/* How many of the crosses have been pressed, and how many there are. A script
 * taps and then waits for this to move, rather than for a length of time: a
 * press that arrives before the overlay is on screen is ignored by design, and
 * a run that only sleeps between taps turns that into a silent short set. */
void ui_calibration_progress(unsigned *taken, unsigned *total);

/* The result, once the four taps are in: what the panel was calibrated with,
 * what it would be, and the two figures the screen puts in words -- the worst
 * error the old calibration had at the four measured points, and the worst the
 * new one still has. False while the flow is still collecting taps, and when
 * the solve was refused. */
bool ui_calibration_result(struct touch_cal_s *before, struct touch_cal_s *after,
                           int32_t *worst_px, int32_t *residual_px);

#endif /* UI_CALIBRATION_HPP */
