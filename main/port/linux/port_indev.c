/**
 * @file linux/port_indev.c
 *
 * The simulator's pointer: the mouse, from LVGL's own SDL driver. Clicks act as
 * touches, which is as close to a finger as a desktop gets.
 *
 * And, for the calibration screen only, a panel that is not there.
 *
 * A desktop has no ADC. SDL reports the window coordinate, exactly, which is
 * why nothing here has ever needed a calibration -- and also why the one
 * procedure that exists to fix a panel could not be run, looked at or tested
 * anywhere except on hardware that the firmware has never been on. So this file
 * models a resistive panel: port_indev_raw_press() takes the position of the
 * press LVGL just delivered and runs a panel's map *backwards* over it, giving
 * the 12-bit pair such a panel would have reported for that finger.
 *
 * The model is used for nothing else. The pointer itself goes on reporting the
 * exact window coordinate, so every `ohez_ctl.py tap x y` still lands where it
 * says it does and no existing test changes meaning. The only thing that reads
 * the model is the code that asks for a raw reading, and the only thing that
 * asks is the calibration screen.
 *
 * OHEZ_TOUCH_SKEW is what makes that screen worth looking at here. It bends the
 * modelled panel away from the calibration the firmware is currently using, so
 * the result view has a real correction to draw rather than four coincident
 * points. Unset, the panel matches the calibration and the correction is zero,
 * which is its own useful case: it is what a calibrated panel looks like.
 */
#include "port_indev.h"

#include <stdio.h>
#include <stdlib.h>

#include "lvgl.h"
#include "port_display.h"
#include "touch_cal.h"
#include "ui/ui_input.h"

/* The panel the simulator pretends to have: the ArduiTouch numbers from
 * port/esp32/board_pins.h. Any real board's would do; these are the ones the
 * documentation quotes, so a reading taken here can be compared with it. */
#define SIM_CAL_X_ORIGIN 275
#define SIM_CAL_X_SPAN   3620
#define SIM_CAL_Y_ORIGIN 264
#define SIM_CAL_Y_SPAN   3532

/* Which way up the display was created. The device needs this because the
 * panel's axes do not turn with the picture; the model needs it for the same
 * reason, one level of pretence further up. */
static bool touch_portrait;

/* What the firmware believes the panel is. */
static struct touch_cal_s cal = {
    SIM_CAL_X_ORIGIN,
    SIM_CAL_X_SPAN,
    SIM_CAL_Y_ORIGIN,
    SIM_CAL_Y_SPAN,
};

/* What the modelled panel actually is. Equal to the above until
 * OHEZ_TOUCH_SKEW says otherwise. */
static struct touch_cal_s truth = {
    SIM_CAL_X_ORIGIN,
    SIM_CAL_X_SPAN,
    SIM_CAL_Y_ORIGIN,
    SIM_CAL_Y_SPAN,
};

static void truth_from_env(void)
{
    const char *skew = getenv("OHEZ_TOUCH_SKEW");
    double      xs = 1.0;
    double      ys = 1.0;
    double      xo = 0.0;
    double      yo = 0.0;

    if (skew == NULL || skew[0] == '\0')
        return;

    if (sscanf(skew, "%lf,%lf,%lf,%lf", &xs, &xo, &ys, &yo) != 4)
    {
        fprintf(stderr, "OHEZ_TOUCH_SKEW: want xscale,xoffset,yscale,yoffset\n");
        return;
    }

    truth.x_span = (int32_t)(SIM_CAL_X_SPAN * xs);
    truth.x_origin = (int32_t)(SIM_CAL_X_ORIGIN + xo);
    truth.y_span = (int32_t)(SIM_CAL_Y_SPAN * ys);
    truth.y_origin = (int32_t)(SIM_CAL_Y_ORIGIN + yo);

    fprintf(stderr, "touch: modelled panel %d/%d %d/%d (calibration %d/%d %d/%d)\n",
            (int)truth.x_origin, (int)truth.x_span, (int)truth.y_origin, (int)truth.y_span,
            (int)cal.x_origin, (int)cal.x_span, (int)cal.y_origin, (int)cal.y_span);
}

void port_indev_init(lv_display_t *disp, bool portrait)
{
    /* lv_sdl_mouse_create() binds itself to the default display, which is the
     * one port_display_init() just created; the arguments are here for the
     * device, where the touch panel has to be told which display it is on and
     * which way up it is. SDL's mouse already reports window coordinates. */
    (void)disp;

    touch_portrait = portrait;
    truth_from_env();

    ui_input_disable_swipes(lv_sdl_mouse_create());
}

bool port_indev_calibratable(void)
{
    /* True, although there is nothing to calibrate: what is being exercised is
     * the procedure, and a simulator that hid it would be a simulator in which
     * the one screen most in need of trying out could not be reached. */
    return true;
}

void port_indev_cal_defaults(struct touch_cal_s *out)
{
    if (out == NULL)
        return;

    out->x_origin = SIM_CAL_X_ORIGIN;
    out->x_span = SIM_CAL_X_SPAN;
    out->y_origin = SIM_CAL_Y_ORIGIN;
    out->y_span = SIM_CAL_Y_SPAN;
}

void port_indev_cal_get(struct touch_cal_s *out)
{
    if (out == NULL)
        return;

    *out = cal;
}

void port_indev_cal_set(const struct touch_cal_s *next)
{
    if (touch_cal_valid(next) == false)
        return;

    /* Stored, and it changes nothing about where the mouse points. That is the
     * difference between the targets stated rather than hidden: on the device
     * this is the line after which the panel reads differently. */
    cal = *next;
}

/* No flip on the simulator: its display is created the right way up, so there
 * is no 180-degree mounting to undo. */
void port_indev_cal_map(const struct touch_cal_s *cal, int32_t raw_x, int32_t raw_y,
                        int32_t *screen_x, int32_t *screen_y)
{
    touch_cal_apply(cal, touch_portrait, false,
                    lv_display_get_horizontal_resolution(NULL),
                    lv_display_get_vertical_resolution(NULL),
                    raw_x, raw_y, screen_x, screen_y);
}

const char *port_indev_cal_solve(const struct touch_cal_sample_s *samples, size_t count,
                                 struct touch_cal_s *out)
{
    return touch_cal_solve(samples, count, touch_portrait, false,
                           lv_display_get_horizontal_resolution(NULL),
                           lv_display_get_vertical_resolution(NULL), out);
}

bool port_indev_raw_press(int32_t *raw_x, int32_t *raw_y)
{
    lv_indev_t *indev = lv_indev_active();
    lv_point_t  point;

    /* Whichever pointer delivered the event being handled -- the SDL mouse or
     * the test interface's synthetic one. Both report panel pixels. */
    if (indev == NULL)
        return false;

    lv_indev_get_point(indev, &point);

    int32_t w = lv_display_get_horizontal_resolution(NULL);
    int32_t h = lv_display_get_vertical_resolution(NULL);

    if (w <= 1 || h <= 1)
        return false;

    /* touch_cal_apply() read backwards. No flip: the simulator's display is
     * created the right way up, so there is no 180-degree mounting to undo. */
    int32_t rx;
    int32_t ry;

    if (touch_portrait)
    {
        rx = truth.y_origin + (point.x * truth.y_span) / w;
        ry = truth.x_origin + (point.y * truth.x_span) / h;
    }
    else
    {
        ry = truth.x_origin + (point.x * truth.x_span) / w;
        rx = truth.y_origin + (point.y * truth.y_span) / h;
    }

    if (raw_x != NULL)
        *raw_x = rx;

    if (raw_y != NULL)
        *raw_y = ry;

    return true;
}
