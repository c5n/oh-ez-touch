#include "beeper_control.hpp"

#include "beeper_song.h"
#include "debug.h"

#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "esp_log.h"

#include "port/port_beeper.h"

#define BEEPER_TASK_STACK_SIZE 2048

/* One above the IDF main task, which is where ohez_loop() and therefore all of
 * LVGL runs.
 *
 * This is the mixer's argument, and it is worth knowing that it is: at equal
 * priority the two round-robin at the tick, so a slot that should be two
 * milliseconds could be one or three depending on where the renderer was --
 * and a chord whose slots wobble like that has a tremolo on it. One level up
 * lets the tick that ends a slot preempt the blend loop immediately.
 *
 * The cost is about nineteen microseconds a slot (ledc_set_freq is the
 * expensive part, at roughly ten), so under one per cent of a core, and only
 * while a chord is actually sounding. That is a different thing from what
 * d779b8b took back from the renderer, which was steady-state work happening
 * whether or not anything was going on.
 *
 * The sequencer does not need the level: its step is five milliseconds, not
 * two, and a step that arrives a tick late shortens the note it was in rather
 * than unbalancing a chord. It is left here anyway, because two hundred
 * preemptions a second is fewer than the five hundred this was written for --
 * lowering it would be a change to argue on its own terms, and one nothing has
 * asked for. */
#define BEEPER_TASK_PRIORITY 2

/* What the queue carries. Eight bytes either way, which is what lets the
 * four-deep by-value send be the same on both engines. */
#if CONFIG_OHEZ_BEEPER_ENGINE_SEQ
typedef struct beeper_seq_s beeper_item_t;
#else
typedef struct beeper_chime_s beeper_item_t;
#endif

static QueueHandle_t xRequestQueue = NULL;

/* Written from the LVGL task and from the web server's, read by beeper_task.
 * Single aligned bytes on Xtensa, so there is nothing to tear; volatile is
 * here to stop the compiler hoisting the enabled read out of the frame loop,
 * which is the whole point of rechecking it there. */
static volatile bool    beeper_enabled;
static volatile uint8_t beeper_master = 25;

/* The one shot beeper_force_next() arms. The claim is taken by the next
 * successful send -- a send that fails leaves it for the one after -- and
 * then travels through the queue as a count, because the item itself has no
 * room for a flag: eight bytes by value, and the FIFO order of the sends is
 * the order of the dequeues, so count and item cannot be mismatched. Both are
 * written from the LVGL task (the only place a forced chime is queued from);
 * the task only ever decrements. */
static volatile bool     beeper_force_claim;
static volatile unsigned beeper_forced_pending;

void beeper_force_next(void)
{
    beeper_force_claim = true;
}

/* Bumped by beeper_stop(), snapshotted by the task when it takes an item off
 * the queue. Anything sounding is abandoned the moment the two differ.
 *
 * A counter rather than a "cancel" flag, and the difference is a race the flag
 * loses: a stop that arrives while the queue is empty and the task is blocked
 * in xQueueReceive() would leave a flag set with nothing to apply it to, and
 * the next sound queued -- seconds later, by something unrelated -- would be
 * cancelled before it started. Nothing has to clear a generation.
 *
 * Written from the LVGL task and read by the beeper task. A 32-bit aligned
 * load and store on both targets, so there is nothing to tear; volatile stops
 * the walk's read being hoisted out of the loop it exists to break. */
static volatile uint32_t beeper_generation;

static void beeper_task(void *parameter);

/* The queue and the task, brought up on first use: the first time the beeper
 * is switched on, or the first forced chime on a panel that never was.
 * Creating them lazily is about not paying the task's two kilobytes on a
 * muted panel, and a locate chime is the one request that pays them anyway. */
static bool beeper_ensure_task(void)
{
    if (xRequestQueue != NULL)
        return true;

    xRequestQueue = xQueueCreate(BEEPER_CONTROL_QUEUE_LENGTH,
                                 sizeof(beeper_item_t));

    if (xRequestQueue == NULL)
        return false;

    xTaskCreate(beeper_task, "beeper_task", BEEPER_TASK_STACK_SIZE, NULL,
                BEEPER_TASK_PRIORITY, NULL);

    return true;
}

/* pdMS_TO_TICKS() rounds down, and the simulator's tick is four milliseconds,
 * so a two-millisecond slot rounds to zero there -- and xTaskDelayUntil() with
 * a zero increment does not wait at all, which would spin this task flat out
 * for the length of the chime. One tick is the floor. */
static void beeper_delay(TickType_t *last_wake, uint16_t ms)
{
    TickType_t ticks = pdMS_TO_TICKS(ms);

    if (ticks == 0)
        ticks = 1;

    /* pdFALSE means the deadline was already behind us: the frame overran, and
     * chasing it would make every following frame return immediately and burst
     * the rest of the chime through at full speed. Resync instead. */
    if (xTaskDelayUntil(last_wake, ticks) == pdFALSE)
        *last_wake = xTaskGetTickCount();
}

#if CONFIG_OHEZ_BEEPER_ENGINE_SEQ

/* Walk the sequencer's frames: one tone per step, and no slot loop.
 *
 * The tune clock is recomputed from the real tick count at every frame rather
 * than accumulated, so a frame that was preempted shortens the note it was in
 * instead of stretching the whole tune -- which for a vibrato would also slide
 * its phase. */
static void beeper_walk(const struct beeper_seq_s *seq, uint32_t generation,
                        bool forced)
{
    TickType_t started   = xTaskGetTickCount();
    TickType_t last_wake = started;
    uint16_t   last_freq = 0;

    for (;;)
    {
        struct beeper_slot_s slot;

        /* Rechecked here, not only at the queue, so that unchecking "Enable
         * beeper" stops the note that is sounding rather than the one after
         * it. beeper_stop() is the same idea without the setting attached. A
         * forced item answers to neither -- it was asked for precisely
         * because the setting is unknown. */
        if ((beeper_enabled == false && forced == false)
            || beeper_generation != generation)
            break;

        uint32_t t_ms = (uint32_t)(xTaskGetTickCount() - started) * portTICK_PERIOD_MS;

        uint16_t frame = beeper_seq_frame(seq, t_ms, beeper_master, &slot);

        if (frame == 0)
            break;

        /* A level of zero is a rest rather than the end -- the tune is inside
         * a pause. Keeping the last frequency programmed is what the mixer
         * does too: the LEDC divider is left where it was and only the duty
         * goes to nothing. */
        port_beeper_tone(slot.freq, slot.level);

        if (slot.freq != 0)
            last_freq = slot.freq;

        beeper_delay(&last_wake, frame);
    }

    port_beeper_tone(last_freq, 0);
}

#else

/* Walk the mixer's frames, programming each voice's slot in turn.
 *
 * The chime clock is recomputed from the real tick count at every frame rather
 * than accumulated, so a frame that was preempted shortens the note it was in
 * instead of stretching the whole chime. */
static void beeper_walk(const struct beeper_chime_s *chime, uint32_t generation,
                        bool forced)
{
    TickType_t started   = xTaskGetTickCount();
    TickType_t last_wake = started;
    uint16_t   last_freq = 0;

    for (;;)
    {
        struct beeper_slot_s slots[BEEPER_VOICES_MAX];
        uint8_t              count;

        /* Rechecked here, not only at the queue, so that unchecking "Enable
         * beeper" stops the note that is sounding rather than the one after
         * it. beeper_stop() is the same idea without the setting attached. A
         * forced item answers to neither -- it was asked for precisely
         * because the setting is unknown. */
        if ((beeper_enabled == false && forced == false)
            || beeper_generation != generation)
            break;

        uint32_t t_ms = (uint32_t)(xTaskGetTickCount() - started) * portTICK_PERIOD_MS;

        uint16_t frame = beeper_mixer_frame(chime, t_ms, beeper_master, slots, &count);

        if (frame == 0)
            break;

        if (count == 0)
        {
            /* Every voice is mid-pause. A rest, not the end. */
            port_beeper_tone(last_freq, 0);
            beeper_delay(&last_wake, frame);
            continue;
        }

        for (uint8_t i = 0; i < count; i++)
        {
            port_beeper_tone(slots[i].freq, slots[i].level);
            last_freq = slots[i].freq;

            beeper_delay(&last_wake, (count == 1) ? frame : BEEPER_SLOT_MS);
        }
    }

    port_beeper_tone(last_freq, 0);
}

#endif /* CONFIG_OHEZ_BEEPER_ENGINE_SEQ */

/* The two things the task needs that differ between the engines, named once so
 * that beeper_task() below has no preprocessor in it at all. */
#if CONFIG_OHEZ_BEEPER_ENGINE_SEQ

static bool beeper_render(const beeper_item_t *item, uint8_t master)
{
    return port_beeper_render_seq(item, master);
}

static uint32_t beeper_item_duration_ms(const beeper_item_t *item)
{
    return beeper_seq_duration_ms(item);
}

#else

static bool beeper_render(const beeper_item_t *item, uint8_t master)
{
    return port_beeper_render(item, master);
}

static uint32_t beeper_item_duration_ms(const beeper_item_t *item)
{
    return beeper_chime_duration_ms(item);
}

#endif

/* The live check, and the only one in the firmware. Unchecking "Enable beeper"
 * used to silence nothing but the wake blip -- that call site tested the config
 * itself and no other one did, so main.cpp's comment claiming the setting
 * applied live was true of one beep out of thirty.
 *
 * By value: eight bytes, and nothing in it can be truncated by a queue that is
 * four deep the way a note-at-a-time sequence could be. */
#if CONFIG_OHEZ_BEEPER_ENGINE_SEQ

void beeper_play_seq(const struct beeper_seq_s *seq)
{
    if (seq == NULL || seq->notes == NULL || seq->count == 0)
        return;

    /* Taken, not peeked at: the claim belongs to the item this call queues,
     * and a send that never happens must not spend it. */
    bool forced = beeper_force_claim;

    if ((beeper_enabled == false && forced == false)
        || beeper_ensure_task() == false)
        return;

#if CONFIG_OHEZ_DEBUG_BEEPER_CONTROL
    printf("beeper_play_seq: %u notes, %ums\r\n",
           (unsigned)beeper_seq_note_count(seq),
           (unsigned)beeper_seq_duration_ms(seq));
#endif

    if (xQueueSend(xRequestQueue, seq, 0) != pdTRUE)
        return;

    if (forced == true)
    {
        beeper_force_claim = false;
        beeper_forced_pending = beeper_forced_pending + 1;
    }
}

#else

void beeper_play(const struct beeper_chime_s *chime)
{
    if (chime == NULL || chime->voices == NULL || chime->count == 0)
        return;

    /* Taken, not peeked at: the claim belongs to the item this call queues,
     * and a send that never happens must not spend it. */
    bool forced = beeper_force_claim;

    if ((beeper_enabled == false && forced == false)
        || beeper_ensure_task() == false)
        return;

#if CONFIG_OHEZ_DEBUG_BEEPER_CONTROL
    printf("beeper_play: %u voices, %ums\r\n", (unsigned)chime->count,
           (unsigned)beeper_chime_duration_ms(chime));
#endif

    if (xQueueSend(xRequestQueue, chime, 0) != pdTRUE)
        return;

    if (forced == true)
    {
        beeper_force_claim = false;
        beeper_forced_pending = beeper_forced_pending + 1;
    }
}

#endif

void beeper_setup(void)
{
    port_beeper_init();
}

void beeper_set_volume(uint8_t percent)
{
    beeper_master = (percent > 100) ? 100 : percent;
}

void beeper_set_enabled(bool enabled)
{
    if (enabled == true && xRequestQueue == NULL
        && beeper_ensure_task() == false)
    {
        ESP_LOGE("beeper", "beeper_set_enabled: Failed to create the queue");
        return;
    }

    beeper_enabled = enabled;

    /* Whatever was waiting was queued while the sound was still on. A chime
     * already dequeued keeps playing until the task's next frame boundary,
     * which is at most one step. The forced count goes with the queue: an
     * item that is gone has no force left to claim. */
    if (enabled == false && xRequestQueue != NULL)
    {
        xQueueReset(xRequestQueue);
        beeper_forced_pending = 0;
    }
}

void beeper_stop(void)
{
    /* The order matters, and it is: stop new work reaching the task, then
     * cancel what it already has, then silence the hardware.
     *
     * Bumping the generation first would leave a window in which the task
     * finishes early, takes the *next* queued item and starts it at the new
     * generation -- which would survive the reset that was meant to clear it.
     * Resetting first cannot go wrong the other way: an item dequeued between
     * the two lines carries the old generation and is cancelled by the bump. */
    if (xRequestQueue != NULL)
    {
        xQueueReset(xRequestQueue);
        beeper_forced_pending = 0;
    }

    /* Spelled out rather than ++, which C++20 deprecates on a volatile -- and
     * the deprecation has a point worth answering: a read-modify-write is not
     * atomic. It is safe here because there is exactly one writer. beeper_stop()
     * is reachable only from the LVGL task, and the beeper task never does
     * anything but compare. */
    beeper_generation = beeper_generation + 1;

    /* And the half of it the task cannot do: on a target that renders a tune
     * whole, the task's grip on it is a delay rather than a loop, so the sound
     * would keep coming out of the audio device until its natural end. */
    port_beeper_stop();
}

/* ------------------------------------------------------ the demonstration */

/* When the tune is due to end, in ticks, and whether one was started at all.
 *
 * A deadline rather than a flag the task clears, and the reason is that the
 * task would have to recognise the tune to clear it -- comparing the item it
 * just finished against beeper_song()'s pointer, which is a coupling the task
 * has no other use for. The tune is one queue item and nothing preempts it, so
 * its end is known the moment it starts, to within a frame.
 *
 * Only ever touched from the LVGL task: started from a button, polled from
 * ui_settings_loop() and ui_beep_play(). The beeper task does not read it. */
#if CONFIG_OHEZ_BEEPER_ENGINE_SEQ

static bool       demo_active;
static TickType_t demo_ends;

bool beeper_demo_available(void)
{
    return true;
}

bool beeper_demo_start(void)
{
    const struct beeper_seq_s *song = beeper_song();
    uint32_t                   ms   = beeper_seq_duration_ms(song);

    /* Asked before anything is queued, because beeper_play_seq() is silently a
     * no-op when the beeper is off and the caller would otherwise be told a
     * tune was playing that nobody can hear. */
    if (beeper_enabled == false || ms == 0)
        return false;

    /* Whatever is sounding gets out of the way: the tune starts from its first
     * note rather than behind a press tick. */
    beeper_stop();

    beeper_play_seq(song);

    demo_ends   = xTaskGetTickCount() + pdMS_TO_TICKS(ms);
    demo_active = true;

    return true;
}

void beeper_demo_stop(void)
{
    if (demo_active == false)
        return;

    demo_active = false;

    beeper_stop();
}

bool beeper_demo_playing(void)
{
    if (demo_active == false)
        return false;

    /* Signed difference, so this stays right across the tick counter's wrap --
     * which on a 32-bit tick at 1 kHz is once every seven weeks, and is exactly
     * the kind of thing that would be found by somebody's panel rather than by
     * anybody's test. */
    if ((int32_t)(xTaskGetTickCount() - demo_ends) >= 0)
        demo_active = false;

    return demo_active;
}

#else

/* No tune under the polyphonic engine -- see beeper_song.h for why that is a
 * decision rather than an omission. Stubs rather than an #if at the call site,
 * so ui_settings.cpp stays free of the preprocessor. */

bool beeper_demo_available(void) { return false; }
bool beeper_demo_start(void) { return false; }
void beeper_demo_stop(void) {}
bool beeper_demo_playing(void) { return false; }

#endif /* CONFIG_OHEZ_BEEPER_ENGINE_SEQ */

/* Wait out a tune the port layer is rendering for us, and come back early if
 * it was cancelled.
 *
 * In slices rather than in one vTaskDelay(), which is what this used to be and
 * was fine while the longest sound was half a second. port_beeper_stop() takes
 * the tune away from the audio callback immediately, so without this the panel
 * would fall silent at once and then refuse to make another sound until the
 * cancelled one's nominal end -- half a minute, for the demonstration tune,
 * with the task asleep the whole time and every press queued behind it.
 *
 * Twenty milliseconds is under a frame of the display and a hundred times the
 * tune's own resolution, so the slicing costs fifty wakeups a second while a
 * sound is playing and nothing at all when one is not. */
#define BEEPER_WAIT_SLICE_MS 20

static void beeper_render_wait(uint32_t ms, uint32_t generation, bool forced)
{
    while (ms > 0)
    {
        uint32_t slice = (ms > BEEPER_WAIT_SLICE_MS) ? BEEPER_WAIT_SLICE_MS : ms;

        if ((beeper_enabled == false && forced == false)
            || beeper_generation != generation)
            return;

        vTaskDelay(pdMS_TO_TICKS(slice));

        ms -= slice;
    }
}

static void beeper_task(void *parameter)
{
    (void)parameter;

    while (true)
    {
        beeper_item_t item;

        /* Blocks until there is a chime. This used to poll with a 1 ms
         * timeout, which on the host target rounds to no wait at all -- the
         * tick is 4 ms -- and would have spun a core for nothing. */
        if (xQueueReceive(xRequestQueue, &item, portMAX_DELAY) == pdTRUE)
        {
            /* Snapshotted before a note sounds, so that a stop racing this
             * dequeue cancels the item it was aimed at rather than the one
             * after it. */
            uint32_t generation = beeper_generation;

            /* The force arrives the same way the item did: the send that
             * armed it and this dequeue see the same FIFO order, so the count
             * cannot stick to the wrong chime. */
            bool forced = (beeper_forced_pending > 0);

            if (forced == true)
                beeper_forced_pending = beeper_forced_pending - 1;

            /* The simulator would rather render the whole thing at audio
             * resolution than be handed one slot every four milliseconds. It
             * returns immediately, so the wait is here: both targets have to
             * serialise chimes the same way, and on the device that falls out
             * of walking the frames. */
            if (beeper_render(&item, beeper_master) == true)
                beeper_render_wait(beeper_item_duration_ms(&item), generation,
                                   forced);
            else
                beeper_walk(&item, generation, forced);

#if CONFIG_OHEZ_DEBUG_BEEPER_CONTROL
            static UBaseType_t stack_free = 0;
            UBaseType_t stack_free_new = uxTaskGetStackHighWaterMark(NULL);

            if (stack_free_new != stack_free)
            {
                stack_free = stack_free_new;
                printf("beeper_task: stack_free=%u\r\n", (unsigned)stack_free);
            }
#endif
        }
    }
}
