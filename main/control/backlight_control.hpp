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
public:
    /* Where the display is between awake and dimmed.
     *
     * Without the blank transition only the two ends are used, and the dim
     * and the wake are single fades the way they have always been. With it,
     * each way through goes via black, so that the screen the clock module
     * swaps in or out appears with the light rather than under it:
     *
     *   AWAKE -> SLEEP_OUT -> SLEEP_DARK -> SLEEP_IN -> ASLEEP
     *   ASLEEP -> WAKE_OUT -> WAKE_DARK -> WAKE_IN -> AWAKE
     *
     * isDimmed() changes in the two DARK phases and nowhere else, which is
     * what the clock screen's own state machine keys on. */
    enum phase_e
    {
        PHASE_AWAKE = 0,
        PHASE_SLEEP_OUT,
        PHASE_SLEEP_DARK,
        PHASE_SLEEP_IN,
        PHASE_ASLEEP,
        PHASE_WAKE_OUT,
        PHASE_WAKE_DARK,
        PHASE_WAKE_IN
    };

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

    /* True from the moment the timeout fires until the next wake: the start
     * of the fade, not its end. Distinct from `dimmed`, which the blank
     * transition holds back until the screen is dark. */
    bool inactive = false;

    bool blank_transition = false;
    enum phase_e phase = PHASE_AWAKE;
    /* When the phase in progress is over: the LEDC fade is fire-and-forget,
     * so its end is a deadline kept here rather than an event. */
    uint64_t phase_end = 0;

    void phase_start(enum phase_e next, uint8_t percent, uint16_t fade_ms);

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

    /* Go through black on the way to the dim level and back, rather than
     * fading straight between the two. main.cpp sets it to whether the clock
     * screen is on: it is the screen swap that the dark is there to hide. A
     * change takes effect at the next dim or wake. */
    void setBlankTransition(bool enabled) { blank_transition = enabled; }

    void setup();
    void loop();

    /* What the backlight is doing, for the simulator's control interface. Both
     * are already tracked; neither had a reader outside this class, which is
     * why "is the panel dimmed?" was not a question anything could ask. */
    uint8_t currentBrightness() const { return current_brightness; }
    bool isDimmed() const { return dimmed; }
    bool isInactive() const { return inactive; }
    enum phase_e currentPhase() const { return phase; }
    static const char *phaseName(enum phase_e phase);
};

#endif
