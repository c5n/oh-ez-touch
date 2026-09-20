/**
 * @file esp32/port_led.c
 *
 * The indicator LEDs on LEDC.
 *
 * PWM rather than on/off, because a mood light at full brightness in a dark
 * hallway is a torch. The channels sit on LEDC_TIMER_2, which is neither the
 * backlight's (LEDC_TIMER_0) nor the beeper's (LEDC_TIMER_1) -- the frequency
 * is a property of the timer and not of the channel, which is the bug
 * port_backlight.c describes at length. Nothing here retunes a timer, so the
 * separation is only insurance; it costs one of four timers and removes a
 * whole class of question.
 *
 * The three channels share that one timer quite deliberately: they are the
 * same kind of load at the same frequency, and a colour mixed from three
 * channels running at three slightly different rates would beat.
 *
 * Compiles to the four empty functions at the bottom on a board whose pin
 * table has no OHEZ_LED_PINS.
 */
#include "port_led.h"

#include "board_pins.h"

#ifdef OHEZ_LED_PINS

#include "driver/ledc.h"
#include "esp_log.h"

static const char *TAG = "port_led";

static const int   led_pins[]  = OHEZ_LED_PINS;
static const char *led_names[] = OHEZ_LED_NAMES;

#define LED_COUNT (sizeof(led_pins) / sizeof(led_pins[0]))

_Static_assert(sizeof(led_names) / sizeof(led_names[0]) == LED_COUNT,
               "OHEZ_LED_NAMES must name every pin in OHEZ_LED_PINS");

#define LED_TIMER       LEDC_TIMER_2
#define LED_MODE        LEDC_LOW_SPEED_MODE
#define LED_RESOLUTION  LEDC_TIMER_10_BIT
#define LED_MAX_DUTY    1023
/* The backlight's figure, for the backlight's reasons: well above hearing, so
 * nothing in the wall whistles, and comfortably inside what 10 bits allow off
 * the 80 MHz APB clock. */
#define LED_FREQ_HZ     30000

/* Channel 0 is the backlight and channel 1 the beeper, so the LEDs start at
 * 2. The ESP32's low-speed mode has eight, which leaves three spare. */
#define LED_FIRST_CHANNEL 2

_Static_assert(LED_FIRST_CHANNEL + LED_COUNT <= LEDC_CHANNEL_MAX,
               "too many LEDs for the LEDC channels left over");

static bool leds_ready;

unsigned port_led_count(void)
{
    return (unsigned)LED_COUNT;
}

const char *port_led_name(unsigned index)
{
    if (index >= LED_COUNT)
        return NULL;

    return led_names[index];
}

void port_led_init(void)
{
    if (leds_ready == true)
        return;

    ledc_timer_config_t timer = {
        .speed_mode      = LED_MODE,
        .duty_resolution = LED_RESOLUTION,
        .timer_num       = LED_TIMER,
        .freq_hz         = LED_FREQ_HZ,
        .clk_cfg         = LEDC_AUTO_CLK,
        .deconfigure     = false,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer));

    for (size_t i = 0; i < LED_COUNT; i++)
    {
        ledc_channel_config_t channel = {
            .gpio_num   = led_pins[i],
            .speed_mode = LED_MODE,
            .channel    = (ledc_channel_t)(LED_FIRST_CHANNEL + i),
            .timer_sel  = LED_TIMER,
            .duty       = 0,
            .hpoint     = 0,
            .sleep_mode = LEDC_SLEEP_MODE_NO_ALIVE_NO_PD,
            .flags      = { .output_invert = OHEZ_LED_ACTIVE_LOW },
        };
        ESP_ERROR_CHECK(ledc_channel_config(&channel));

        ESP_LOGI(TAG, "LED %s on GPIO %d", led_names[i], led_pins[i]);
    }

    leds_ready = true;
}

void port_led_set(unsigned index, uint8_t percent)
{
    if (index >= LED_COUNT)
        return;

    if (leds_ready == false)
        port_led_init();

    if (percent > 100)
        percent = 100;

    uint32_t duty = ((uint32_t)percent * LED_MAX_DUTY) / 100;

    ESP_ERROR_CHECK(ledc_set_duty(LED_MODE, (ledc_channel_t)(LED_FIRST_CHANNEL + index), duty));
    ESP_ERROR_CHECK(ledc_update_duty(LED_MODE, (ledc_channel_t)(LED_FIRST_CHANNEL + index)));
}

#else /* !OHEZ_LED_PINS */

unsigned port_led_count(void)
{
    return 0;
}

const char *port_led_name(unsigned index)
{
    (void)index;
    return NULL;
}

void port_led_init(void)
{
}

void port_led_set(unsigned index, uint8_t percent)
{
    (void)index;
    (void)percent;
}

#endif /* OHEZ_LED_PINS */
