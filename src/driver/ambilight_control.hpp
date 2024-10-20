#ifndef AMBILIGHT_CONTROL_HPP
#define AMBILIGHT_CONTROL_HPP

#include "config.hpp"
#include "stdint.h"

#ifndef AMBILIGHT_CONTROL_RED_PWM_CHANNEL
#define AMBILIGHT_CONTROL_RED_PWM_CHANNEL 1
#endif
#ifndef AMBILIGHT_CONTROL_GREEN_PWM_CHANNEL
#define AMBILIGHT_CONTROL_GREEN_PWM_CHANNEL 2
#endif
#ifndef AMBILIGHT_CONTROL_BLUE_PWM_CHANNEL
#define AMBILIGHT_CONTROL_BLUE_PWM_CHANNEL 3
#endif

struct color_s
{
    uint8_t red;
    uint8_t green;
    uint8_t blue;
};

class AmbilightControl
{
private:
    unsigned long dim_timeout_timestamp;
    unsigned long dim_timeout;
    uint8_t current_brightness;
    uint8_t normal_brightness;
    uint8_t dim_brightness;
    bool led_invert;
    struct color_s color;

    void set_brightness(uint8_t percent);


public:
    void setColor(uint8_t red, uint8_t green, uint8_t blue) {color.red = red; color.green = green; color.blue = blue; };
    void setDimTimeout(unsigned long timeout) { dim_timeout = timeout; };
    void setNormalBrightness(uint8_t percent) { normal_brightness = percent; };
    void setDimBrightness(uint8_t percent) { dim_brightness = percent; };

    bool resetDimTimeout();

    void setup(uint8_t pin_red, uint8_t pin_green, uint8_t pin_blue, bool invert);
    void loop();
};

#endif