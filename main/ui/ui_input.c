/**
 * @file ui_input.c
 *
 * The panel's whole pointer policy: what a finger is allowed to mean.
 *
 * A swipe used to do three different things depending on which theme was
 * loaded, and one of them was dangerous.
 *
 * There was never a swipe handler on a tile page: the firmware's only
 * LV_EVENT_GESTURE callback lived on the item screen. What looked like a back
 * gesture on a page was LVGL doing something else entirely -- a press that
 * travels still ends as a click on whatever was under its *start*, because a
 * tile is not inside a scrollable container and so nothing ever takes the
 * click away. In the Material theme the back tile happens to sit at the top
 * left, where a right-swipe begins, so the page went back and the mechanism
 * was never questioned. In LCARS the same swipe starts on the spine and opened
 * the settings screen; in JARVIS it starts on a bracket and did nothing. And
 * a swipe that started anywhere else did what tapping there would have done --
 * across the top row of a page that meant **a swipe turned a light on**,
 * confirmed against a real openHAB server.
 *
 * The policy, in full: a tap is the only thing a finger can mean here.
 *
 *   - no object anywhere registers LV_EVENT_GESTURE. The item screen was the
 *     last one; its back bar was always the documented way out and the
 *     resistive ArduiTouch panels could never swipe reliably anyway. Nothing
 *     listens, so nothing happens.
 *   - nothing scrolls, and nothing but a slider's knob is dragged. Every
 *     container in the firmware is built non-scrollable, and LVGL's own drag
 *     detection is pinned out of reach on top of that (see
 *     ui_input_pointer_policy()) -- except for the length of a press that
 *     lands on a slider. LVGL's slider measures its own drag against the same
 *     scroll limit, so with the limit at 255 px its knob never followed the
 *     finger and only jumped to where it lifted. press_event() lowers the
 *     limit for that one press and drag_end_event() puts it back.
 *   - a tap acts the moment the press is confirmed, not when the finger lifts.
 *     The click goes to whatever the finger landed on, and where it drifts
 *     afterwards is irrelevant: the resistive panel's reported point wanders
 *     several pixels while a finger flattens onto it, and a tap that is judged
 *     at lift-off has to decide whether that drift was a drag -- which is how
 *     a panel full of buttons ends up needing three presses to land one.
 *
 * The last of those is what this file is. Two callbacks on the input device
 * itself, which LVGL offers every event *before* the widget sees it:
 *
 *   - press_event() hands the object under the finger its LV_EVENT_CLICKED
 *     while the press is still going on. It is the same lv_obj_send_event()
 *     call LVGL makes at release -- same code, same param, same bubbling --
 *     so a widget cannot tell the difference, except that it no longer has to
 *     wait for the lift. One callback per indev covers every clickable widget
 *     in the firmware, and any added later.
 *   - click_event() swallows the release-time SHORT_CLICKED and CLICKED, so
 *     nothing hears the tap twice.
 *
 * What the policy costs is the slide-off cancel: a finger that lands on the
 * Restart button has restarted, and sliding away cannot take it back. On a
 * panel where nothing scrolls and nothing swipes there is nothing else for a
 * moving finger to mean, so there is nothing left to cancel.
 *
 * Deliberately left alone:
 *
 *   - LV_EVENT_RELEASED. LVGL's own core turns it into the checked-state
 *     toggle on checkable widgets (the switch, checkable buttons), so those
 *     still flip when the finger comes up, exactly as before. Sliders and the
 *     keyboard's keys also live on PRESSED/PRESSING/RELEASED, none of which
 *     this file touches.
 *   - LV_EVENT_LONG_PRESSED and LV_EVENT_LONG_PRESSED_REPEAT. They are what
 *     holding the setpoint's +/- pad and the keyboard's keys runs on, and a
 *     hold is not a swipe -- a swipe is over long before LVGL's 400 ms
 *     long-press threshold.
 *
 * A press that wakes the dimmed panel never reaches this file at all: the port
 * swallows it below LVGL (port_indev.c), so the first tap after waking only
 * wakes. The two-polls-agree confirmation there still applies to every press,
 * phantom ones included, before any click this file delivers.
 */
#include "ui_input.h"

/* At most this many pointer indevs get the policy: the panel's touch
 * controller, or the simulator's mouse, plus the test interface's synthetic
 * pointer. */
#define UI_INPUT_INDEV_MAX 4

/* Out of reach: 255 px is more than any of these panels is tall, and it is the
 * widest the uint8_t LVGL stores it in goes. */
#define SCROLL_LIMIT_OFF   UINT8_MAX

/* How far a finger on a slider travels before the knob follows it. Small,
 * because the knob is the one thing here that is meant to move with a finger;
 * the few pixels a resistive panel's point wanders while a finger flattens
 * onto it only nudge a knob that is set from the lift-off point anyway. */
#define SLIDER_DRAG_LIMIT  4

/* What this file remembers about the press an input device is in.

 * Per device rather than one global, because the simulator runs two pointers
 * at once -- the SDL mouse and the test interface -- and a script driving one
 * while a hand rests on the other must not make either forget where it began.
 */
struct press_origin_s
{
    bool known;   /* a PRESSED was seen for the press now being released */
    bool clicked; /* its click has already been delivered, at press-down */
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
        origin->clicked = false;
        return;
    }

    origin->known = true;
    origin->clicked = false;

    /* The object the finger landed on. Valid here because this callback runs
     * inside LVGL's own send_event(LV_EVENT_PRESSED) -- after the hit test has
     * found the object, before the object hears anything -- and it is that
     * object LVGL would be sending the press to. NULL is a press on nothing:
     * no object, no click. */
    lv_obj_t *obj = lv_indev_get_active_obj();

    /* Before the first PRESSING, which is where the slider compares how far
     * the finger has gone with this limit. Every press sets it, so a press
     * whose release was never seen cannot leave a slider's limit behind. */
    lv_indev_set_scroll_limit(indev, (obj != NULL && lv_obj_check_type(obj, &lv_slider_class))
                                         ? SLIDER_DRAG_LIMIT
                                         : SCROLL_LIMIT_OFF);

    if (obj == NULL)
        return;

    /* The whole point: the click arrives now, not at lift-off. */
    lv_obj_send_event(obj, LV_EVENT_CLICKED, indev);

    origin->clicked = true;
}

/* Whether the release-time SHORT_CLICKED / CLICKED must be kept from the
 * widget, because the widget already heard from this press. */
static void click_event(lv_event_t *e)
{
    struct press_origin_s *origin = (struct press_origin_s *)lv_event_get_user_data(e);
    lv_indev_t            *indev  = lv_indev_active();

    if (indev == NULL || origin == NULL)
        return;

    /* A press whose start was never seen -- the first event after a reset,
     * say -- is given the benefit of the doubt. Swallowing it would lose a
     * real tap, which is the one failure this must not introduce. */
    if (origin->known == false || origin->clicked == false)
        return;

    /* The widget under the finger never hears the tap twice. */
    lv_indev_stop_processing(indev);
}

/* The press is over, so whatever it was on, nothing can be dragged again
 * until the next one says otherwise. */
static void drag_end_event(lv_event_t *e)
{
    lv_indev_t *indev = lv_indev_active();

    LV_UNUSED(e);

    if (indev != NULL)
        lv_indev_set_scroll_limit(indev, SCROLL_LIMIT_OFF);
}

void ui_input_pointer_policy(lv_indev_t *indev)
{
    struct press_origin_s *origin;

    if (indev == NULL || origin_count >= UI_INPUT_INDEV_MAX)
        return;

    /* Out of reach, not merely out of use. A press that moves can no longer be
     * recognised as a drag on a scrollable widget or accumulate into a swipe,
     * whatever is on screen -- the scroll limit only ever comes down for a
     * press on a slider, see press_event(). */
    lv_indev_set_scroll_limit(indev, SCROLL_LIMIT_OFF);
    lv_indev_set_gesture_min_distance(indev, UINT8_MAX);
    lv_indev_set_gesture_min_velocity(indev, UINT8_MAX);

    origin = &origins[origin_count++];

    lv_indev_add_event_cb(indev, press_event, LV_EVENT_PRESSED, origin);
    lv_indev_add_event_cb(indev, drag_end_event, LV_EVENT_RELEASED, NULL);
    lv_indev_add_event_cb(indev, drag_end_event, LV_EVENT_PRESS_LOST, NULL);

    /* Only the two that mean "a tap happened", and only to take them back:
     * both were delivered already, at press-down. */
    lv_indev_add_event_cb(indev, click_event, LV_EVENT_SHORT_CLICKED, origin);
    lv_indev_add_event_cb(indev, click_event, LV_EVENT_CLICKED, origin);
}
