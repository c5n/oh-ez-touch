#include "backlight_control.hpp"
#include "Arduino.h"
#include "debug.h"

#ifndef DEBUG_BACKLIGHT_CONTROL
#define DEBUG_BACKLIGHT_CONTROL 0
#endif

void BacklightControl::set_brightness(uint8_t percent)
{
#if DEBUG_BACKLIGHT_CONTROL
    debug_printf("BacklightControl::set_brightness: %u\n", percent);
#endif
    ledcWrite(BACKLIGHT_CONTROL_PWM_CHANNEL, map(percent, 0, 100, 1023, 0));
    BacklightControl::current_brightness = percent;
}

bool BacklightControl::resetDimTimeout()
{
    bool woken_up = false;

    if (BacklightControl::current_brightness != BacklightControl::normal_brightness)
    {
#if DEBUG_BACKLIGHT_CONTROL
        debug_printf("BacklightControl::resetDimTimeout: wake up\n");
#endif

        set_brightness(BacklightControl::normal_brightness);
        woken_up = true;
    }

    if (BacklightControl::dim_timeout == 0)
        BacklightControl::dim_timeout_timestamp = 0;
    else
        BacklightControl::dim_timeout_timestamp = millis() + BacklightControl::dim_timeout * 1000;

    return woken_up;
}

void BacklightControl::setup(uint8_t pin)
{
    ledcSetup(BACKLIGHT_CONTROL_PWM_CHANNEL, 5000, 10);
    ledcAttachPin(pin, BACKLIGHT_CONTROL_PWM_CHANNEL);

    set_brightness(BacklightControl::normal_brightness);

    BacklightControl::resetDimTimeout();
}

void BacklightControl::loop()
{
    if ((BacklightControl::dim_timeout_timestamp != 0) && (BacklightControl::dim_timeout_timestamp < millis()) && (BacklightControl::current_brightness != BacklightControl::dim_brightness))
    {
#if DEBUG_BACKLIGHT_CONTROL
        debug_printf("BacklightControl::loop: activity timeout, dim display\n");
#endif
        set_brightness(BacklightControl::dim_brightness);
    }
}
