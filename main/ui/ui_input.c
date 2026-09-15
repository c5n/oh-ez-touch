/**
 * @file ui_input.c
 *
 * Swipes, and why this panel does not have any.
 *
 * A swipe used to do three different things depending on which theme was
 * loaded, and one of them was dangerous.
 *
 * There was never a swipe handler on a tile page: the firmware's only
 * LV_EVENT_GESTURE callback lived on the item screen. What looked like a back
 * gesture on a page was LVGL doing something else entirely -- a press that
 * travels still ends as a click on whatever was under its *start*, because a
 * tile is not inside a scrollable container and so nothing ever takes the
 * click away. In the Default theme the back tile happens to sit at the top
 * left, where a right-swipe begins, so the page went back and the mechanism
 * was never questioned. In LCARS the same swipe starts on the spine and opened
 * the settings screen; in JARVIS it starts on a bracket and did nothing. And a
 * swipe that started anywhere else did what tapping there would have done --
 * across the top row of a page that meant **a swipe turned a light on**,
 * confirmed against a real openHAB server.
 *
 * So the policy is now simply that a swipe does nothing, anywhere, in any
 * theme. Two halves, because LVGL can turn a finger that moves into a click by
 * two different routes:
 *
 *   - no object anywhere registers LV_EVENT_GESTURE any more. The item screen
 *     was the last one; its back bar was always the documented way out and the
 *     resistive ArduiTouch panels could never swipe reliably anyway. Nothing
 *     listens, so nothing happens.
 *   - a press that has travelled does not become a tap. That is what this file
 *     is: one callback on the input device itself, which LVGL offers every
 *     click *before* the widget sees it.
 *
 * The hook is lv_indev_add_event_cb() plus lv_indev_stop_processing(): LVGL's
 * send_event() passes PRESSED/CLICKED/SHORT_CLICKED and friends to the
 * indev's own event list first, and skips the object entirely if a handler
 * there asks it to. One callback per indev therefore covers every clickable
 * widget in the firmware -- the twenty-odd tiles, buttons, rows and glyphs
 * that would otherwise each need guarding, and any added later.
 */
#include "ui_input.h"

/* How far a press may travel and still count as a tap, in panel pixels.
 *
 * This is LVGL's own LV_INDEV_DEF_SCROLL_LIMIT, the distance at which it
 * decides a press on a *scrollable* widget has become a drag and withholds the
 * click. Reusing that number is the point rather than a coincidence: every
 * scrolling list in the settings screen has always behaved this way, and a
 * tile was the odd one out. LVGL publishes a setter for the value and no
 * getter, so it is repeated here.
 *
 * It also catches the case LVGL's own gesture detection misses. A gesture
 * needs both distance (50 px) and speed -- lv_indev.c zeroes the accumulated
 * distance on any read that moved less than gesture_min_velocity -- so a
 * deliberately slow drag is never reported as a swipe, and would otherwise
 * still have landed as a tap.
 */
#define UI_INPUT_TAP_SLOP 10

/* At most this many pointer indevs get a guard: the panel's touch controller,
 * or the simulator's mouse, plus the test interface's synthetic pointer. */
#define UI_INPUT_INDEV_MAX 4

/* Where the press being processed started, per input device.
 *
 * Per device rather than one global, because the simulator runs two pointers
 * at once -- the SDL mouse and the test interface -- and a script driving one
 * while a hand rests on the other must not make either forget where it began.
 */
struct press_origin_s
{
    lv_point_t point;
    bool       known;
};

static struct press_origin_s origins[UI_INPUT_INDEV_MAX];
static unsigned              origin_count;

static void press_event(lv_event_t *e)
{
    struct press_origin_s *origin = (struct press_origin_s *)lv_event_get_user_data(e);
    lv_indev_t            *indev  = lv_indev_active();

    if (origin == NULL)
        return;

    if (indev == NULL)
    {
        origin->known = false;
        return;
    }

    lv_indev_get_point(indev, &origin->point);
    origin->known = true;
}

/* Whether the press that is ending travelled far enough not to be a tap. */
static bool press_travelled(lv_indev_t *indev, const struct press_origin_s *origin)
{
    lv_point_t now;

    /* LVGL has already made up its mind: this was a swipe. */
    if (lv_indev_get_gesture_dir(indev) != LV_DIR_NONE)
        return true;

    /* A press whose start was never seen -- the first event after a reset, say
     * -- is given the benefit of the doubt. Swallowing it would lose a real
     * tap, which is the one failure this must not introduce. */
    if (origin->known == false)
        return false;

    lv_indev_get_point(indev, &now);

    return (LV_ABS(now.x - origin->point.x) > UI_INPUT_TAP_SLOP)
        || (LV_ABS(now.y - origin->point.y) > UI_INPUT_TAP_SLOP);
}

static void click_event(lv_event_t *e)
{
    struct press_origin_s *origin = (struct press_origin_s *)lv_event_get_user_data(e);
    lv_indev_t            *indev  = lv_indev_active();

    if (indev == NULL || origin == NULL)
        return;

    if (press_travelled(indev, origin) == false)
        return;

    /* The widget under the finger never hears about it. */
    lv_indev_stop_processing(indev);
}

void ui_input_disable_swipes(lv_indev_t *indev)
{
    struct press_origin_s *origin;

    if (indev == NULL || origin_count >= UI_INPUT_INDEV_MAX)
        return;

    origin = &origins[origin_count++];

    lv_indev_add_event_cb(indev, press_event, LV_EVENT_PRESSED, origin);

    /* Only the two that mean "a tap happened".
     *
     * LV_EVENT_LONG_PRESSED and LV_EVENT_LONG_PRESSED_REPEAT are deliberately
     * left alone. They are what holding the setpoint's +/- pad and the
     * keyboard's keys runs on, a hold is not a swipe -- a swipe is over long
     * before LVGL's 400 ms long-press threshold -- and cancelling them on
     * travel would break a finger that drifts a few pixels during a deliberate
     * three-second hold on a resistive panel. */
    lv_indev_add_event_cb(indev, click_event, LV_EVENT_SHORT_CLICKED, origin);
    lv_indev_add_event_cb(indev, click_event, LV_EVENT_CLICKED, origin);
}
