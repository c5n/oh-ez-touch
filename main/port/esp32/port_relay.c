/**
 * @file esp32/port_relay.c
 *
 * The relays on plain GPIO.
 *
 * No PWM, no drive strength, nothing to configure: a relay coil is on or it
 * is off, and the only board-dependent thing about it is which pin and which
 * way round. Both come from board_pins.h.
 *
 * The whole file compiles to the three empty functions at the bottom on a
 * board whose pin table has no OHEZ_RELAY_PINS, which is every board but the
 * Lanbon L8-HS.
 */
#include "port_relay.h"

#include "board_pins.h"

#ifdef OHEZ_RELAY_PINS

#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "port_relay";

static const int relay_pins[] = OHEZ_RELAY_PINS;

#define RELAY_COUNT (sizeof(relay_pins) / sizeof(relay_pins[0]))

static bool relays_ready;

unsigned port_relay_count(void)
{
    return (unsigned)RELAY_COUNT;
}

void port_relay_init(void)
{
    if (relays_ready == true)
        return;

    for (size_t i = 0; i < RELAY_COUNT; i++)
    {
        gpio_config_t pin = {
            .pin_bit_mask = 1ULL << relay_pins[i],
            .mode         = GPIO_MODE_OUTPUT,
            .pull_up_en   = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type    = GPIO_INTR_DISABLE,
        };
        ESP_ERROR_CHECK(gpio_config(&pin));

        /* Before anything else can ask for a state: gpio_config() leaves the
         * output latch at whatever it held, so a pin that came out of a warm
         * reset high would stay high until the first command arrived. */
        ESP_ERROR_CHECK(gpio_set_level(relay_pins[i], OHEZ_RELAY_ACTIVE_LOW ? 1 : 0));

        ESP_LOGI(TAG, "relay %u on GPIO %d, %s", (unsigned)(i + 1), relay_pins[i],
                 OHEZ_RELAY_ACTIVE_LOW ? "active low" : "active high");
    }

    relays_ready = true;
}

void port_relay_set(unsigned index, bool on)
{
    if (index >= RELAY_COUNT)
        return;

    if (relays_ready == false)
        port_relay_init();

    ESP_ERROR_CHECK(gpio_set_level(relay_pins[index], (on != (bool)OHEZ_RELAY_ACTIVE_LOW) ? 1 : 0));
}

#else /* !OHEZ_RELAY_PINS */

unsigned port_relay_count(void)
{
    return 0;
}

void port_relay_init(void)
{
}

void port_relay_set(unsigned index, bool on)
{
    (void)index;
    (void)on;
}

#endif /* OHEZ_RELAY_PINS */
