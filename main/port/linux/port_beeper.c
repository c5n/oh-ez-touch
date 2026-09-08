/**
 * @file linux/port_beeper.c
 *
 * No buzzer on a desktop. Reported the same way a board without one is, so the
 * UI's beep calls are harmless here without a guard around each of them.
 */
#include "port_beeper.h"

bool port_beeper_init(void)
{
    return false;
}

void port_beeper_tone(uint16_t freq, uint8_t volume)
{
    (void)freq;
    (void)volume;
}
