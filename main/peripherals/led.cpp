/**
 * @file led.cpp
 *
 * The indicator LEDs: which topics they answer to, and what a payload means.
 *
 * ------------------------------------------------------------------- topics
 *
 *   <prefix>/led/red, /led/green, /led/blue      0 to 100, the brightness now
 *   <prefix>/led/red/set, ...                    set one
 *
 * One pair per LED the board actually has, named by port_led_name(), so an
 * ArduiTouch publishes none of them and a Lanbon L8 publishes three.
 *
 * A `set` payload is a number from 0 to 100 -- openHAB's Dimmer percentage --
 * or ON and OFF for the two ends of it. A number wins where the two could
 * disagree: "1" is one percent and not "on", because a dimmer that jumped to
 * full when asked for its lowest setting would be useless. Out of range is
 * clamped; anything that is neither a number nor a recognised word is off.
 *
 * --------------------------------------------------------- three, not a colour
 *
 * Three brightnesses rather than one `led/color` taking a hex triplet, even
 * though the hardware is one RGB LED. The three are what the board has, a
 * colour is an interpretation, and an interpretation belongs with whatever is
 * publishing -- an openHAB Color item, a rule, a scene -- rather than in a
 * panel that would have to be reflashed to change its mind about gamma. Three
 * topics also compose: a rule can fade one channel without restating the
 * other two.
 *
 * ----------------------------------------------------- and not the backlight
 *
 * These are not control/backlight_control.cpp. That one is the display, it is
 * driven by the dim timeout rather than by anyone's command, and it has
 * settings because how bright a screen should be in a dark room is a
 * preference. A mood light is neither a preference nor a timeout: it is an
 * output someone else decides about, which is why it has no setting and only
 * a topic.
 *
 * The state is not saved, for the reasons relay.cpp sets out at length. The
 * LEDs come up dark and the broker's retained `set` messages, if there are
 * any, restore them a second after the connection.
 */
#include "led.hpp"

#include "debug.h"

#include "mqtt/ohez_mqtt.hpp"
#include "port/port_led.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "esp_log.h"

static const char *TAG = "led";

/* Above the three the largest supported board has. The real count is
 * port_led_count(); this only sizes the state that shadows it. */
#define LED_MAX 8

/* "led/green/set" and room for a longer name than any board has yet. */
#define LED_TOPIC_MAX 32

static unsigned led_count = 0;

static uint8_t led_percent[LED_MAX];
static bool    led_published[LED_MAX];

static bool led_was_connected = false;

/* ------------------------------------------------------------------ commands */

/* The payload as a brightness.
 *
 * A leading digit decides it: strtoul() would read "ON" as 0 just as happily,
 * so the two forms are told apart before either is parsed rather than after.
 */
static uint8_t wanted_percent(const char *value)
{
    while (isspace((unsigned char)*value))
        value++;

    if (isdigit((unsigned char)*value))
    {
        unsigned long percent = strtoul(value, NULL, 10);

        return (percent > 100) ? 100 : (uint8_t)percent;
    }

    if (strcasecmp(value, "ON") == 0 || strcasecmp(value, "TRUE") == 0
        || strcasecmp(value, "YES") == 0)
        return 100;

    return 0;
}

/* Which channel `led/<name>/set` names, or led_count when it names none. */
static unsigned led_by_topic(const char *topic)
{
    /* "led/" is 4 characters, and the filter guarantees the "/set". */
    const char *name = topic + 4;
    size_t      len = strcspn(name, "/");

    for (unsigned i = 0; i < led_count; i++)
    {
        const char *known = port_led_name(i);

        if (known != NULL && strncasecmp(name, known, len) == 0 && known[len] == '\0')
            return i;
    }

    return led_count;
}

/* `led/<name>/set`, on the application's task. */
static void led_command(const char *topic, const char *value)
{
    unsigned index = led_by_topic(topic);

    if (index >= led_count)
    {
        /* The name alone, not the rest of the topic. */
        ESP_LOGW(TAG, "no LED called %.*s", (int)strcspn(topic + 4, "/"), topic + 4);
        return;
    }

    uint8_t want = wanted_percent(value);

#if CONFIG_OHEZ_DEBUG_LED
    printf("led_command: %s = %s -> %s %u %%\r\n", topic, value, port_led_name(index),
           (unsigned)want);
#endif

    if (want == led_percent[index] && led_published[index] == true)
        return;

    led_percent[index] = want;
    led_published[index] = false;

    port_led_set(index, want);
}

/* ---------------------------------------------------------------------- API */

void led_setup(void)
{
    led_count = port_led_count();

    if (led_count > LED_MAX)
    {
        ESP_LOGW(TAG, "%u LEDs, only %u addressable", led_count, (unsigned)LED_MAX);
        led_count = LED_MAX;
    }

    if (led_count == 0)
        return;

    port_led_init();

    ohez_mqtt_subscribe("led/+/set", led_command);
}

void led_loop(void)
{
    if (led_count == 0)
        return;

    /* A reconnected broker holds nothing from the last session. Same edge,
     * for the same reason, as relay.cpp and ble/ble_scan.cpp watch. */
    bool connected = ohez_mqtt_connected();

    if (connected == true && led_was_connected == false)
        for (unsigned i = 0; i < led_count; i++)
            led_published[i] = false;

    led_was_connected = connected;

    if (connected == false)
        return;

    for (unsigned i = 0; i < led_count; i++)
    {
        char suffix[LED_TOPIC_MAX];
        char value[8];

        if (led_published[i] == true)
            continue;

        snprintf(suffix, sizeof(suffix), "led/%s", port_led_name(i));
        snprintf(value, sizeof(value), "%u", (unsigned)led_percent[i]);

        /* Only marked published when it actually went, so a failure is
         * retried rather than leaving the broker holding a stale brightness. */
        if (ohez_mqtt_publish_value(suffix, value) == true)
            led_published[i] = true;
    }
}
