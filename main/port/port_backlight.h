/**
 * @file port_backlight.h
 *
 * The display backlight, as a brightness in percent.
 *
 * Only the PWM is here. The dim-timeout state machine that decides *what*
 * brightness to ask for stays in driver/backlight_control.cpp and runs on both
 * targets, so its logic is exercised in the simulator even though nothing
 * there gets dimmer -- that state machine is where the bugs would be.
 */
#ifndef PORT_BACKLIGHT_H
#define PORT_BACKLIGHT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Bring the PWM up. Idempotent; safe to call before port_backlight_set(). */
void port_backlight_init(void);

/**
 * 0 is off, 100 is full.
 *
 * The polarity is the board's business, not the caller's: on the ArduiTouch
 * the pin is active low and on the Lanbon active high, and both answer 100
 * here with a fully lit panel.
 */
void port_backlight_set(uint8_t percent);

#ifdef __cplusplus
}
#endif

#endif /* PORT_BACKLIGHT_H */
