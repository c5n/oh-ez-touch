#include "beeper_control.hpp"

#include "debug.h"

#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "esp_log.h"

#include "port/port_beeper.h"

#define BEEPER_TASK_STACK_SIZE  2048

/* The queue carries a whole chime, so a sequence cannot be truncated by a
 * queue that is four deep. The pointer is to flash in every real caller. */
struct request_s
{
    const struct beeper_note_s *notes;
    uint8_t                     count;
    struct beeper_note_s        inline_note; /* backing for beeper_playNote() */
};

/* How often the frequency and the duty are updated inside a note. Fine enough
 * that a sweep is heard as a slide rather than as steps, coarse enough that a
 * 200 ms chime is forty wakeups of a task that is otherwise asleep. */
#define STEP_MS 5

static QueueHandle_t xRequestQueue = NULL;

static void beeper_task(void *parameter);

/* The envelope, as a fraction of the peak in 0..255.
 *
 * Duty is amplitude on a piezo, so ramping it is the difference between a
 * square wave gated on and off -- which clicks at both ends and sounds like a
 * doorbell -- and something with an attack and a decay. */
static uint8_t envelope(uint8_t shape, uint32_t elapsed, uint32_t duration)
{
    if (duration == 0)
        return 255;

    uint32_t t = (elapsed * 255) / duration; /* 0..255 through the note */

    switch (shape)
    {
    case BEEPER_SHAPE_PLUCK:
        /* Straight up, then away: what a struck thing does. */
        return (uint8_t)(255 - t);

    case BEEPER_SHAPE_PAD:
        /* Up and down again, so neither end is a click. */
        return (uint8_t)((t < 128) ? (t * 2) : ((255 - t) * 2));

    case BEEPER_SHAPE_FLAT:
    default:
        return 255;
    }
}

/* One note: the pitch walked from f_start to f_end while the duty follows the
 * envelope, in STEP_MS increments. */
static void play_note(const struct beeper_note_s *note)
{
    uint32_t elapsed = 0;

    while (elapsed < note->duration_ms)
    {
        uint16_t freq = note->f_start;

        if (note->f_end != note->f_start)
        {
            int32_t span = (int32_t)note->f_end - (int32_t)note->f_start;

            freq = (uint16_t)((int32_t)note->f_start +
                              (span * (int32_t)elapsed) / (int32_t)note->duration_ms);
        }

        uint32_t level = envelope(note->shape, elapsed, note->duration_ms);

        port_beeper_tone(freq, (uint8_t)((note->volume * level) / 255));

        vTaskDelay(pdMS_TO_TICKS(STEP_MS));
        elapsed += STEP_MS;
    }

    port_beeper_tone(note->f_end, 0);

    if (note->pause_ms != 0)
        vTaskDelay(pdMS_TO_TICKS(note->pause_ms));
}

void beeper_play(const struct beeper_chime_s *chime)
{
    if (xRequestQueue == NULL || chime == NULL || chime->notes == NULL || chime->count == 0)
        return;

#if CONFIG_OHEZ_DEBUG_BEEPER_CONTROL
    printf("beeper_play: %u notes, first %u->%u Hz\r\n", (unsigned)chime->count,
           (unsigned)chime->notes[0].f_start, (unsigned)chime->notes[0].f_end);
#endif

    struct request_s request = {};

    request.notes = chime->notes;
    request.count = chime->count;

    /* Dropped rather than waited on: a missed blip is not worth blocking a
     * touch handler for, which is why this has always been a zero timeout. */
    xQueueSend(xRequestQueue, &request, 0);
}

void beeper_playNote(uint16_t note, uint8_t volume, uint16_t duration, uint16_t pause)
{
#if CONFIG_OHEZ_DEBUG_BEEPER_CONTROL
    printf("beeper_playNote: freq=%u Hz, duration=%u ms\r\n", note, duration);
#endif
    if (xRequestQueue == NULL)
        return;

    struct request_s request = {};

    /* Carried by value inside the request, because a caller passing loose
     * numbers has no table for the note to live in. */
    request.inline_note.f_start     = note;
    request.inline_note.f_end       = note;
    request.inline_note.duration_ms = duration;
    request.inline_note.pause_ms    = pause;
    request.inline_note.volume      = (volume > 100) ? 100 : volume;
    request.inline_note.shape       = BEEPER_SHAPE_FLAT;
    request.notes                   = NULL;
    request.count                   = 1;

    xQueueSend(xRequestQueue, &request, 0);
}

void beeper_setup(void)
{
    port_beeper_init();
}

void beeper_enable(void)
{
    /* Called from setup() and again from settings_apply_live() on every save.
     * Without this it created a second queue -- orphaning the first, along with
     * anything queued in it -- and started a second task on every save. */
    if (xRequestQueue != NULL)
        return;

    xRequestQueue = xQueueCreate(BEEPER_CONTROL_QUEUE_LENGTH, sizeof(struct request_s));

    if (xRequestQueue == NULL)
    {
        ESP_LOGE("beeper", "beeper_enable: Failed to create the queue");
        return;
    }

    xTaskCreate(beeper_task, "beeper_task", BEEPER_TASK_STACK_SIZE, NULL, 1, NULL);
}

static void beeper_task(void *parameter)
{
    (void)parameter;

    while (true)
    {
        struct request_s beep_request;

        /* Blocks until there is a note. This used to poll with a 1 ms timeout,
         * which on the host target rounds to no wait at all -- the tick is 4 ms
         * -- and would have spun a core for nothing. */
        if (xQueueReceive(xRequestQueue, &beep_request, portMAX_DELAY) == pdTRUE)
        {
            const struct beeper_note_s *notes =
                (beep_request.notes != NULL) ? beep_request.notes : &beep_request.inline_note;

            for (uint8_t i = 0; i < beep_request.count; i++)
                play_note(&notes[i]);

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
