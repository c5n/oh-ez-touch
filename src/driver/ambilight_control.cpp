#include "ambilight_control.hpp"
#include "Arduino.h"

#ifndef DEBUG_AMBILIGHT_CONTROL
#define DEBUG_AMBILIGHT_CONTROL 1
#endif

#define PWM_FREQUENCY_HZ 30000
#define PWM_RESOLUTION_BITS 10

#define VALUE2PWMFACTOR (4)       // Factor = 2^PWM_RESOLUTION_BITS / 2^COLORDEPTH ; COLORDEPTH=8

void AmbilightControl::set_brightness(uint8_t percent)
{
#if DEBUG_AMBILIGHT_CONTROL
    Serial.print("AmbilightControl::set_brightness: ");
    Serial.println(percent);
#endif
    AmbilightControl::current_brightness = percent;

    if (AmbilightControl::led_invert == true)
    {
        percent = 100 - percent;
    }

    uint32_t red_pwm = AmbilightControl::color.red * VALUE2PWMFACTOR * percent / 100;
    uint32_t green_pwm = AmbilightControl::color.green * VALUE2PWMFACTOR * percent / 100;
    uint32_t blue_pwm = AmbilightControl::color.blue * VALUE2PWMFACTOR * percent / 100;

    ledcWrite(AMBILIGHT_CONTROL_RED_PWM_CHANNEL, red_pwm);
    ledcWrite(AMBILIGHT_CONTROL_GREEN_PWM_CHANNEL, green_pwm);
    ledcWrite(AMBILIGHT_CONTROL_BLUE_PWM_CHANNEL, blue_pwm);
}

bool AmbilightControl::resetDimTimeout()
{
    bool woken_up = false;

    if (AmbilightControl::current_brightness != AmbilightControl::normal_brightness)
    {
#if DEBUG_AMBILIGHT_CONTROL
        Serial.println("AmbilightControl::resetDimTimeout: wake up");
#endif

        set_brightness(AmbilightControl::normal_brightness);
        woken_up = true;
    }

    if (AmbilightControl::dim_timeout == 0)
        AmbilightControl::dim_timeout_timestamp = 0;
    else
        AmbilightControl::dim_timeout_timestamp = millis() + AmbilightControl::dim_timeout * 1000;

    return woken_up;
}

void AmbilightControl::setup(uint8_t pin_red, uint8_t pin_green, uint8_t pin_blue, bool invert)
{
    ledcSetup(AMBILIGHT_CONTROL_RED_PWM_CHANNEL, PWM_FREQUENCY_HZ, PWM_RESOLUTION_BITS);
    ledcAttachPin(pin_red, AMBILIGHT_CONTROL_RED_PWM_CHANNEL);
    ledcSetup(AMBILIGHT_CONTROL_GREEN_PWM_CHANNEL, PWM_FREQUENCY_HZ, PWM_RESOLUTION_BITS);
    ledcAttachPin(pin_green, AMBILIGHT_CONTROL_GREEN_PWM_CHANNEL);
    ledcSetup(AMBILIGHT_CONTROL_BLUE_PWM_CHANNEL, PWM_FREQUENCY_HZ, PWM_RESOLUTION_BITS);
    ledcAttachPin(pin_blue, AMBILIGHT_CONTROL_BLUE_PWM_CHANNEL);

    AmbilightControl::led_invert = invert;

    set_brightness(AmbilightControl::normal_brightness);

    AmbilightControl::resetDimTimeout();
}

void AmbilightControl::loop()
{
    if ((AmbilightControl::dim_timeout_timestamp != 0) && (AmbilightControl::dim_timeout_timestamp < millis()) && (AmbilightControl::current_brightness != AmbilightControl::dim_brightness))
    {
#if DEBUG_AMBILIGHT_CONTROL
        Serial.println("AmbilightControl::loop: activity timeout, dim display");
#endif
        set_brightness(AmbilightControl::dim_brightness);
    }
}
