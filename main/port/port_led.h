/**
 * @file port_led.h
 *
 * The board's indicator LEDs, as a brightness in percent per channel.
 *
 * On the Lanbon L8 these are the three channels of the RGB "mood light"
 * behind the glass. They are exposed as three independent brightnesses rather
 * than as one colour: the hardware is three LEDs on three PWM channels, and a
 * colour is something the thing driving them decides. Combining them here
 * would mean this layer having an opinion about gamma and white balance that
 * no caller asked it for.
 *
 * Separate from port_backlight.h, which is the display and is driven by the
 * dim timeout rather than by anyone's command.
 *
 * Channels are named rather than numbered, because "red" is what a broker
 * wants in a topic and "1" tells nobody anything. The names come from the
 * board's pin table.
 */
#ifndef PORT_LED_H
#define PORT_LED_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * How many LEDs this board has, 0 where there are none.
 *
 * Safe to call before port_led_init().
 */
unsigned port_led_count(void);

/**
 * This channel's name, e.g. "red". NULL when the index is out of range.
 *
 * A pointer into the board's own table, so it outlives any caller and never
 * needs freeing.
 */
const char *port_led_name(unsigned index);

/** Bring the PWMs up, all dark. Idempotent; a no-op where the count is 0. */
void port_led_init(void);

/**
 * 0 is off, 100 is full. Values above 100 are clamped; an index out of range
 * is ignored.
 *
 * Linear in duty, not in perceived brightness. That is the honest thing for
 * this layer to be: a caller mixing a colour wants the channels to relate to
 * each other the way the datasheet says, and a caller fading one up can apply
 * its own curve.
 */
void port_led_set(unsigned index, uint8_t percent);

#ifdef __cplusplus
}
#endif

#endif /* PORT_LED_H */
