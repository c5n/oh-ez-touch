/**
 * @file port_indev.h
 *
 * The pointer input device.
 *
 * Device: the resistive or capacitive touch panel, through esp_lcd_touch.
 * Host:   the mouse, which LVGL's SDL driver already provides.
 *
 * One pointer, no keypad and no encoder: that is all the UI has ever read.
 */
#ifndef PORT_INDEV_H
#define PORT_INDEV_H

#include <stdbool.h>
#include <stddef.h>

#include "lvgl.h"

#include "touch_cal.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Register the pointer with LVGL, bound to `disp`.
 *
 * Same task rule as port_display_init(), and call it after that.
 *
 * @param portrait  which way up the display was created, so the touch
 *   coordinates land on the same orientation as the picture. The simulator
 *   ignores it: SDL's mouse already reports window coordinates.
 */
void port_indev_init(lv_display_t *disp, bool portrait);

/**
 * Implemented by the application; called from the pointer read on every
 * sampled press, before the position reaches LVGL.
 *
 * @return true to swallow this touch and the next fraction of a second of
 *   them. The tap that wakes a dimmed display must not also operate whatever
 *   widget happens to be under the finger, and the same tap is what the touch
 *   blip sounds for -- both of which are application policy about hardware the
 *   port owns, which is why it is a call outwards rather than a setting.
 *
 * Called only where there is a backlight to wake. The simulator's pointer is
 * LVGL's own SDL mouse, whose read callback is not ours to filter, and there
 * is nothing there to dim -- so this is not called on the host, and that is
 * the difference itself rather than a stub standing in for one.
 */
bool ohez_touch_wake(void);

/**
 * Whether this panel has a calibration to set at all.
 *
 * True for the resistive XPT2046 boards, which report a raw ADC value per axis.
 * False for the capacitive FT6X36, which reports the panel's own pixel grid --
 * there is nothing to calibrate there, and the settings screen says so rather
 * than offering a procedure that could only make it worse.
 */
bool port_indev_calibratable(void);

/** The board's built-in constants, from board_pins.h. Never fails. */
void port_indev_cal_defaults(struct touch_cal_s *cal);

/** What the pointer is converting with at this moment. */
void port_indev_cal_get(struct touch_cal_s *cal);

/**
 * Convert with these from the next read onwards.
 *
 * Live, not boot-only. The orientation carries SETTINGS_F_RESTART because the
 * panel's MADCTL is set once while the display is brought up; the calibration
 * is only arithmetic in the read path, so a change takes effect at once and
 * the user can see whether it worked.
 *
 * An invalid calibration -- either span zero -- is ignored, because the pointer
 * is what the user would need in order to undo it.
 */
void port_indev_cal_set(const struct touch_cal_s *cal);

/**
 * The raw reading behind the most recent confirmed press.
 *
 * This is what the calibration screen records at each target: the position
 * LVGL reports has already been through the calibration being replaced, and on
 * a panel bad enough to need calibrating it is nowhere near the finger.
 *
 * "Confirmed" is the two-poll press of read_cb(), not any reading that arrived
 * -- a calibration solved from the noise that confirmation exists to discard
 * would be worse than the one it replaced.
 *
 * The simulator has no panel and models one; see linux/port_indev.c for what it
 * reports and why that is the honest answer there rather than a stub.
 *
 * @return false if no press has been seen yet, or if the panel has no raw
 *   reading to give.
 */
bool port_indev_raw_press(int32_t *raw_x, int32_t *raw_y);

/**
 * Convert a raw pair with `cal`, whatever `cal` is.
 *
 * The calibration screen's way of asking "where would the calibration I am
 * about to replace have put this tap?", which is the whole of what it draws.
 *
 * This and port_indev_cal_solve() are wrappers rather than direct calls to
 * touch_cal.h because the two arguments they add -- which way up the display
 * was created, and whether this board is mounted upside down -- are the port's
 * to know. Leaking them upwards would put a board fact in the UI, and the UI
 * would have to be right about it on every board.
 *
 * Not clamped to the screen: the distance a bad calibration puts a tap
 * *outside* the panel is a real part of the error being corrected.
 */
void port_indev_cal_map(const struct touch_cal_s *cal, int32_t raw_x, int32_t raw_y,
                        int32_t *screen_x, int32_t *screen_y);

/**
 * Solve a calibration from corner taps, against this panel's geometry.
 *
 * Returns NULL on success, or a short reason fit to show on the panel.
 */
const char *port_indev_cal_solve(const struct touch_cal_sample_s *samples, size_t count,
                                 struct touch_cal_s *out);

#ifdef __cplusplus
}
#endif

#endif /* PORT_INDEV_H */
