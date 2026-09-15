/**
 * @file port_beeper.h
 *
 * The piezo buzzer, as a tone.
 *
 * Only the PWM is here. The request queue and the task that plays chimes from
 * it stay in control/beeper_control.cpp, and the arithmetic that decides what
 * a chime sounds like stays in control/beeper_mixer.c, for the same reason the
 * backlight's timeout state machine does.
 *
 * The one exception is port_beeper_render(), and it is a deliberate crack in
 * this header's usual rule against knowing anything above it -- see there. It
 * is one prototype wide rather than a whole header, because beeper_common.h
 * took the tone path out of it: a level and a band are properties of the piezo,
 * not of whichever engine is arranging the notes.
 */
#ifndef PORT_BEEPER_H
#define PORT_BEEPER_H

#include "control/beeper_common.h"
#include "control/beeper_mixer.h" /* port_beeper_render() only; see there */

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Bring the PWM up, silent.
 *
 * @return false when the board has no buzzer -- the Lanbon L8 does not. The
 *   caller may still queue chimes; they are played into nothing. That is a
 *   deliberate exception to this layer's usual rule against silent no-ops: a
 *   sound the hardware cannot make is not an observable difference, and the
 *   alternative is a target guard around every beep in the UI.
 */
bool port_beeper_init(void);

/**
 * Sound `freq` Hz at `level`, or silence at level 0.
 *
 * `level` is 0..BEEPER_LEVEL_MAX, i.e. per mille, and it becomes duty cycle --
 * which on a piezo is loudness, and only roughly. Per mille rather than per
 * cent because the shipped master volume is 25: on a 0..100 scale that would
 * leave a whole envelope twelve steps tall, where a PLUCK decay wants fifty.
 */
void port_beeper_tone(uint16_t freq, uint16_t level);

/**
 * Play a whole chime here instead, if this target would rather.
 *
 * @return false on the panel, where the answer is no and beeper_control walks
 *   the mixer's frames itself, calling port_beeper_tone() per slot. Only the
 *   simulator returns true.
 *
 * This exists because the simulator cannot honour a two-millisecond slot from
 * a task: its FreeRTOS tick is four milliseconds and
 * sdkconfig.defaults.linux gives a good reason not to raise it. Following the
 * port_beeper_tone() calls would therefore render every chord coarser than the
 * device plays it, and the simulator would be lying in the one direction that
 * matters -- it would sound worse than the hardware, and somebody would
 * "fix" a table that was fine.
 *
 * So the simulator takes the chime and renders it against beeper_mixer_frame()
 * at audio resolution, which is exactly what that function being pure buys.
 * The cost is this header knowing what a chime is, which is a real cost and
 * worth it: the alternative is a second synthesiser, in another language,
 * drifting from this one.
 */
bool port_beeper_render(const struct beeper_chime_s *chime, uint8_t master);

#ifdef __cplusplus
}
#endif

#endif /* PORT_BEEPER_H */
