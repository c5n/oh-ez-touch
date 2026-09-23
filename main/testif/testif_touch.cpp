/**
 * @file testif_touch.cpp
 *
 * The synthetic pointer: a second LVGL input device, beside the SDL mouse, that
 * plays back a queue of pointer states.
 *
 * The queue is consumed by the *read*, not by the clock. Each read takes the
 * next step unless the current one has asked to be dwelt on, which is what
 * makes a swipe reliable: LVGL works out a gesture from the movement between
 * consecutive reads (indev_gesture() in lv_indev.c), and it throws the
 * accumulated distance away on any read that moved less than
 * `gesture_min_velocity` on both axes. A schedule driven by wall-clock time
 * would sooner or later report the same position twice -- one zero-movement
 * read -- and the gesture would silently never fire. One step per read cannot.
 *
 * Dwell is what a tap needs, because a press is measured in milliseconds and
 * not in reads: LVGL's long-press threshold is wall-clock. So a step may ask to
 * be held for a time before the next is taken, and a swipe simply asks for
 * none.
 *
 * The real pointer stays registered alongside this -- the SDL mouse on the
 * simulator, the touch panel on a device. They do not fight: the SDL mouse is
 * in LV_INDEV_MODE_EVENT and this one is in the default timer mode, so the
 * window stays clickable by hand while a script drives it, and on the panel
 * the finger and the script simply take turns.
 */

#include "sdkconfig.h"

#if CONFIG_IDF_TARGET_LINUX || CONFIG_OHEZ_TESTIF

#include <stdlib.h>
#include <string.h>

#include <lvgl.h>

#include "port_display.h"

#include "testif_internal.hpp"
#include "ui/ui_input.h"

/* Room for the longest gesture plus its edges. A 400 ms swipe is 25 steps at
 * the read period below, so this holds two of them back to back. */
#define STEP_MAX 64

/* What LVGL reads an indev at: lv_indev_create() gives the read timer a period
 * of LV_DEF_REFR_PERIOD, which lv_conf.h sets to 16 ms. Durations arriving as
 * milliseconds are turned into step counts with it. */
#define READ_PERIOD_MS 16

/* LVGL's own gesture thresholds, from LV_INDEV_DEF_GESTURE_LIMIT and
 * LV_INDEV_DEF_GESTURE_MIN_VELOCITY in lv_indev.c. This project overrides
 * neither, and a swipe that does not clear both of them does nothing at all --
 * so they are checked here, where the command can be refused with a reason,
 * rather than left to fail silently in the UI. */
#define GESTURE_MIN_DISTANCE 50
#define GESTURE_MIN_VELOCITY 3

/* Below this a press can fall entirely between two reads and never be seen. */
#define TAP_HOLD_MIN_MS 20

#define TAP_HOLD_DEFAULT_MS       60
#define LONGPRESS_HOLD_DEFAULT_MS 500
#define SWIPE_DEFAULT_MS          200

/* Where a named swipe runs between. Inset from the edges so that the gesture
 * starts on the panel rather than on its last pixel. */
#define SWIPE_EDGE_INSET 20

struct step_s
{
    int32_t  x;
    int32_t  y;
    bool     pressed;
    uint32_t dwell_ms; /* hold this step for at least this long before the next */
};

static struct step_s queue[STEP_MAX];
static unsigned      head;      /* next step to take */
static unsigned      tail;      /* where the next push lands */

static struct step_s current;   /* what the read callback is reporting now */
static uint32_t      current_since;

/* Where the pointer ends up once everything queued has played, which is what a
 * new command has to reason about. Not the same as `current`: a command is
 * queued well before it is read, so `current` is the past. */
static int32_t queued_x;
static int32_t queued_y;
static bool    queued_pressed;

static unsigned queue_free(void)
{
    return STEP_MAX - 1 - ((tail - head + STEP_MAX) % STEP_MAX);
}

static bool push(int32_t x, int32_t y, bool pressed, uint32_t dwell_ms)
{
    if (queue_free() == 0)
        return false;

    queue[tail].x        = x;
    queue[tail].y        = y;
    queue[tail].pressed  = pressed;
    queue[tail].dwell_ms = dwell_ms;

    tail = (tail + 1) % STEP_MAX;

    queued_x       = x;
    queued_y       = y;
    queued_pressed = pressed;

    return true;
}

/**
 * Make sure the next press is a fresh edge.
 *
 * LVGL starts a click on the released-to-pressed transition, so pressing while
 * already pressed -- after a `press` with no `release`, or between two taps --
 * would read as one continuous touch and the second tap would do nothing.
 * Queued rather than applied, so it costs nothing when the pointer is already
 * up and never discards a gesture that has not played yet.
 */
static bool ensure_released(void)
{
    if (queued_pressed == false)
        return true;

    return push(queued_x, queued_y, false, 0);
}

static void read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;

    if (head != tail && lv_tick_elaps(current_since) >= current.dwell_ms)
    {
        current       = queue[head];
        head          = (head + 1) % STEP_MAX;
        current_since = lv_tick_get();
    }

    data->point.x = current.x;
    data->point.y = current.y;
    data->state   = current.pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

void testif_touch_init(void)
{
    lv_indev_t *indev = lv_indev_create();

    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, read_cb);

    /* The same policy the finger gets. A script that swipes has to see exactly
     * what a hand would see, or the interface stops being a way to test the
     * panel and becomes a second, more permissive one. */
    ui_input_disable_swipes(indev);
}

/* ------------------------------------------------------------------ presses */

static const char *tap_common(const testif_cmd_t *cmd, uint32_t default_ms)
{
    int32_t     x      = 0;
    int32_t     y      = 0;
    const char *reason = testif_coords(cmd, 1, &x, &y);

    if (reason != NULL)
        return reason;

    long hold = (long)default_ms;

    if (cmd->argc > 3 && testif_arg_int(cmd, 3, &hold) == false)
        return "hold is not a number";

    if (hold < TAP_HOLD_MIN_MS)
        return "hold too short";

    if (queue_free() < 3)
        return "queue full";

    ensure_released();
    push(x, y, true, (uint32_t)hold);
    push(x, y, false, 0);

    return NULL;
}

const char *testif_cmd_tap(const testif_cmd_t *cmd, char *out, size_t out_size)
{
    (void)out;
    (void)out_size;

    return tap_common(cmd, TAP_HOLD_DEFAULT_MS);
}

const char *testif_cmd_longpress(const testif_cmd_t *cmd, char *out, size_t out_size)
{
    (void)out;
    (void)out_size;

    /* Only the default differs. It is over LVGL's 400 ms long-press threshold,
     * which is the whole point of having the command: reaching a long press
     * otherwise means knowing that number and passing it to `tap`. */
    return tap_common(cmd, LONGPRESS_HOLD_DEFAULT_MS);
}

/* ------------------------------------------------------------------- swipes */

static const char *swipe_points(int32_t x1, int32_t y1, int32_t x2, int32_t y2, long ms)
{
    if (ms <= 0)
        return "duration is not positive";

    int32_t dx = x2 - x1;
    int32_t dy = y2 - y1;

    /* Per axis, because that is how LVGL measures it: the gesture fires when
     * either axis of the accumulated movement passes the limit. */
    int32_t reach = LV_MAX(LV_ABS(dx), LV_ABS(dy));

    if (reach <= GESTURE_MIN_DISTANCE)
        return "gesture too short";

    long moves = ms / READ_PERIOD_MS;

    if (moves < 1)
        moves = 1;

    /* More steps than reads is wasted -- two would land in one read and the
     * first would never be seen -- and each extra step lowers the movement per
     * read towards the velocity floor. */
    if (moves > STEP_MAX - 4)
        moves = STEP_MAX - 4;

    if (reach / moves < GESTURE_MIN_VELOCITY)
        return "gesture too slow";

    if (queue_free() < (unsigned)moves + 3)
        return "queue full";

    ensure_released();
    push(x1, y1, true, 0);

    for (long i = 1; i <= moves; i++)
        push(x1 + (int32_t)((long)dx * i / moves), y1 + (int32_t)((long)dy * i / moves), true, 0);

    push(x2, y2, false, 0);

    return NULL;
}

static bool swipe_direction(const char *name, int32_t cx, int32_t cy,
                            int32_t *x1, int32_t *y1, int32_t *x2, int32_t *y2)
{
    /* The live resolution, so a portrait panel's swipes cross it rather than
     * the landscape rectangle PORT_DISPLAY_WIDTH/HEIGHT describe. */
    const int32_t left   = SWIPE_EDGE_INSET;
    const int32_t right  = lv_display_get_horizontal_resolution(NULL) - 1 - SWIPE_EDGE_INSET;
    const int32_t top    = SWIPE_EDGE_INSET;
    const int32_t bottom = lv_display_get_vertical_resolution(NULL) - 1 - SWIPE_EDGE_INSET;

    if (strcmp(name, "right") == 0)
    {
        *x1 = left;  *y1 = cy; *x2 = right; *y2 = cy;
    }
    else if (strcmp(name, "left") == 0)
    {
        *x1 = right; *y1 = cy; *x2 = left;  *y2 = cy;
    }
    else if (strcmp(name, "down") == 0)
    {
        *x1 = cx; *y1 = top;    *x2 = cx; *y2 = bottom;
    }
    else if (strcmp(name, "up") == 0)
    {
        *x1 = cx; *y1 = bottom; *x2 = cx; *y2 = top;
    }
    else
    {
        return false;
    }

    return true;
}

const char *testif_cmd_swipe(const testif_cmd_t *cmd, char *out, size_t out_size)
{
    (void)out;
    (void)out_size;

    if (cmd->argc < 2)
        return "want a direction or x1 y1 x2 y2";

    int32_t x1 = 0;
    int32_t y1 = 0;
    int32_t x2 = 0;
    int32_t y2 = 0;

    /* The named form crosses the whole panel on one axis, through the centre or
     * through a point the caller names. It is the form a test normally wants --
     * "swipe right" to dismiss an item screen -- and it always clears the two
     * thresholds, which the explicit form is free not to. */
    int32_t cx = lv_display_get_horizontal_resolution(NULL) / 2;
    int32_t cy = lv_display_get_vertical_resolution(NULL) / 2;

    if (swipe_direction(cmd->argv[1], cx, cy, &x1, &y1, &x2, &y2) == true)
    {
        if (cmd->argc > 2)
        {
            const char *reason = testif_coords(cmd, 2, &cx, &cy);

            if (reason != NULL)
                return reason;

            swipe_direction(cmd->argv[1], cx, cy, &x1, &y1, &x2, &y2);
        }

        return swipe_points(x1, y1, x2, y2, SWIPE_DEFAULT_MS);
    }

    const char *reason = testif_coords(cmd, 1, &x1, &y1);

    if (reason != NULL)
        return reason;

    reason = testif_coords(cmd, 3, &x2, &y2);

    if (reason != NULL)
        return reason;

    long ms = SWIPE_DEFAULT_MS;

    if (cmd->argc > 5 && testif_arg_int(cmd, 5, &ms) == false)
        return "duration is not a number";

    return swipe_points(x1, y1, x2, y2, ms);
}

/* --------------------------------------------------------------- primitives */

const char *testif_cmd_press(const testif_cmd_t *cmd, char *out, size_t out_size)
{
    (void)out;
    (void)out_size;

    int32_t     x      = 0;
    int32_t     y      = 0;
    const char *reason = testif_coords(cmd, 1, &x, &y);

    if (reason != NULL)
        return reason;

    return push(x, y, true, 0) ? NULL : "queue full";
}

const char *testif_cmd_move(const testif_cmd_t *cmd, char *out, size_t out_size)
{
    (void)out;
    (void)out_size;

    int32_t     x      = 0;
    int32_t     y      = 0;
    const char *reason = testif_coords(cmd, 1, &x, &y);

    if (reason != NULL)
        return reason;

    /* Carries the pressed state rather than setting one, so `move` drags after
     * a `press` and merely travels without one. */
    return push(x, y, queued_pressed, 0) ? NULL : "queue full";
}

const char *testif_cmd_release(const testif_cmd_t *cmd, char *out, size_t out_size)
{
    (void)cmd;
    (void)out;
    (void)out_size;

    return push(queued_x, queued_y, false, 0) ? NULL : "queue full";
}

#endif /* CONFIG_IDF_TARGET_LINUX || CONFIG_OHEZ_TESTIF */
