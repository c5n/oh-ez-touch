/**
 * @file touch_cal.c
 *
 * The map in one direction, and the fit in the other.
 *
 * The fit is two independent one-dimensional solves, not a plane fit, because
 * the map it has to produce is two independent one-dimensional maps. Four
 * corner taps therefore give each solve two samples at each end, which are
 * averaged before the two-point solve: one shaky tap then moves an end by half
 * its own error instead of all of it, and the pair is also what makes the
 * disagreement check below possible at all.
 */
#include "touch_cal.h"

/* What a solved span may be, in raw counts.
 *
 * The lower bound is the interesting one. A panel read through a loose ribbon,
 * or four taps that all landed in the same place, produces a small span -- and a
 * small span is a large multiplier, so the result is a panel where a finger
 * anywhere sends the pointer to a corner. That is precisely the state from which
 * the user can no longer reach the calibration screen to try again, which is why
 * this is refused here rather than clamped into range and stored. The shipped
 * constants are 3532 to 3700 across a whole screen, so 500 is far below anything
 * a working panel produces and well above anything a broken one does. */
#define SPAN_MIN 500
#define SPAN_MAX 4095

/* An origin is the raw value at the screen's edge and is normally a few hundred.
 * It may legitimately go a little negative -- the edge of the glass can sit
 * outside the touch-sensitive area -- but not by thousands. */
#define ORIGIN_MIN (-2000)
#define ORIGIN_MAX 4095

/* How far the two taps at one end of an axis may disagree before the pair is
 * taken for a mis-registered press rather than a panel. Two corners at the same
 * end of an axis read the same value on that axis in theory; in practice a
 * resistive panel's corners are its least linear region. 300 counts is about
 * eight per cent of a full span, which is far wider than that non-linearity and
 * far narrower than a tap that landed on the wrong cross. */
#define AGREE_RAW 300

/* One end of one axis: the two corner readings that share a target coordinate. */
struct end_s
{
    int32_t p;
    int32_t sum;
    int32_t min;
    int32_t max;
    int32_t count;
};

static int32_t map_axis(int32_t raw, int32_t origin, int32_t span, int32_t len)
{
    /* Nothing sensible to return, and returning it is better than the trap: an
     * uncalibrated struct reaches this only through a caller bug, and a pointer
     * stuck at the top left is a diagnosable panel. */
    if (span == 0)
        return 0;

    return ((raw - origin) * len) / span;
}

void touch_cal_apply(const struct touch_cal_s *cal, bool portrait, bool flip,
                     int32_t w, int32_t h, int32_t raw_x, int32_t raw_y,
                     int32_t *screen_x, int32_t *screen_y)
{
    int32_t sx;
    int32_t sy;

    if (cal == NULL)
        return;

    if (portrait)
    {
        /* Portrait drops the axis swap, the same change the panel's MADCTL gets
         * in port_display.c: the screen's x comes from the controller's x
         * again. The pairs stay with the raw axes they were measured on. */
        sx = map_axis(raw_x, cal->y_origin, cal->y_span, w);
        sy = map_axis(raw_y, cal->x_origin, cal->x_span, h);
    }
    else
    {
        sx = map_axis(raw_y, cal->x_origin, cal->x_span, w);
        sy = map_axis(raw_x, cal->y_origin, cal->y_span, h);
    }

    if (flip)
    {
        sx = (w - 1) - sx;
        sy = (h - 1) - sy;
    }

    if (screen_x != NULL)
        *screen_x = sx;

    if (screen_y != NULL)
        *screen_y = sy;
}

bool touch_cal_valid(const struct touch_cal_s *cal)
{
    return cal != NULL && cal->x_span != 0 && cal->y_span != 0;
}

/* Fold one (raw, target) point into whichever of the two ends it belongs to.
 * The ends are discovered rather than assumed, so the caller is free to order
 * its corners however it likes. */
static bool end_add(struct end_s *ends, int32_t p, int32_t raw)
{
    for (unsigned i = 0; i < 2; i++)
    {
        if (ends[i].count != 0 && ends[i].p != p)
            continue;

        ends[i].p = p;
        ends[i].sum += raw;
        ends[i].count++;

        if (ends[i].count == 1 || raw < ends[i].min)
            ends[i].min = raw;

        if (ends[i].count == 1 || raw > ends[i].max)
            ends[i].max = raw;

        return true;
    }

    /* A third distinct target coordinate on one axis: the caller drew targets
     * this solver was not written for. */
    return false;
}

/* The two-point solve for one axis, over an axis of `len` pixels. */
static const char *fit_axis(const int32_t *raw, const int32_t *p, size_t count,
                            int32_t len, int32_t *origin_out, int32_t *span_out)
{
    struct end_s ends[2] = {{0, 0, 0, 0, 0}, {0, 0, 0, 0, 0}};

    for (size_t i = 0; i < count; i++)
    {
        if (end_add(ends, p[i], raw[i]) == false)
            return "Targets do not line up";
    }

    if (ends[0].count == 0 || ends[1].count == 0)
        return "Targets do not line up";

    for (unsigned i = 0; i < 2; i++)
    {
        if ((ends[i].max - ends[i].min) > AGREE_RAW)
            return "Two taps disagree. Try again";
    }

    const struct end_s *lo = (ends[0].p < ends[1].p) ? &ends[0] : &ends[1];
    const struct end_s *hi = (lo == &ends[0]) ? &ends[1] : &ends[0];

    int32_t p_lo = lo->p;
    int32_t p_hi = hi->p;
    int32_t r_lo = lo->sum / lo->count;
    int32_t r_hi = hi->sum / hi->count;

    /* Largest term here is a full-scale raw difference times a screen axis:
     * 4095 * 320 is 1.3 million, three orders below an int32_t. */
    int32_t span = ((r_hi - r_lo) * len) / (p_hi - p_lo);

    if (span < 0)
    {
        /* Not a calibration error. The axis runs backwards, which is how the
         * panel is mounted or wired, and the firmware expresses that with
         * OHEZ_TOUCH_FLIP in board_pins.h. Storing it as a negative span would
         * work by accident on this axis and lie about the board. */
        return "Panel is mirrored. See board_pins.h";
    }

    if (span < SPAN_MIN)
        return "Taps too close together";

    if (span > SPAN_MAX)
        return "Readings out of range";

    int32_t origin = r_lo - ((p_lo * span) / len);

    if (origin < ORIGIN_MIN || origin > ORIGIN_MAX)
        return "Readings out of range";

    *origin_out = origin;
    *span_out = span;

    return NULL;
}

const char *touch_cal_solve(const struct touch_cal_sample_s *samples, size_t count,
                            bool portrait, bool flip, int32_t w, int32_t h,
                            struct touch_cal_s *out)
{
    /* The X pair's raws and the coordinate each one should map to, and the same
     * for the Y pair. Which screen coordinate that is depends on the
     * orientation; which raw axis it comes from does not. */
    int32_t x_raw[TOUCH_CAL_SAMPLES];
    int32_t x_p[TOUCH_CAL_SAMPLES];
    int32_t y_raw[TOUCH_CAL_SAMPLES];
    int32_t y_p[TOUCH_CAL_SAMPLES];

    if (samples == NULL || out == NULL)
        return "No samples";

    if (count != TOUCH_CAL_SAMPLES)
        return "Wrong number of taps";

    if (w <= 1 || h <= 1)
        return "No display";

    for (size_t i = 0; i < count; i++)
    {
        /* The target as the map sees it, which is before the flip: the flip is
         * the last thing touch_cal_apply() does, so it is the first thing the
         * solve has to undo. */
        int32_t px = flip ? ((w - 1) - samples[i].target_x) : samples[i].target_x;
        int32_t py = flip ? ((h - 1) - samples[i].target_y) : samples[i].target_y;

        x_raw[i] = samples[i].raw_y;
        y_raw[i] = samples[i].raw_x;

        x_p[i] = portrait ? py : px;
        y_p[i] = portrait ? px : py;
    }

    int32_t x_len = portrait ? h : w;
    int32_t y_len = portrait ? w : h;

    struct touch_cal_s solved = {0, 0, 0, 0};

    const char *reason = fit_axis(x_raw, x_p, count, x_len, &solved.x_origin, &solved.x_span);

    if (reason != NULL)
        return reason;

    reason = fit_axis(y_raw, y_p, count, y_len, &solved.y_origin, &solved.y_span);

    if (reason != NULL)
        return reason;

    /* Written only once both axes are known good, so a refusal leaves whatever
     * the caller had. */
    *out = solved;

    return NULL;
}
