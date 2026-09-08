#include "backlight_control.hpp"

#include <stdio.h>

#include "port/port_backlight.h"
#include "port/port_sys.h"

#ifndef DEBUG_BACKLIGHT_CONTROL
#define DEBUG_BACKLIGHT_CONTROL 0
#endif

void BacklightControl::set_brightness(uint8_t percent)
{
#if DEBUG_BACKLIGHT_CONTROL
    printf("BacklightControl::set_brightness: %u\r\n", (unsigned)percent);
#endif
    BacklightControl::current_brightness = percent;

    /* Straight through: which way round the pin has to move to make the panel
     * brighter is the board's business, and lives in port_backlight. This used
     * to invert the percentage here and then map it to a descending duty, two
     * inversions that cancelled on one board and not on the other. */
    port_backlight_set(percent);
}

bool BacklightControl::resetDimTimeout()
{
    bool woken_up = false;

    if (BacklightControl::current_brightness != BacklightControl::normal_brightness)
    {
#if DEBUG_BACKLIGHT_CONTROL
        printf("BacklightControl::resetDimTimeout: wake up\r\n");
#endif

        set_brightness(BacklightControl::normal_brightness);
        woken_up = true;
    }

    if (BacklightControl::dim_timeout == 0)
        BacklightControl::dim_timeout_timestamp = 0;
    else
        BacklightControl::dim_timeout_timestamp = port_millis() + BacklightControl::dim_timeout * 1000;

    return woken_up;
}

void BacklightControl::setup()
{
    port_backlight_init();

    set_brightness(BacklightControl::normal_brightness);

    BacklightControl::resetDimTimeout();
}

void BacklightControl::loop()
{
    if (   (BacklightControl::dim_timeout_timestamp != 0)
        && (port_millis() >= BacklightControl::dim_timeout_timestamp)
        && (BacklightControl::current_brightness != BacklightControl::dim_brightness))
    {
#if DEBUG_BACKLIGHT_CONTROL
        printf("BacklightControl::loop: activity timeout, dim display\r\n");
#endif
        set_brightness(BacklightControl::dim_brightness);
    }
}
