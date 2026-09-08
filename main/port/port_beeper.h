/**
 * @file port_beeper.h
 *
 * The piezo buzzer, as a tone.
 *
 * Only the PWM is here. The request queue and the task that plays notes from
 * it stay in driver/beeper_control.cpp and run on both targets, for the same
 * reason the backlight's timeout state machine does.
 */
#ifndef PORT_BEEPER_H
#define PORT_BEEPER_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Bring the PWM up, silent.
 *
 * @return false when the board has no buzzer -- the Lanbon L8 does not. The
 *   caller may still queue notes; they are played into nothing. That is a
 *   deliberate exception to this layer's usual rule against silent no-ops: a
 *   sound the hardware cannot make is not an observable difference, and the
 *   alternative is a target guard around every beep in the UI.
 */
bool port_beeper_init(void);

/**
 * Sound `freq` Hz at `volume` percent, or silence at volume 0.
 *
 * Volume is duty cycle, which on a piezo is loudness only very roughly. The
 * scale is the one the UI has always used.
 */
void port_beeper_tone(uint16_t freq, uint8_t volume);

#ifdef __cplusplus
}
#endif

#endif /* PORT_BEEPER_H */
