#ifndef BACKLIGHT_CONTROL_HPP
#define BACKLIGHT_CONTROL_HPP

#include "config.hpp"
#include "stdint.h"

#ifndef BACKLIGHT_CONTROL_PWM_CHANNEL
#define BACKLIGHT_CONTROL_PWM_CHANNEL 0
#endif

class BacklightControl
{
private:
    unsigned long dim_timeout_timestamp;
    unsigned long dim_timeout;
    uint8_t current_brightness;
    uint8_t normal_brightness;
    uint8_t dim_brightness;
    void set_brightness(uint8_t percent);

public:
    void setDimTimeout(unsigned long timeout) { dim_timeout = timeout; };
    void setNormalBrightness(uint8_t percent) { normal_brightness = percent; };
    void setDimBrightness(uint8_t percent) { dim_brightness = percent; };

    bool resetDimTimeout();

    void setup(uint8_t pin);
    void loop();
};

#endif