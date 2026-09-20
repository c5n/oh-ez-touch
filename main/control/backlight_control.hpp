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

    /* True once setup() has run. The two setters above are called from
     * settings_apply_live(), which runs before setup() on the boot path, and
     * driving the pin before port_backlight_init() is not something to ask
     * of a board. */
    bool ready = false;

    /* Which of the two levels the display is meant to be showing.
     *
     * Tracked rather than derived from current_brightness, because the two
     * levels may be equal -- a panel configured to dim to 100% is a perfectly
     * ordinary way of saying "never dim visibly" -- and then comparing
     * brightnesses cannot tell the states apart. Only the setters use it;
     * resetDimTimeout() still answers "did this wake the display" by
     * comparing brightness, so a tap on a panel where the two levels are
     * equal is not swallowed for a change nobody can see. */
    bool dimmed = false;

public:
    void setDimTimeout(unsigned long timeout) { dim_timeout = timeout; };

    /* Both of these take effect at once where the display is already showing
     * that level, rather than at the next dim or wake. They are offered as
     * live settings by both front ends -- settings_apply_live() calls them on
     * every save -- and until this they were not: a panel sat at its old
     * brightness until something happened to move it, which for the normal
     * level means until the dim timeout expired and a finger woke it again.
     * Sliding a brightness and seeing nothing change reads as a broken
     * setting. */
    void setNormalBrightness(uint8_t percent);
    void setDimBrightness(uint8_t percent);

    /* Wake the display and restart the timeout. True when this call was what
     * woke it, which the touch handler uses to swallow the tap. */
    bool resetDimTimeout();

    void setup();
    void loop();

    /* What the backlight is doing, for the simulator's control interface. Both
     * are already tracked; neither had a reader outside this class, which is
     * why "is the panel dimmed?" was not a question anything could ask. */
    uint8_t currentBrightness() const { return current_brightness; }
    bool isDimmed() const { return dimmed; }
};

#endif
