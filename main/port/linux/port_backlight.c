/**
 * @file linux/port_backlight.c
 *
 * No backlight on a desktop, and nothing sensible to stand in for one: dimming
 * the SDL window would misrepresent what the panel looks like, which is the one
 * thing the simulator exists to get right.
 *
 * So this is an explicit no-op rather than an absent port. The part worth
 * exercising -- when to dim, and what to dim to -- is the shared state machine
 * in driver/backlight_control.cpp, and it runs here too.
 */
#include "port_backlight.h"

void port_backlight_init(void)
{
}

void port_backlight_set(uint8_t percent)
{
    (void)percent;
}

void port_backlight_fade(uint8_t percent, uint16_t ms)
{
    (void)percent;
    (void)ms;
}
