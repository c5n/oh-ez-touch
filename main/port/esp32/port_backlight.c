/**
 * @file esp32/port_backlight.c
 *
 * The backlight on LEDC.
 *
 * Two things here are deliberately not what the Arduino code did, because what
 * it did was wrong:
 *
 * 1. It owns LEDC_TIMER_0 and says so. Arduino's esp32-hal-ledc derived the
 *    timer from the channel -- `timer = ((chan / 2) % 4)` -- which put the
 *    backlight (channel 0) and the beeper (channel 1) on the same hardware
 *    timer. ledcWriteTone() then reconfigured that timer for the note, so the
 *    first touch beep dropped the backlight's PWM from 30 kHz to somewhere
 *    between 131 and 3136 Hz and left it there: visible flicker and audible
 *    coil whine, permanently, after the first tap. Here the beeper is on
 *    LEDC_TIMER_1 and cannot reach this one.
 *
 * 2. The polarity is in the hardware, not in the arithmetic. The old code
 *    computed `map(percent, 0, 100, 1023, 0)` -- so 100 % meant duty 0 -- and
 *    then, for a board that declared TFT_BACKLIGHT_INVERT, inverted the
 *    percentage first. Two inversions that cancelled on one board and did not
 *    on the other. Duty is now simply proportional to brightness, and the pin
 *    is inverted in the channel configuration for the boards whose backlight
 *    is active low.
 */
#include "port_backlight.h"

#include "board_pins.h"

#include "driver/ledc.h"
#include "esp_log.h"

static const char *TAG = "port_backlight";

#define BACKLIGHT_TIMER         LEDC_TIMER_0
#define BACKLIGHT_CHANNEL       LEDC_CHANNEL_0
#define BACKLIGHT_MODE          LEDC_LOW_SPEED_MODE
#define BACKLIGHT_RESOLUTION    LEDC_TIMER_10_BIT
#define BACKLIGHT_MAX_DUTY      1023
/* Well above hearing, and comfortably within what 10 bits allow off the 80 MHz
 * APB clock (80e6 / 1024 = 78 kHz). Same figure the Arduino code used. */
#define BACKLIGHT_FREQ_HZ       30000

static bool backlight_ready;

void port_backlight_init(void)
{
    if (backlight_ready == true)
        return;

    ledc_timer_config_t timer = {
        .speed_mode      = BACKLIGHT_MODE,
        .duty_resolution = BACKLIGHT_RESOLUTION,
        .timer_num       = BACKLIGHT_TIMER,
        .freq_hz         = BACKLIGHT_FREQ_HZ,
        .clk_cfg         = LEDC_AUTO_CLK,
        .deconfigure     = false,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer));

    ledc_channel_config_t channel = {
        .gpio_num   = OHEZ_BACKLIGHT_PIN,
        .speed_mode = BACKLIGHT_MODE,
        .channel    = BACKLIGHT_CHANNEL,
        .intr_type  = LEDC_INTR_DISABLE,
        .timer_sel  = BACKLIGHT_TIMER,
        .duty       = 0,
        .hpoint     = 0,
        .sleep_mode = LEDC_SLEEP_MODE_NO_ALIVE_NO_PD,
        .flags      = { .output_invert = OHEZ_BACKLIGHT_ACTIVE_LOW },
    };
    ESP_ERROR_CHECK(ledc_channel_config(&channel));

    backlight_ready = true;

    ESP_LOGI(TAG, "backlight on GPIO %d, %s", OHEZ_BACKLIGHT_PIN,
             OHEZ_BACKLIGHT_ACTIVE_LOW ? "active low" : "active high");
}

void port_backlight_set(uint8_t percent)
{
    if (backlight_ready == false)
        port_backlight_init();

    if (percent > 100)
        percent = 100;

    uint32_t duty = ((uint32_t)percent * BACKLIGHT_MAX_DUTY) / 100;

    ESP_ERROR_CHECK(ledc_set_duty(BACKLIGHT_MODE, BACKLIGHT_CHANNEL, duty));
    ESP_ERROR_CHECK(ledc_update_duty(BACKLIGHT_MODE, BACKLIGHT_CHANNEL));
}
