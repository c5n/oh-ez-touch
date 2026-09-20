/**
 * @file ui_frame_probe.c
 *
 * Seven LVGL display events, and what each of them is worth knowing.
 *
 * All of them come out of lv_refr.c's _lv_display_refr_timer() and its
 * callees, and the nesting is what makes the arithmetic below work:
 *
 *   REFR_START ......................... every refresh period, drawing or not
 *     RENDER_START ..................... only if something was invalidated
 *       [ FLUSH_WAIT_START/FINISH ] .... waiting for the previous transfer
 *       [ FLUSH_START/FINISH ] ......... handing a strip to the panel
 *     RENDER_READY
 *   REFR_READY
 *
 * So RENDER_START..RENDER_READY is the whole drawing phase and *includes* the
 * waits; the software renderer's own share is that span minus them. That
 * subtraction is the entire point of this file. A panel where the remainder is
 * small is SPI-bound, and the CPU-side settings the README lists cannot help
 * it; a panel where it is large is renderer-bound, and they can.
 *
 * FLUSH_WAIT_START/FINISH are sent unconditionally, ahead of the
 * `if(disp->flush_wait_cb)` branch, so they bracket the wait even though this
 * project sets no wait callback and LVGL therefore spins in
 * `while(disp->flushing);`. The spin does not make a frame late -- it ends when
 * the DMA does -- but it is why the CPU looks busy while it has nothing to do.
 *
 * FLUSH_START carries the lv_area_t as its parameter, which is where the pixel
 * count comes from without either port's flush callback having to count.
 */
#include "ui_frame_probe.h"

#include "ui_frame_stats.h"

#include "port/port_sys.h"

/* One display, one frame in flight. Reset at REFR_START rather than at
 * REFR_READY so that a refresh LVGL abandons -- the deleted-display path takes
 * REFR_START's return value and leaves without sending REFR_READY -- cannot
 * leave its half-counted totals in the next frame. */
static struct
{
    bool     drawing;
    uint64_t refr_start_us;
    uint64_t render_start_us;
    uint64_t wait_start_us;
    uint32_t render_us;
    uint32_t wait_us;
    uint32_t pixels;
    uint32_t flushes;
} frame;

static void on_refr_start(lv_event_t *e)
{
    (void)e;

    frame.drawing   = false;
    frame.render_us = 0;
    frame.wait_us   = 0;
    frame.pixels    = 0;
    frame.flushes   = 0;

    frame.refr_start_us = port_micros();

    /* Even on the cycles that draw nothing: this is what advances the clock the
     * window is measured against when the screen is still. */
    ui_frame_stats_tick((uint32_t)port_millis());
}

static void on_render_start(lv_event_t *e)
{
    (void)e;

    frame.drawing         = true;
    frame.render_start_us = port_micros();
}

static void on_render_ready(lv_event_t *e)
{
    (void)e;

    frame.render_us = (uint32_t)(port_micros() - frame.render_start_us);
}

static void on_flush_wait_start(lv_event_t *e)
{
    (void)e;

    frame.wait_start_us = port_micros();
}

static void on_flush_wait_finish(lv_event_t *e)
{
    (void)e;

    frame.wait_us += (uint32_t)(port_micros() - frame.wait_start_us);
}

static void on_flush_start(lv_event_t *e)
{
    const lv_area_t *area = lv_event_get_param(e);

    if (area != NULL)
        frame.pixels += (uint32_t)lv_area_get_width(area) * (uint32_t)lv_area_get_height(area);

    frame.flushes++;
}

static void on_refr_ready(lv_event_t *e)
{
    (void)e;

    if (frame.drawing == false)
        return;

    /* Clamped rather than trusted. The two spans are read from the same clock
     * and nest, so the subtraction cannot legitimately go negative -- but it is
     * unsigned, and a wrong sign here would report a renderer four thousand
     * seconds slow rather than nothing at all. */
    uint32_t total  = (uint32_t)(port_micros() - frame.refr_start_us);
    uint32_t render = frame.render_us > frame.wait_us ? frame.render_us - frame.wait_us : 0;

    ui_frame_stats_frame((uint32_t)port_millis(), total, render, frame.wait_us,
                         frame.pixels, frame.flushes);

    frame.drawing = false;
}

void ui_frame_probe_attach(lv_display_t *disp)
{
    ui_frame_stats_reset((uint32_t)port_millis());

    lv_display_add_event_cb(disp, on_refr_start, LV_EVENT_REFR_START, NULL);
    lv_display_add_event_cb(disp, on_refr_ready, LV_EVENT_REFR_READY, NULL);
    lv_display_add_event_cb(disp, on_render_start, LV_EVENT_RENDER_START, NULL);
    lv_display_add_event_cb(disp, on_render_ready, LV_EVENT_RENDER_READY, NULL);
    lv_display_add_event_cb(disp, on_flush_start, LV_EVENT_FLUSH_START, NULL);
    lv_display_add_event_cb(disp, on_flush_wait_start, LV_EVENT_FLUSH_WAIT_START, NULL);
    lv_display_add_event_cb(disp, on_flush_wait_finish, LV_EVENT_FLUSH_WAIT_FINISH, NULL);
}
