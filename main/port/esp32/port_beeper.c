/**
 * @file esp32/port_beeper.c
 *
 * The buzzer on LEDC.
 *
 * It owns LEDC_TIMER_1, which is the fix for the backlight bug described in
 * port_backlight.c: the frequency of a note is a property of the timer, not of
 * the channel, so a beeper that shares a timer with the backlight retunes the
 * backlight every time it sounds.
 *
 * The volume scale is the one the UI has always effectively used, which is not
 * the one the old code appears to use. beeper_control.cpp configured 8-bit
 * resolution, but Arduino's ledcWriteTone() reconfigured the timer to 10 bits
 * before every note, so the duty it then wrote -- map(volume, 0, 100, 0, 127)
 * -- landed out of 1024 rather than out of 256. Every beep this firmware has
 * ever made was at a quarter of the intended duty. Reproducing the arithmetic
 * exactly keeps it sounding the way it does; taking the 8 bits at face value
 * would make it four times louder than anyone has heard it.
 */
#include "port_beeper.h"

#include "board_pins.h"

#if OHEZ_HAS_BEEPER

#include "driver/ledc.h"
#include "esp_log.h"

static const char *TAG = "port_beeper";

#define BEEPER_TIMER        LEDC_TIMER_1
#define BEEPER_CHANNEL      LEDC_CHANNEL_1
#define BEEPER_MODE         LEDC_LOW_SPEED_MODE
#define BEEPER_RESOLUTION   LEDC_TIMER_10_BIT
/* map(volume, 0, 100, 0, 127) at volume 100, out of the 1024 the resolution
 * above gives. See the file comment. */
#define BEEPER_MAX_DUTY     127
/* Only until the first note sets a real one; the notes run 131 to 3136 Hz. */
#define BEEPER_IDLE_FREQ_HZ 2000

static bool beeper_ready;

bool port_beeper_init(void)
{
    if (beeper_ready == true)
        return true;

    ledc_timer_config_t timer = {
        .speed_mode      = BEEPER_MODE,
        .duty_resolution = BEEPER_RESOLUTION,
        .timer_num       = BEEPER_TIMER,
        .freq_hz         = BEEPER_IDLE_FREQ_HZ,
        .clk_cfg         = LEDC_AUTO_CLK,
        .deconfigure     = false,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer));

    ledc_channel_config_t channel = {
        .gpio_num   = OHEZ_BEEPER_PIN,
        .speed_mode = BEEPER_MODE,
        .channel    = BEEPER_CHANNEL,
        .timer_sel  = BEEPER_TIMER,
        .duty       = 0,
        .hpoint     = 0,
        .sleep_mode = LEDC_SLEEP_MODE_NO_ALIVE_NO_PD,
        .flags      = { .output_invert = 0 },
    };
    ESP_ERROR_CHECK(ledc_channel_config(&channel));

    beeper_ready = true;

    ESP_LOGI(TAG, "beeper on GPIO %d", OHEZ_BEEPER_PIN);

    return true;
}

void port_beeper_tone(uint16_t freq, uint8_t volume)
{
    if (beeper_ready == false)
        return;

    if (volume > 100)
        volume = 100;

    uint32_t duty = ((uint32_t)volume * BEEPER_MAX_DUTY) / 100;

    /* Silence is duty 0 at whatever frequency was last set: retuning the timer
     * to stop a note would be pointless, and ledc_set_freq() on a timer with
     * duty 0 still costs a reconfiguration. */
    if (duty != 0)
        ESP_ERROR_CHECK(ledc_set_freq(BEEPER_MODE, BEEPER_TIMER, freq));

    ESP_ERROR_CHECK(ledc_set_duty(BEEPER_MODE, BEEPER_CHANNEL, duty));
    ESP_ERROR_CHECK(ledc_update_duty(BEEPER_MODE, BEEPER_CHANNEL));
}

#else /* !OHEZ_HAS_BEEPER */

bool port_beeper_init(void)
{
    return false;
}

void port_beeper_tone(uint16_t freq, uint8_t volume)
{
    (void)freq;
    (void)volume;
}

#endif /* OHEZ_HAS_BEEPER */
