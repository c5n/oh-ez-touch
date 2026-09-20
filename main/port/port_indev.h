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

#include "lvgl.h"

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

#ifdef __cplusplus
}
#endif

#endif /* PORT_INDEV_H */
