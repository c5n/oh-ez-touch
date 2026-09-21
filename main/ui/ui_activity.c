/**
 * @file ui_activity.c
 *
 * See ui_activity.h.
 */
#include "ui_activity.h"

#include <lvgl.h>

/* LVGL's inactivity clock as it read on the previous call, and whether input
 * has been seen at all.
 *
 * The clock is watched for going *backwards* rather than turned into an
 * absolute timestamp, and that is not a style choice. lv_display_get_inactive_
 * time() computes its answer from lv_tick_get() itself, so reconstructing the
 * stamp as `lv_tick_get() - inactive` reads the clock a second time and lands
 * a millisecond or two later -- which makes the reconstructed stamp creep
 * forward on its own, and every creep looks like a touch. That is exactly what
 * the first version of this file did, and an idle simulator announced an
 * interaction within a second of connecting to the broker.
 *
 * A decrease cannot be manufactured that way: the clock only goes down when
 * lv_indev_read() has stamped the display, which it does on any poll that came
 * back pressed, before any hit testing.
 *
 * Only ever touched from the LVGL task, which is where ohez_loop() runs. */
static uint32_t last_inactive;
static bool     have_last;
static bool     seen_input;
static bool     active;

void ui_activity_loop(void)
{
    uint32_t inactive = lv_display_get_inactive_time(NULL);

    /* No display yet, which lv_display_get_inactive_time() answers with
     * UINT32_MAX rather than with an error. */
    if (inactive == UINT32_MAX)
        return;

    if (have_last == true && inactive < last_inactive)
    {
        seen_input = true;

        /* The backlight's timeout is the same question asked over a longer
         * window, and this is the one place both targets answer it. */
        ohez_touch_activity();
    }

    last_inactive = inactive;
    have_last     = true;

    /* Nothing is an interaction until something has been. A panel fresh out of
     * a reboot has an inactivity clock that reads seconds rather than minutes
     * -- lv_display_create() stamps it -- and the broker connects well inside
     * that, so without the flag every boot would announce a touch. */
    active = (seen_input == true) && (inactive < UI_ACTIVITY_WINDOW_MS);
}

bool ui_activity_active(void)
{
    return active;
}
