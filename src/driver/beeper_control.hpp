#ifndef BEEPER_CONTROL_HPP
#define BEEPER_CONTROL_HPP

#include "beeper_control_pitches.h"

#include "stdint.h"

#ifndef BEEPER_CONTROL_PWM_CHANNEL
#define BEEPER_CONTROL_PWM_CHANNEL 1
#endif

#ifndef BEEPER_CONTROL_QUEUE_LENGTH
#define BEEPER_CONTROL_QUEUE_LENGTH 4
#endif

void beeper_playNote(uint16_t note, uint8_t volume, uint16_t duration, uint16_t pause);
void beeper_setup(uint8_t pin);
void beeper_enable(void);

#endif
