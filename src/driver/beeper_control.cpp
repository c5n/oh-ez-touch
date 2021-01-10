#include "beeper_control.hpp"

#include "Arduino.h"
#include "debug.h"

#ifndef DEBUG_BEEPER_CONTROL
#define DEBUG_BEEPER_CONTROL 0
#endif

#define BEEPER_PWM_FREQUENCY    2000
#define BEEPER_PWM_RESOLUTION   8
#define BEEPER_TASK_STACK_SIZE  800

struct request_s
{
    uint16_t note;
    uint8_t volume;
    uint16_t duration;
    uint16_t pause;
};

QueueHandle_t xRequestQueue = 0;

static void beeper_task(void *parameter);

#define BEEP_NOTE_REQUEST_NULL 0

void beeper_playNote(uint16_t note, uint8_t volume, uint16_t duration, uint16_t pause)
{
#if DEBUG_BEEPER_CONTROL
    debug_printf("beeper_playNote: freq=%u Hz, duration=%u ms\n", note, duration);
#endif
    if (xRequestQueue != 0)
    {
        struct request_s new_request;
        new_request.note = note;
        volume = volume > 100 ? 100 : volume;
        new_request.volume = map(volume, 0, 100, 0, 127);
        new_request.duration = duration;
        new_request.pause = pause;
        xQueueSend(xRequestQueue, &new_request, 0);
    }
}

void beeper_setup(uint8_t pin)
{
    ledcSetup(BEEPER_CONTROL_PWM_CHANNEL, BEEPER_PWM_FREQUENCY, BEEPER_PWM_RESOLUTION);
    ledcAttachPin(pin, BEEPER_CONTROL_PWM_CHANNEL);
    ledcWrite(BEEPER_CONTROL_PWM_CHANNEL, 0); // set volume to 0
}

void beeper_enable(void)
{
    xRequestQueue = xQueueCreate(BEEPER_CONTROL_QUEUE_LENGTH, sizeof(struct request_s));
    if (xRequestQueue == 0)
        debug_printf("beeper_setup: Failed to create the queue\n");

    xTaskCreate(beeper_task, "beeper_task", BEEPER_TASK_STACK_SIZE, NULL, 1, NULL);
}

static void beeper_task(void *parameter)
{
    while (true)
    {
        struct request_s beep_request;
        if (xQueueReceive(xRequestQueue, &beep_request, 1 / portTICK_PERIOD_MS) == pdTRUE)
        {
            ledcWriteTone(BEEPER_CONTROL_PWM_CHANNEL, beep_request.note);
            ledcWrite(BEEPER_CONTROL_PWM_CHANNEL, beep_request.volume);
            vTaskDelay(beep_request.duration / portTICK_PERIOD_MS);
            ledcWrite(BEEPER_CONTROL_PWM_CHANNEL, 0);
            vTaskDelay(beep_request.pause / portTICK_PERIOD_MS);
        }
    }
}
