/**
 * @file linux/port_outputs.c
 *
 * The relays and the indicator LEDs on a desktop, which has neither.
 *
 * Both ports are in one file because they are one decision: `OHEZ_OUTPUTS=1`
 * gives the simulator three relays and a three-channel mood light that exist
 * only as log lines, and without it there are none of either. Splitting that
 * switch over two files would let a desktop end up with LEDs and no relays,
 * which is not a board anyone ships.
 *
 * Off by default, and for the reason port_ble.c gives: these outputs have
 * topics, and a simulator that quietly announced three relays to a broker
 * would put a switch on someone's dashboard that turns nothing on. Asked for
 * explicitly, they are the cheapest way to exercise everything above the pin
 * -- the subscriptions, the payload parsing, the state that is republished on
 * a reconnect -- against a real broker, on a machine with a debugger.
 *
 * Unlike the BLE fixture this invents nothing. An output has no reading to
 * fake: what the log prints is exactly what was asked for, so the only thing
 * missing at the far end is the click.
 */
#include "port_led.h"
#include "port_relay.h"

#include <stdlib.h>
#include <string.h>

#include "esp_log.h"

static const char *TAG = "port_outputs";

/* The Lanbon L8-HS's complement, so that what is exercised here is the shape
 * the device actually has. */
#define SIM_RELAY_COUNT 3

static const char *sim_led_names[] = { "red", "green", "blue" };

#define SIM_LED_COUNT (sizeof(sim_led_names) / sizeof(sim_led_names[0]))

/* Resolved once, on the first count enquiry: port_relay_count() is called
 * from a loop and getenv() on every pass would be silly. -1 is "not yet
 * asked". */
static int outputs_enabled = -1;

static bool outputs_present(void)
{
    if (outputs_enabled < 0)
    {
        const char *want = getenv("OHEZ_OUTPUTS");

        outputs_enabled = (want != NULL && strcmp(want, "0") != 0) ? 1 : 0;

        if (outputs_enabled == 0)
            ESP_LOGI(TAG, "no relays or LEDs on this target "
                          "(set OHEZ_OUTPUTS=1 for logged ones)");
        else
            ESP_LOGI(TAG, "%u relays and %u LEDs, logged only",
                     (unsigned)SIM_RELAY_COUNT, (unsigned)SIM_LED_COUNT);
    }

    return outputs_enabled == 1;
}

/* ------------------------------------------------------------------ relays */

unsigned port_relay_count(void)
{
    return outputs_present() ? SIM_RELAY_COUNT : 0;
}

void port_relay_init(void)
{
    if (outputs_present() == false)
        return;

    for (unsigned i = 0; i < SIM_RELAY_COUNT; i++)
        ESP_LOGI(TAG, "relay %u: off", i + 1);
}

void port_relay_set(unsigned index, bool on)
{
    if (index >= port_relay_count())
        return;

    ESP_LOGI(TAG, "relay %u: %s", index + 1, on ? "on" : "off");
}

/* -------------------------------------------------------------------- LEDs */

unsigned port_led_count(void)
{
    return outputs_present() ? (unsigned)SIM_LED_COUNT : 0;
}

const char *port_led_name(unsigned index)
{
    if (index >= port_led_count())
        return NULL;

    return sim_led_names[index];
}

void port_led_init(void)
{
    if (outputs_present() == false)
        return;

    for (unsigned i = 0; i < SIM_LED_COUNT; i++)
        ESP_LOGI(TAG, "LED %s: 0 %%", sim_led_names[i]);
}

void port_led_set(unsigned index, uint8_t percent)
{
    if (index >= port_led_count())
        return;

    if (percent > 100)
        percent = 100;

    ESP_LOGI(TAG, "LED %s: %u %%", sim_led_names[index], (unsigned)percent);
}
