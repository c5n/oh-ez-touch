#include "beeper_control.hpp"

#include "debug.h"

#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "esp_log.h"

#include "port/port_beeper.h"

#define BEEPER_TASK_STACK_SIZE  2048

struct request_s
{
    uint16_t note;
    uint8_t volume;
    uint16_t duration;
    uint16_t pause;
};

static QueueHandle_t xRequestQueue = NULL;

static void beeper_task(void *parameter);

void beeper_playNote(uint16_t note, uint8_t volume, uint16_t duration, uint16_t pause)
{
#if CONFIG_OHEZ_DEBUG_BEEPER_CONTROL
    printf("beeper_playNote: freq=%u Hz, duration=%u ms\r\n", note, duration);
#endif
    if (xRequestQueue != NULL)
    {
        struct request_s new_request;
        new_request.note = note;
        new_request.volume = volume > 100 ? 100 : volume;
        new_request.duration = duration;
        new_request.pause = pause;
        xQueueSend(xRequestQueue, &new_request, 0);
    }
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
            port_beeper_tone(beep_request.note, beep_request.volume);
            vTaskDelay(pdMS_TO_TICKS(beep_request.duration));
            port_beeper_tone(beep_request.note, 0);
            vTaskDelay(pdMS_TO_TICKS(beep_request.pause));

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
