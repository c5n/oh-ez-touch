/**
 * @file ui_beep.cpp
 *
 * Playing a themed sound: the mute gate, the layering backstop, and the press
 * hook every pressable object goes through.
 *
 * The tables themselves are in ui_beep_tables.cpp, which includes no LVGL so
 * that the host tests can link them. The policy this file enforces is written
 * out in ui_beep.hpp, and it is worth reading before touching either.
 */
#include "ui_beep.hpp"

#include "ui_style.hpp"

#include <lvgl.h>

/* The backstop under the layering policy.
 *
 * Contact ticks are deliberately allowed to overlap outcome chimes in *time*
 * -- that is what makes "tick ... chime" read as one gesture rather than as
 * two events. What must not happen is a tick landing inside a chime, which is
 * the one arrangement that sounds like a mistake, and it is also the only one
 * the call sites cannot rule out by themselves: a finger dragging a settings
 * list presses a row every few hundred milliseconds whatever the handlers do.
 *
 * So a tick is dropped if another was played very recently, or if there is
 * still a meaningful amount of audio queued ahead of it. Outcome sounds are
 * never dropped here -- they are the half of the pair that carries meaning,
 * and the queue's own four-deep limit is the only thing allowed to lose one. */
#define UI_BEEP_TICK_MIN_GAP_MS 100
#define UI_BEEP_TICK_DROP_MS    40

/* Starts false. Everything raised while the UI is still being built -- the
 * "Connecting..." banner among them -- would otherwise announce itself to a
 * room before the panel is ready to be looked at. main.cpp turns this on at
 * the end of ohez_setup(), immediately before the boot chime. */
static bool enabled;

/* When the queue should next be empty, and when the last tick-class sound
 * went out. Both lv_tick milliseconds, both only ever touched from the LVGL
 * task -- ui_beep_play() has no other caller. */
static uint32_t busy_until;
static uint32_t last_tick;

static bool is_tick_class(enum ui_sound_e sound)
{
    return sound == UI_SOUND_PRESS || sound == UI_SOUND_TICK ||
           sound == UI_SOUND_TICK_BACK;
}

void ui_beep_play(enum ui_sound_e sound)
{
    if (enabled == false)
        return;

    if ((unsigned)sound >= UI_SOUND_COUNT)
        return;

    const struct ui_sound_s *set = ui_style_theme()->sound;

    if (set == NULL)
        return;

    const struct beeper_chime_s *chime = &set->chime[sound];

    uint32_t now     = lv_tick_get();
    uint32_t pending = (busy_until > now) ? (busy_until - now) : 0;

    if (is_tick_class(sound) == true)
    {
        if (lv_tick_elaps(last_tick) < UI_BEEP_TICK_MIN_GAP_MS)
            return;

        if (pending > UI_BEEP_TICK_DROP_MS)
            return;

        last_tick = now;
    }

    beeper_play(chime);

    /* An estimate, and deliberately one that errs high: the queue plays chimes
     * one after another, so what is outstanding is whatever was outstanding
     * plus this. Erring high only makes the tick guard keener, which is the
     * safe direction. */
    busy_until = now + pending + beeper_chime_duration_ms(chime);
}

void ui_beep_set_enabled(bool en)
{
    enabled = en;
}

static void press_event(lv_event_t *e)
{
    LV_UNUSED(e);

    BEEPER_EVENT_PRESS();
}

void ui_beep_attach_press(lv_obj_t *obj)
{
    if (obj == NULL)
        return;

    lv_obj_add_event_cb(obj, press_event, LV_EVENT_PRESSED, NULL);
}
