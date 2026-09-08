#ifndef BEEPER_CONTROL_HPP
#define BEEPER_CONTROL_HPP

#include "beeper_control_pitches.h"

#include <stdint.h>

#ifndef BEEPER_CONTROL_QUEUE_LENGTH
#define BEEPER_CONTROL_QUEUE_LENGTH 4
#endif

/* Notes are played from a task of their own, so that a UI event can ask for a
 * three-note chime without blocking the screen for its duration. Shared by both
 * targets; the tone behind it is port_beeper, which is silent in the simulator
 * and on the Lanbon, neither of which has a buzzer.
 *
 * Queue a note. Silently dropped if the queue is full or beeper_enable() has
 * not run -- a missed blip is not worth blocking a touch handler for. */
void beeper_playNote(uint16_t note, uint8_t volume, uint16_t duration, uint16_t pause);

/* Bring the PWM up, silent. */
void beeper_setup(void);

/* Start the queue and the task. Idempotent: it is called from setup() and
 * again from settings_apply_live() on every save, and used to leak a queue and
 * a task each time. */
void beeper_enable(void);

#endif
