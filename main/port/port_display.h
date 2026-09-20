/**
 * @file port_display.h
 *
 * The screen, as an LVGL display.
 *
 * Device: an esp_lcd panel on SPI, flushed from a DMA-capable draw buffer.
 * Host:   an SDL window, which LVGL's own driver already provides.
 *
 * The geometry is shared because it is shared in fact: all four supported
 * boards are 320x240 in landscape, and the simulator has to be the same size or
 * it stops being a simulator -- the UI is laid out in absolute pixels against
 * these numbers.
 *
 * PORT_DISPLAY_WIDTH/HEIGHT name the landscape axes. A panel mounted upright
 * swaps them: port_display_init(true) creates a 240x320 display, the panel's
 * MADCTL is told to stop swapping the axes, and the UI lays out against the
 * swapped resolutions LVGL then reports.
 */
#ifndef PORT_DISPLAY_H
#define PORT_DISPLAY_H

#include "lvgl.h"

#include <stdbool.h>

#define PORT_DISPLAY_WIDTH  320
#define PORT_DISPLAY_HEIGHT 240

/* The horizontal resolution, whichever way up the panel is mounted. The
 * vertical one is the other of the two. */
#define PORT_DISPLAY_HOR_RES(portrait) ((portrait) ? PORT_DISPLAY_HEIGHT : PORT_DISPLAY_WIDTH)

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Bring the display up and hand it to LVGL.
 *
 * Must be called from the task that will call lv_timer_handler(), and never
 * before lv_init(). That is not an arbitrary tidiness rule: LVGL's SDL driver
 * pumps SDL's event queue from an lv_timer rather than a thread of its own
 * (lv_sdl_window.c registers sdl_event_handler at 5 ms), so SDL_Init(),
 * SDL_PollEvent() and SDL_RenderPresent() all end up running in whichever task
 * drives LVGL. Splitting them across tasks is what would break.
 *
 * @param portrait  false for the landscape 320x240 every panel has always had,
 *                  true for an upright 240x320 mount.
 * @return the display, never NULL -- a failure here aborts.
 */
lv_display_t *port_display_init(bool portrait);

#ifdef __cplusplus
}
#endif

#endif /* PORT_DISPLAY_H */
