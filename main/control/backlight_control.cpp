#include "backlight_control.hpp"

#include "debug.h"

#include <stdio.h>

#include "port/port_backlight.h"
#include "port/port_sys.h"

/* How long the dim and the wake take.
 *
 * Asymmetric on purpose. Waking is an answer to a touch, so it has to feel
 * immediate -- long enough not to be a step, short enough that the panel is
 * lit by the time a finger has finished landing. Dimming is not an answer to
 * anything, so it can take its time and simply stop drawing attention. */
#define WAKE_FADE_MS 180
#define DIM_FADE_MS  600

/* The same two directions through black, for the blank transition. Going to
 * sleep it is slow on both sides of the dark: the panel is being left alone,
 * and a screen that changes slowly enough is one nobody notices changing. The
 * way back is a dip -- out in a blink, a moment to swap the screen, and in at
 * WAKE_FADE_MS -- about a third of a second end to end, which is still an
 * answer to the touch. The holds are the margin for the swap: ui_clock draws
 * the new screen synchronously, so they only have to outlast one loop. */
#define SLEEP_OUT_MS  1500
#define SLEEP_HOLD_MS 60
#define SLEEP_IN_MS   800
#define WAKE_OUT_MS   100
#define WAKE_HOLD_MS  40

void BacklightControl::set_brightness(uint8_t percent, uint16_t fade_ms)
{
#if CONFIG_OHEZ_DEBUG_BACKLIGHT_CONTROL
    printf("BacklightControl::set_brightness: %u\r\n", (unsigned)percent);
#endif
    BacklightControl::current_brightness = percent;

    /* Straight through: which way round the pin has to move to make the panel
     * brighter is the board's business, and lives in port_backlight. This used
     * to invert the percentage here and then map it to a descending duty, two
     * inversions that cancelled on one board and not on the other. */
    port_backlight_fade(percent, fade_ms);
}

void BacklightControl::phase_start(enum phase_e next, uint8_t percent, uint16_t fade_ms)
{
#if CONFIG_OHEZ_DEBUG_BACKLIGHT_CONTROL
    printf("BacklightControl: %s -> %s\r\n", phaseName(phase), phaseName(next));
#endif
    phase = next;
    phase_end = port_millis() + fade_ms;

    if (current_brightness != percent)
        set_brightness(percent, fade_ms);
}

const char *BacklightControl::phaseName(enum phase_e phase)
{
    switch (phase)
    {
    case PHASE_AWAKE:      return "awake";
    case PHASE_SLEEP_OUT:  return "sleep_out";
    case PHASE_SLEEP_DARK: return "sleep_dark";
    case PHASE_SLEEP_IN:   return "sleep_in";
    case PHASE_ASLEEP:     return "asleep";
    case PHASE_WAKE_OUT:   return "wake_out";
    case PHASE_WAKE_DARK:  return "wake_dark";
    case PHASE_WAKE_IN:    return "wake_in";
    }

    return "?";
}

void BacklightControl::setNormalBrightness(uint8_t percent)
{
    BacklightControl::normal_brightness = percent;

    /* Only while the display is awake: changing the normal level must not
     * light up a panel that has dimmed itself -- nor cut into a transition,
     * which ends at the level it reads when it gets there. */
    if (   BacklightControl::ready == true
        && BacklightControl::phase == PHASE_AWAKE
        && BacklightControl::current_brightness != percent)
        set_brightness(percent, WAKE_FADE_MS);
}

void BacklightControl::setDimBrightness(uint8_t percent)
{
    BacklightControl::dim_brightness = percent;

    /* The mirror of it: only while it is actually dimmed, so this never dims
     * a panel somebody is standing in front of. */
    if (   BacklightControl::ready == true
        && BacklightControl::phase == PHASE_ASLEEP
        && BacklightControl::current_brightness != percent)
        set_brightness(percent, DIM_FADE_MS);
}

bool BacklightControl::resetDimTimeout()
{
    bool woken_up = false;

    BacklightControl::inactive = false;

    switch (BacklightControl::phase)
    {
    case PHASE_AWAKE:
        /* Still answers "was it lit at the wrong level", as it always has:
         * a level that is not the normal one is a panel that needed this. */
        if (BacklightControl::current_brightness != BacklightControl::normal_brightness)
        {
            set_brightness(BacklightControl::normal_brightness, WAKE_FADE_MS);
            woken_up = true;
        }
        break;

    case PHASE_SLEEP_OUT:
        /* Caught on the way down, before anything was swapped: straight back
         * up, there is no screen to change. */
        phase_start(PHASE_AWAKE, BacklightControl::normal_brightness, WAKE_FADE_MS);
        woken_up = true;
        break;

    case PHASE_SLEEP_DARK:
    case PHASE_SLEEP_IN:
    case PHASE_ASLEEP:
        if (BacklightControl::blank_transition == true)
        {
#if CONFIG_OHEZ_DEBUG_BACKLIGHT_CONTROL
            printf("BacklightControl::resetDimTimeout: wake up via black\r\n");
#endif
            phase_start(PHASE_WAKE_OUT, 0, WAKE_OUT_MS);
            woken_up = true;
            break;
        }

        /* The plain wake, which is the old one. "Woken" still means the light
         * changed: with both levels equal the tap is not swallowed for a
         * change nobody can see. */
#if CONFIG_OHEZ_DEBUG_BACKLIGHT_CONTROL
        printf("BacklightControl::resetDimTimeout: wake up\r\n");
#endif
        BacklightControl::dimmed = false;
        woken_up = (BacklightControl::current_brightness != BacklightControl::normal_brightness);
        phase_start(PHASE_AWAKE, BacklightControl::normal_brightness, WAKE_FADE_MS);
        break;

    case PHASE_WAKE_OUT:
    case PHASE_WAKE_DARK:
    case PHASE_WAKE_IN:
        /* Already on the way: a second finger inside the third of a second
         * is part of the same wake, and swallowed with it. */
        woken_up = true;
        break;
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

    BacklightControl::ready = true;
    BacklightControl::dimmed = false;
    BacklightControl::inactive = false;
    BacklightControl::phase = PHASE_AWAKE;

    /* No ramp here: this is the first time the panel is lit at all, and there
     * is nothing to ramp away from. */
    set_brightness(BacklightControl::normal_brightness, 0);

    BacklightControl::resetDimTimeout();
}

void BacklightControl::loop()
{
    uint64_t now = port_millis();
    bool     phase_over = (now >= BacklightControl::phase_end);

    switch (BacklightControl::phase)
    {
    case PHASE_AWAKE:
        if (   BacklightControl::dim_timeout_timestamp == 0
            || now < BacklightControl::dim_timeout_timestamp)
            break;

        BacklightControl::inactive = true;

        if (BacklightControl::blank_transition == true)
        {
            phase_start(PHASE_SLEEP_OUT, 0, SLEEP_OUT_MS);
            break;
        }

#if CONFIG_OHEZ_DEBUG_BACKLIGHT_CONTROL
        printf("BacklightControl::loop: activity timeout, dim display\r\n");
#endif
        /* Dimmed even when the two levels are equal and there is nothing to
         * fade: the display has still reached the point where the dim level
         * is the one in effect. */
        BacklightControl::dimmed = true;
        phase_start(PHASE_ASLEEP, BacklightControl::dim_brightness, DIM_FADE_MS);
        break;

    case PHASE_SLEEP_OUT:
        if (phase_over == true)
        {
            /* Dark. This is the moment the clock screen goes up. */
            BacklightControl::dimmed = true;
            phase_start(PHASE_SLEEP_DARK, 0, SLEEP_HOLD_MS);
        }
        break;

    case PHASE_SLEEP_DARK:
        if (phase_over == true)
            phase_start(PHASE_SLEEP_IN, BacklightControl::dim_brightness, SLEEP_IN_MS);
        break;

    case PHASE_SLEEP_IN:
        if (phase_over == true)
            BacklightControl::phase = PHASE_ASLEEP;
        break;

    case PHASE_ASLEEP:
        break;

    case PHASE_WAKE_OUT:
        if (phase_over == true)
        {
            /* Dark again, and the clock screen comes down. */
            BacklightControl::dimmed = false;
            phase_start(PHASE_WAKE_DARK, 0, WAKE_HOLD_MS);
        }
        break;

    case PHASE_WAKE_DARK:
        if (phase_over == true)
            phase_start(PHASE_WAKE_IN, BacklightControl::normal_brightness, WAKE_FADE_MS);
        break;

    case PHASE_WAKE_IN:
        if (phase_over == true)
            BacklightControl::phase = PHASE_AWAKE;
        break;
    }
}
