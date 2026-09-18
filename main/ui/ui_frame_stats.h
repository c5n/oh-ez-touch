/**
 * @file ui_frame_stats.h
 *
 * What a frame cost, averaged over a window.
 *
 * Deliberately free of LVGL: this file and ui_frame_stats.c include nothing but
 * libc, which is what lets test/host reach them. The half that knows about
 * LVGL -- the event callbacks that produce these numbers -- is ui_frame_probe.c,
 * and it is not testable there because a frame needs a display.
 */
#ifndef UI_FRAME_STATS_H
#define UI_FRAME_STATS_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* A window closes on whichever of these comes first, so that the numbers stay
 * fresh while the screen is moving and do not need a frame to be reported while
 * it is still. 64 frames is a second of animation at the 16 ms refresh period;
 * the cap is what closes a window that a few stray redraws started. */
#define UI_FRAME_STATS_WINDOW_FRAMES    64
#define UI_FRAME_STATS_WINDOW_MS        10000

/**
 * One closed window.
 *
 * `render_us` and `wait_us` are the two halves the whole question is about:
 * time the CPU spent in LVGL's software renderer, and time it spent waiting for
 * the panel to take the last strip. If the second dominates, the screen is
 * SPI-bound and no amount of CPU will help it; if the first does, the CPU-side
 * levers in the README are worth taking.
 */
typedef struct
{
    bool     valid;         /**< false until a window with at least one frame has closed */
    uint32_t age_ms;        /**< how long ago that window closed -- 0 while drawing */
    uint32_t window_ms;     /**< how long it covered */
    uint32_t frames;        /**< frames that actually drew something */
    uint32_t fps_x10;       /**< frames per second over the window, in tenths */
    uint32_t frame_us;      /**< mean refresh, start to ready */
    uint32_t frame_us_max;  /**< the worst single one */
    uint32_t render_us;     /**< of which, mean: in the software renderer */
    uint32_t wait_us;       /**< of which, mean: waiting for the panel */
    uint32_t pixels;        /**< mean pixels handed to the flush callback */
    uint32_t flushes_x10;   /**< mean flush calls -- strips -- per frame, in tenths */
} ui_frame_stats_t;

/** Start a window at `now_ms` and throw away whatever was collected. */
void ui_frame_stats_reset(uint32_t now_ms);

/**
 * Close the window if it is due, without contributing a frame.
 *
 * Called once per refresh cycle, including the cycles where nothing was
 * redrawn, so that an idle panel still advances the clock the rate is measured
 * against. A window that times out with no frames in it is discarded rather
 * than published: a still screen has no frame rate, and reporting 0.0 fps for
 * one would overwrite the last real measurement with an artefact of nobody
 * touching the panel.
 */
void ui_frame_stats_tick(uint32_t now_ms);

/**
 * Contribute one finished frame.
 *
 * All four durations are microseconds, `pixels` counts what the flush callback
 * was given, and `flushes` counts how many times it was called.
 */
void ui_frame_stats_frame(uint32_t now_ms, uint32_t frame_us, uint32_t render_us,
                          uint32_t wait_us, uint32_t pixels, uint32_t flushes);

/**
 * The last closed window, with `age_ms` filled in from `now_ms`.
 *
 * Shaped like port_net_info(): the caller owns the struct. Everything is zero
 * and `valid` is false until the first window closes, which is the state the
 * report sites have to render rather than a frame rate nobody measured.
 */
void ui_frame_stats_get(uint32_t now_ms, ui_frame_stats_t *out);

#ifdef __cplusplus
}
#endif

#endif /* UI_FRAME_STATS_H */
