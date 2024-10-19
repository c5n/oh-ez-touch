#include "backlight_control.hpp"
#include "Arduino.h"

#ifndef DEBUG_BACKLIGHT_CONTROL
#define DEBUG_BACKLIGHT_CONTROL 0
#endif

void BacklightControl::set_brightness(uint8_t percent)
{
#if DEBUG_BACKLIGHT_CONTROL
    Serial.print("BacklightControl::set_brightness: ");
    Serial.println(percent);
#endif
    BacklightControl::current_brightness = percent;

    if (BacklightControl::led_invert == true)
    {
        percent = 100 - percent;
    }

    ledcWrite(BACKLIGHT_CONTROL_PWM_CHANNEL, map(percent, 0, 100, 1023, 0));
}

bool BacklightControl::resetDimTimeout()
{
    bool woken_up = false;

    if (BacklightControl::current_brightness != BacklightControl::normal_brightness)
    {
#if DEBUG_BACKLIGHT_CONTROL
        Serial.println("BacklightControl::resetDimTimeout: wake up");
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

void BacklightControl::setup(uint8_t pin, bool invert)
{
    ledcSetup(BACKLIGHT_CONTROL_PWM_CHANNEL, 30000, 10);
    ledcAttachPin(pin, BACKLIGHT_CONTROL_PWM_CHANNEL);

    BacklightControl::led_invert = invert;

    set_brightness(BacklightControl::normal_brightness);

    BacklightControl::resetDimTimeout();
}

void BacklightControl::loop()
{
    if ((BacklightControl::dim_timeout_timestamp != 0) && (BacklightControl::dim_timeout_timestamp < millis()) && (BacklightControl::current_brightness != BacklightControl::dim_brightness))
    {
#if DEBUG_BACKLIGHT_CONTROL
        Serial.println("BacklightControl::loop: activity timeout, dim display");
#endif
        set_brightness(BacklightControl::dim_brightness);
    }
}
