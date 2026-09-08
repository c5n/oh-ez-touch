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

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Register the pointer with LVGL, bound to `disp`.
 *
 * Same task rule as port_display_init(), and call it after that.
 */
void port_indev_init(lv_display_t *disp);

#ifdef __cplusplus
}
#endif

#endif /* PORT_INDEV_H */
