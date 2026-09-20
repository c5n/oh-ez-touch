/**
 * @file ui_beep.cpp
 *
 * Playing a themed sound: the mute gate, the layering backstop, the press hook
 * every pressable object goes through, and the one topic a broker can ask for
 * a sound on.
 *
 * The tables themselves are in ui_beep_tables.cpp or ui_beep_tables_seq.cpp
 * depending on the engine, neither of which includes LVGL so that the host
 * tests can link both. The policy this file enforces is written
 * out in ui_beep.hpp, and it is worth reading before touching either.
 */
#include "ui_beep.hpp"

#include "mqtt/ohez_mqtt.hpp"
#include "ui_style.hpp"

#include <lvgl.h>

#include "esp_log.h"

static const char *TAG = "ui_beep";

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

/* Queue the sound, and say how long it will take.
 *
 * The one place in this file that knows which engine is compiled in.
 * Everything else here is policy -- the mute gate, the tick backstop, the press
 * hook -- and the policy is the same either way. */
#if CONFIG_OHEZ_BEEPER_ENGINE_SEQ

static uint32_t ui_beep_queue(const ui_sound_set_s *set, enum ui_sound_e sound)
{
    beeper_play_seq(&set->chime[sound]);

    return beeper_seq_duration_ms(&set->chime[sound]);
}

#else

static uint32_t ui_beep_queue(const ui_sound_set_s *set, enum ui_sound_e sound)
{
    beeper_play(&set->chime[sound]);

    return beeper_chime_duration_ms(&set->chime[sound]);
}

#endif

static void ui_beep_play_gated(enum ui_sound_e sound, bool forced)
{
    /* What a forced sound skips is the settings half of this flag -- the
     * half settings_apply_live() mirrors out of Config. The startup half is
     * not really skipped: before the theme is applied there is no sound set,
     * and the NULL check below is the guard for that, forced or not. */
    if (enabled == false && forced == false)
        return;

    /* The demonstration tune owns the piezo while it runs.
     *
     * Not politeness: the queue plays items one after another, so a press tick
     * raised during those thirty seconds would not interrupt the tune, it
     * would be stored and fired at the end of it -- and a finger that walked a
     * settings list meanwhile would empty four of them in a burst when the
     * music stopped. Dropping them is the only behaviour that is not
     * surprising. The one press worth hearing is the one that stops the tune,
     * and that is an outcome rather than a tick: by the time it sounds,
     * beeper_demo_stop() has already run and this gate has opened again. */
    if (beeper_demo_playing() == true)
        return;

    if ((unsigned)sound >= UI_SOUND_COUNT)
        return;

    const ui_sound_set_s *set = ui_style_theme()->sound;

    if (set == NULL)
        return;

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

    /* An estimate, and deliberately one that errs high: the queue plays chimes
     * one after another, so what is outstanding is whatever was outstanding
     * plus this. Erring high only makes the tick guard keener, which is the
     * safe direction. */
    if (forced == true)
        beeper_force_next();

    busy_until = now + pending + ui_beep_queue(set, sound);
}

void ui_beep_play(enum ui_sound_e sound)
{
    ui_beep_play_gated(sound, false);
}

void ui_beep_set_enabled(bool en)
{
    enabled = en;
}

/* ---------------------------------------------------------------- requests */

/* The slot ui_beep_request() writes and ui_beep_loop() drains -- one, not a
 * queue, for the reason the header gives. forced is written first and read
 * second: the sound is the guard both sides test, so a torn pair is not
 * possible. */
static volatile int  requested_sound = (int)UI_SOUND_COUNT;
static volatile bool requested_forced;

void ui_beep_request(enum ui_sound_e sound, bool forced)
{
    requested_forced = forced;
    requested_sound = (int)sound;
}

void ui_beep_loop(void)
{
    if (requested_sound == (int)UI_SOUND_COUNT)
        return;

    enum ui_sound_e sound  = (enum ui_sound_e)requested_sound;
    bool            forced = requested_forced;

    requested_sound = (int)UI_SOUND_COUNT;

    ui_beep_play_gated(sound, forced);
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

/* ------------------------------------------------------------------- MQTT */

/* `sound/set`: play the named sound from the theme in force.
 *
 * The payload is a name out of ui_sound_names[] -- "accept", "door_chime",
 * "error" -- rather than the topic naming the sound and the payload being
 * ignored. Three reasons, and the third is the one that decided it. An
 * installation wants one openHAB String item pointed at one topic, not
 * eighteen. A `<something>/set` whose payload does nothing is a shape nobody
 * else in this firmware has. And a name that is not in the vocabulary can be
 * *said so*, which a wildcard subtree cannot do: `sound/+/set` would match
 * `sound/dooor_chime/set` and the only symptom would be silence.
 *
 * An empty payload plays nothing and is not an error. That is what somebody
 * publishing a zero-length retained message to clear the topic sends, and
 * clearing a topic should not be the last thing it ever does.
 *
 * Runs on the application's task, from ohez_mqtt_loop(), which is the task
 * that owns LVGL -- so ui_beep_play()'s lv_tick_get() and its two statics are
 * reached from the one place they are reached from everywhere else. Both of
 * its gates still apply: a panel with the beeper switched off in the settings
 * stays silent, and so does one that has not finished starting up. */
static void sound_command(const char *topic, const char *value)
{
    LV_UNUSED(topic);

    enum ui_sound_e sound;

    if (value[0] == '\0')
        return;

    sound = ui_sound_from_name(value);

    if (sound == UI_SOUND_COUNT)
    {
        ESP_LOGW(TAG, "no sound called %s", value);
        return;
    }

    ui_beep_play(sound);
}

void ui_beep_mqtt_setup(void)
{
    /* _live rather than plain subscribe: a retained message on this topic is
     * the broker replaying an old request, and a panel that plays the last
     * sound it was asked for every time the broker restarts is one somebody
     * unplugs. See ohez_mqtt_subscribe_live() and the note in ui_beep.hpp. */
    ohez_mqtt_subscribe_live("sound/set", sound_command);
}
