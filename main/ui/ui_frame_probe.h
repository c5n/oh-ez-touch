/**
 * @file ui_frame_probe.h
 *
 * Where the numbers in ui_frame_stats.h come from.
 *
 * LVGL already measures the frame; it just does not keep the result. Every
 * boundary this needs is an event the display sends by itself, so nothing here
 * touches the port layer and the simulator is instrumented by the same code as
 * the panel.
 */
#ifndef UI_FRAME_PROBE_H
#define UI_FRAME_PROBE_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Start measuring one display. Call once, right after it is created.
 *
 * Registers seven event callbacks and keeps no reference to the display beyond
 * them, so a display that is deleted takes its callbacks with it. Measuring a
 * second display is not supported and is not a limitation anything here has:
 * both targets create exactly one.
 */
void ui_frame_probe_attach(lv_display_t *disp);

#ifdef __cplusplus
}
#endif

#endif /* UI_FRAME_PROBE_H */
