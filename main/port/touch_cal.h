/**
 * @file touch_cal.h
 *
 * The resistive panel's raw-to-screen map, and how to solve it from taps.
 *
 * A resistive panel reports a 12-bit ADC reading per axis. What turns that into
 * a pixel is an origin and a span per axis -- the raw value at one edge of the
 * screen, and the raw distance across the whole of it. The **span is a span and
 * not a maximum**: it is the divisor. Reading it as a maximum is about eight per
 * cent out across the screen, which looks fine in the middle and misses by half
 * a tile at the edges, and it is the mistake this file exists to stop being
 * repeated in a second place.
 *
 * The arithmetic lives here rather than in port_indev.c because three callers
 * need it and only one of them is the port: the pointer read converts every
 * press with it, the calibration screen converts a recorded press with the
 * *previous* numbers to show what changed, and the host tests check both against
 * the constants the boards ship with. It therefore touches neither LVGL nor IDF
 * nor board_pins.h.
 *
 * Which pair belongs to which axis is not symmetrical and is worth stating once.
 * The X pair always binds the controller's **y** reading and the Y pair always
 * binds its **x** reading, in both orientations -- the panel's axes are what
 * they are, and the calibration numbers stay with the axis they were measured
 * on. What the orientation changes is only which screen axis each pair then
 * feeds.
 */
#ifndef TOUCH_CAL_H
#define TOUCH_CAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The four numbers, in the order and meaning board_pins.h has always used
 * them. Signed, because a solve can produce a negative span and refusing it is
 * this file's job rather than the caller's surprise. */
struct touch_cal_s
{
    int32_t x_origin;
    int32_t x_span;
    int32_t y_origin;
    int32_t y_span;
};

/* One target tap: where the cross was, and what the controller reported. */
struct touch_cal_sample_s
{
    int32_t target_x;
    int32_t target_y;
    int32_t raw_x;
    int32_t raw_y;
};

/* How many samples touch_cal_solve() is written for: the four corners. */
#define TOUCH_CAL_SAMPLES 4

/* Raw to screen.
 *
 * `w` and `h` are the live resolution, so in portrait they are 240 and 320.
 * `flip` is the board's 180-degree mounting -- it inverts both axes after the
 * division, which is where port_indev.c has always applied it.
 *
 * Not clamped. The port clamps afterwards, because a press a little past the
 * edge is a press on the widget at the edge; the calibration screen wants the
 * number the map really produced, because that is the error it is drawing.
 *
 * A zero span yields 0 on that axis rather than dividing by zero.
 */
void touch_cal_apply(const struct touch_cal_s *cal, bool portrait, bool flip,
                     int32_t w, int32_t h, int32_t raw_x, int32_t raw_y,
                     int32_t *screen_x, int32_t *screen_y);

/* Solve a calibration from `count` corner taps.
 *
 * Returns NULL on success, or a short reason suitable for showing on the panel.
 * A refusal leaves `*out` untouched: a calibration that cannot be trusted must
 * not reach the panel, because the one thing it would break is the ability to
 * ask for another one.
 */
const char *touch_cal_solve(const struct touch_cal_sample_s *samples, size_t count,
                            bool portrait, bool flip, int32_t w, int32_t h,
                            struct touch_cal_s *out);

/* Whether `cal` can be used at all -- both spans non-zero. A stored
 * calibration of all zeroes is how config.json says "use the board's". */
bool touch_cal_valid(const struct touch_cal_s *cal);

#ifdef __cplusplus
}
#endif

#endif /* TOUCH_CAL_H */
