#ifndef LED_HPP
#define LED_HPP

/**
 * @file led.hpp
 *
 * The board's indicator LEDs, over MQTT and nothing else.
 *
 * On the Lanbon L8 these are the three channels of the mood light behind the
 * glass. Each answers `<prefix>/led/<name>/set` and reports on
 * `<prefix>/led/<name>`; the names are the board's, and are red, green and
 * blue there. The payloads are in led.cpp.
 *
 * Both calls are no-ops on a board with no LEDs, so main.cpp does not guard
 * them.
 */

/** Bring the LEDs up, all dark, and claim their topics. Before the first
 * ohez_mqtt_loop(), so the subscription is in place when the client
 * connects. */
void led_setup(void);

/** Publish whatever has changed, and everything again after a reconnect.
 * Commands are applied from inside ohez_mqtt_loop(), so call this after it. */
void led_loop(void);

#endif /* LED_HPP */
