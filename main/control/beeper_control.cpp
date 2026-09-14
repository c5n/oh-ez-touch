#include "beeper_control.hpp"

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
 * At equal priority the two round-robin at the tick, so a slot that should be
 * two milliseconds could be one or three depending on where the renderer was
 * -- and a chord whose slots wobble like that has a tremolo on it. One level
 * up lets the tick that ends a slot preempt the blend loop immediately.
 *
 * The cost is about nineteen microseconds a slot (ledc_set_freq is the
 * expensive part, at roughly ten), so under one per cent of a core, and only
 * while a chord is actually sounding. That is a different thing from what
 * d779b8b took back from the renderer, which was steady-state work happening
 * whether or not anything was going on. */
#define BEEPER_TASK_PRIORITY 2

static QueueHandle_t xRequestQueue = NULL;

/* Written from the LVGL task and from the web server's, read by beeper_task.
 * Single aligned bytes on Xtensa, so there is nothing to tear; volatile is
 * here to stop the compiler hoisting the enabled read out of the frame loop,
 * which is the whole point of rechecking it there. */
static volatile bool    beeper_enabled;
static volatile uint8_t beeper_master = 25;

static void beeper_task(void *parameter);

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

/* Walk the mixer's frames, programming each voice's slot in turn.
 *
 * The chime clock is recomputed from the real tick count at every frame rather
 * than accumulated, so a frame that was preempted shortens the note it was in
 * instead of stretching the whole chime. */
static void beeper_play_chime(const struct beeper_chime_s *chime)
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
         * it. */
        if (beeper_enabled == false)
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

void beeper_play(const struct beeper_chime_s *chime)
{
    /* The live check, and the only one in the firmware. Unchecking "Enable
     * beeper" used to silence nothing but the wake blip -- that call site
     * tested the config itself and no other one did, so main.cpp's comment
     * claiming the setting applied live was true of one beep out of thirty. */
    if (beeper_enabled == false || xRequestQueue == NULL || chime == NULL ||
        chime->voices == NULL || chime->count == 0)
        return;

#if CONFIG_OHEZ_DEBUG_BEEPER_CONTROL
    printf("beeper_play: %u voices, %ums\r\n", (unsigned)chime->count,
           (unsigned)beeper_chime_duration_ms(chime));
#endif

    /* By value: eight bytes, and nothing in it can be truncated by a queue
     * that is four deep the way a note-at-a-time sequence could be. */
    xQueueSend(xRequestQueue, chime, 0);
}

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
    if (enabled == true && xRequestQueue == NULL)
    {
        xRequestQueue = xQueueCreate(BEEPER_CONTROL_QUEUE_LENGTH,
                                     sizeof(struct beeper_chime_s));

        if (xRequestQueue == NULL)
        {
            ESP_LOGE("beeper", "beeper_set_enabled: Failed to create the queue");
            return;
        }

        xTaskCreate(beeper_task, "beeper_task", BEEPER_TASK_STACK_SIZE, NULL,
                    BEEPER_TASK_PRIORITY, NULL);
    }

    beeper_enabled = enabled;

    /* Whatever was waiting was queued while the sound was still on. A chime
     * already dequeued keeps playing until the task's next frame boundary,
     * which is at most one step. */
    if (enabled == false && xRequestQueue != NULL)
        xQueueReset(xRequestQueue);
}

static void beeper_task(void *parameter)
{
    (void)parameter;

    while (true)
    {
        struct beeper_chime_s chime;

        /* Blocks until there is a chime. This used to poll with a 1 ms
         * timeout, which on the host target rounds to no wait at all -- the
         * tick is 4 ms -- and would have spun a core for nothing. */
        if (xQueueReceive(xRequestQueue, &chime, portMAX_DELAY) == pdTRUE)
        {
            /* The simulator would rather render the whole thing at audio
             * resolution than be handed one slot every four milliseconds. It
             * returns immediately, so the wait is here: both targets have to
             * serialise chimes the same way, and on the device that falls out
             * of walking the frames. */
            if (port_beeper_render(&chime, beeper_master) == true)
                vTaskDelay(pdMS_TO_TICKS(beeper_chime_duration_ms(&chime)));
            else
                beeper_play_chime(&chime);

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
