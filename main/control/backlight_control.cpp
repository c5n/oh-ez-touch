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

void BacklightControl::setNormalBrightness(uint8_t percent)
{
    BacklightControl::normal_brightness = percent;

    /* Only while the display is awake: changing the normal level must not
     * light up a panel that has dimmed itself. */
    if (   BacklightControl::ready == true
        && BacklightControl::dimmed == false
        && BacklightControl::current_brightness != percent)
        set_brightness(percent, WAKE_FADE_MS);
}

void BacklightControl::setDimBrightness(uint8_t percent)
{
    BacklightControl::dim_brightness = percent;

    /* The mirror of it: only while it is actually dimmed, so this never dims
     * a panel somebody is standing in front of. */
    if (   BacklightControl::ready == true
        && BacklightControl::dimmed == true
        && BacklightControl::current_brightness != percent)
        set_brightness(percent, DIM_FADE_MS);
}

bool BacklightControl::resetDimTimeout()
{
    bool woken_up = false;

    BacklightControl::dimmed = false;

    if (BacklightControl::current_brightness != BacklightControl::normal_brightness)
    {
#if CONFIG_OHEZ_DEBUG_BACKLIGHT_CONTROL
        printf("BacklightControl::resetDimTimeout: wake up\r\n");
#endif

        set_brightness(BacklightControl::normal_brightness, WAKE_FADE_MS);
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

    BacklightControl::ready = true;
    BacklightControl::dimmed = false;

    /* No ramp here: this is the first time the panel is lit at all, and there
     * is nothing to ramp away from. */
    set_brightness(BacklightControl::normal_brightness, 0);

    BacklightControl::resetDimTimeout();
}

void BacklightControl::loop()
{
    if (   (BacklightControl::dim_timeout_timestamp != 0)
        && (port_millis() >= BacklightControl::dim_timeout_timestamp)
        && (BacklightControl::current_brightness != BacklightControl::dim_brightness))
    {
#if CONFIG_OHEZ_DEBUG_BACKLIGHT_CONTROL
        printf("BacklightControl::loop: activity timeout, dim display\r\n");
#endif
        set_brightness(BacklightControl::dim_brightness, DIM_FADE_MS);
    }

    /* Outside the guard above, which also tests the brightness: with the two
     * levels set equal there is nothing to fade, but the display has still
     * reached the point where the dim level is the one in effect. */
    if (   BacklightControl::dim_timeout_timestamp != 0
        && port_millis() >= BacklightControl::dim_timeout_timestamp)
        BacklightControl::dimmed = true;
}
