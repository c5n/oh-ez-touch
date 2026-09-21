#ifndef UI_ACTIVITY_H
#define UI_ACTIVITY_H

/**
 * @file ui_activity.h
 *
 * Whether somebody is using the panel: one bit, and the ten seconds after the
 * last touch.
 *
 * This exists because "every touch" is not something an installation can be
 * told about. A finger walking a settings list presses a row every few hundred
 * milliseconds, and a broker that hears all of them has been sent thirty
 * messages to say what one bit says -- while a rule that wants to turn the
 * hall light on when somebody walks up to the panel needs the *edge* and
 * nothing else.
 *
 * So an interaction is a window rather than an event. It opens on the first
 * touch and closes ten seconds after the last one, and what is published is
 * the two edges of it: see `ui/activity` in [MQTT](../../doc/mqtt.md).
 *
 * The clock underneath is LVGL's own. lv_indev_read() stamps the display on
 * every poll that comes back pressed, before any hit testing, so this counts a
 * press on the wallpaper as readily as one on a tile, and it counts the
 * simulator's mouse and the test interface's synthetic pointer as readily as a
 * finger on glass. The one touch LVGL does not see is the tap that wakes a
 * dimmed display -- port_indev swallows it -- so ohez_touch_wake() says so
 * itself with lv_display_trigger_activity().
 */

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** How long after the last touch an interaction is still in progress. */
#define UI_ACTIVITY_WINDOW_MS 10000

/**
 * Poll LVGL's activity clock and move the window.
 *
 * Polled rather than hooked, for one reason: the state a panel boots in must
 * not read as a touch. LVGL stamps the display when it is *created*, so its
 * inactivity clock says "active" for the first ten seconds of every boot --
 * which is inside the window the broker connects in. So this watches that
 * clock for the moment it goes backwards, which only a press can do, and
 * nothing is an interaction until one has.
 *
 * Called from ohez_loop() on both targets, once per frame: the clock is
 * sampled rather than integrated, so a call that is skipped costs nothing but
 * its own resolution.
 */
void ui_activity_loop(void);

/** True while an interaction is in progress. */
bool ui_activity_active(void);

/**
 * Implemented by the application; called from ui_activity_loop() whenever
 * input has just been seen.
 *
 * It restarts the backlight's dim timeout, and it exists because
 * ohez_touch_wake() cannot: that one is called from the panel's pointer read,
 * which the simulator does not have -- LVGL's SDL mouse reads itself -- so on
 * the host the dim timer was started at boot and nothing ever reset it. The
 * shared state machine dimmed once and stayed dimmed, which was invisible
 * while nothing but a PWM pin depended on it and is not now that the state is
 * published.
 *
 * This one only resets the timeout. Waking with a chime, and swallowing the
 * press that did it, stay in ohez_touch_wake(), because only the port that
 * read the press can withhold it.
 */
void ohez_touch_activity(void);

#ifdef __cplusplus
}
#endif

#endif /* UI_ACTIVITY_H */
