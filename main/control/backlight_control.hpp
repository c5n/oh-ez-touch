#ifndef BACKLIGHT_CONTROL_HPP
#define BACKLIGHT_CONTROL_HPP

#include <stdint.h>

/* When to dim, and to what.
 *
 * Shared by both targets: the PWM behind it is port_backlight, which does
 * nothing in the simulator, but the timing decisions here are the part worth
 * running everywhere.
 */
class BacklightControl
{
private:
    uint64_t dim_timeout_timestamp;
    unsigned long dim_timeout;
    uint8_t current_brightness;
    uint8_t normal_brightness;
    uint8_t dim_brightness;

    /* fade_ms of 0 steps straight there, which is what the first call at boot
     * wants and what a board with no backlight gets regardless. */
    void set_brightness(uint8_t percent, uint16_t fade_ms);

public:
    void setDimTimeout(unsigned long timeout) { dim_timeout = timeout; };
    void setNormalBrightness(uint8_t percent) { normal_brightness = percent; };
    void setDimBrightness(uint8_t percent) { dim_brightness = percent; };

    /* Wake the display and restart the timeout. True when this call was what
     * woke it, which the touch handler uses to swallow the tap. */
    bool resetDimTimeout();

    void setup();
    void loop();
};

#endif
