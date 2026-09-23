#ifndef UI_CLOCK_HPP
#define UI_CLOCK_HPP

#include "config/config.hpp"

/* The screen a dimmed panel can wear: nothing but the time, the weekday and
 * the date -- white on black, in the theme's largest face.
 *
 * Strictly tied to the dim state -- the backlight's activity timeout is the
 * only trigger, so there is no second idle clock of its own to configure. The
 * setting is the checkbox under LCD Backlight Dimming; when it is off, the
 * panel behaves exactly as it always has and shows the page underneath the
 * dimmed backlight.
 *
 * Chosen to hide everything uniformly: a pushed item screen and the settings
 * screen are taken down through their owners before this goes up, and the
 * waking tap returns to the page rather than to what was covered. The tap
 * that woke the panel is already swallowed by ohez_touch_wake(), so nothing
 * under the clock is pressed on the way back.
 */
void ui_clock_setup(Config *config);

/* Push or pop the screen as the dim state and the setting ask, and tick the
 * labels once a second while it is up. Called unconditionally from the main
 * loop -- the clock is worth showing on a panel that has no WLAN, which on
 * the device is also one whose NTP has not answered yet: the time reads
 * "--:--" until it does, which is the convention the header clock set. */
void ui_clock_loop(void);

#endif /* UI_CLOCK_HPP */