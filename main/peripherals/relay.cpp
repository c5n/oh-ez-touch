/**
 * @file relay.cpp
 *
 * The relays: which topics they answer to, and what a payload means.
 *
 * ------------------------------------------------------------------- topics
 *
 *   <prefix>/relay/1 .. <prefix>/relay/<n>       ON or OFF, the current state
 *   <prefix>/relay/1/set ..                      switch one
 *
 * One pair per relay the board actually has, so an ArduiTouch publishes none
 * of them and a Lanbon L8-HS publishes three. Numbered from 1, because that
 * is what is printed on the wall plate; port_relay.h indexes from 0 and says
 * so.
 *
 * A `set` payload is ON, OFF, TRUE, FALSE, YES, NO, 1, 0 -- case-insensitive,
 * which covers openHAB's switch strings, a JSON boolean and a bare digit --
 * or TOGGLE, which is what a physical push-button publishes and would
 * otherwise need the sender to know the current state.
 *
 * -------------------------------------------------------- MQTT and no other
 *
 * Deliberately not a setting, not a widget and not an openHAB item.
 *
 * Not a setting, because the state of a light is not configuration: it
 * changes many times a day and writing each change to flash would wear the
 * chip out for no gain. It is not saved at all -- the relays come up off and
 * the broker's retained `set` message, if there is one, puts them back within
 * a second of the connection. That is MQTT doing what it is for, and it also
 * means a panel that is reflashed comes back in the state the installation
 * thinks it is in rather than the state it happened to die in.
 *
 * Not a widget, because the panel already draws whatever the sitemap says and
 * a relay wired into an installation belongs in that sitemap like every other
 * switch -- as an openHAB item bound to these topics through the MQTT
 * binding, which is one line of configuration and gets rules, schedules and
 * the phone app for free. A button hard-wired to the local relay would look
 * the same and do none of that.
 *
 * ------------------------------------------------------------------- states
 *
 * The published state is what was last written to the pin, because that is
 * all there is to know: the coil is driven from a GPIO with no sense line
 * back. It is published on change and again after every reconnect, since a
 * broker that has just come back holds none of the last session's retained
 * messages.
 */
#include "relay.hpp"

#include "debug.h"

#include "mqtt/ohez_mqtt.hpp"
#include "port/port_relay.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "esp_log.h"

static const char *TAG = "relay";

/* Comfortably above the three the largest supported board has. The real count
 * is port_relay_count(); this only sizes the state that shadows it. */
#define RELAY_MAX 8

/* "relay/8/set" and a little room. */
#define RELAY_TOPIC_MAX 24

static unsigned relay_count = 0;

static bool relay_on[RELAY_MAX];

/* Per relay, so that a state that changed while the broker was away is still
 * published when it comes back. Cleared on a reconnect for all of them. */
static bool relay_published[RELAY_MAX];

static bool relay_was_connected = false;

/* ------------------------------------------------------------------ commands */

/* What the payload asks for, given what is set now.
 *
 * Anything unrecognised is off, which is the direction a switch should fail
 * in: a typo turning something on unattended is the worse of the two. */
static bool wanted_state(const char *value, bool now)
{
    if (strcasecmp(value, "TOGGLE") == 0)
        return !now;

    return strcasecmp(value, "ON") == 0 || strcasecmp(value, "TRUE") == 0
           || strcasecmp(value, "YES") == 0 || strcmp(value, "1") == 0;
}

/* `relay/<n>/set`, on the application's task. */
static void relay_command(const char *topic, const char *value)
{
    /* "relay/" is 6 characters, and the filter guarantees the rest. strtoul()
     * stops at the '/' before "set" by itself. */
    const char   *name = topic + 6;
    unsigned long number = strtoul(name, NULL, 10);

    if (number < 1 || number > relay_count)
    {
        /* The segment alone, not the rest of the topic: "no relay 9/set"
         * reads as though the topic were the problem. */
        ESP_LOGW(TAG, "no relay %.*s", (int)strcspn(name, "/"), name);
        return;
    }

    unsigned index = (unsigned)number - 1;
    bool     want = wanted_state(value, relay_on[index]);

#if CONFIG_OHEZ_DEBUG_RELAY
    printf("relay_command: %s = %s -> relay %lu %s\r\n", topic, value, number,
           want ? "on" : "off");
#endif

    if (want == relay_on[index] && relay_published[index] == true)
        return;

    relay_on[index] = want;
    relay_published[index] = false;

    port_relay_set(index, want);

    ESP_LOGI(TAG, "relay %lu: %s", number, want ? "on" : "off");
}

/* ---------------------------------------------------------------------- API */

void relay_setup(void)
{
    relay_count = port_relay_count();

    if (relay_count > RELAY_MAX)
    {
        /* A board with more relays than this file expects: drive the ones it
         * can address rather than nothing at all, and say which were left. */
        ESP_LOGW(TAG, "%u relays, only %u addressable", relay_count, (unsigned)RELAY_MAX);
        relay_count = RELAY_MAX;
    }

    if (relay_count == 0)
        return;

    port_relay_init();

    ohez_mqtt_subscribe("relay/+/set", relay_command);
}

void relay_loop(void)
{
    if (relay_count == 0)
        return;

    /* A reconnected broker holds nothing from the last session, so everything
     * has to be said again. Same edge, for the same reason, as the one
     * ble/ble_scan.cpp watches. */
    bool connected = ohez_mqtt_connected();

    if (connected == true && relay_was_connected == false)
        for (unsigned i = 0; i < relay_count; i++)
            relay_published[i] = false;

    relay_was_connected = connected;

    if (connected == false)
        return;

    for (unsigned i = 0; i < relay_count; i++)
    {
        char suffix[RELAY_TOPIC_MAX];

        if (relay_published[i] == true)
            continue;

        snprintf(suffix, sizeof(suffix), "relay/%u", i + 1);

        /* Only marked published when it actually went: a publish that failed
         * is retried on the next pass rather than leaving the broker holding
         * a state the panel is not in. */
        if (ohez_mqtt_publish_value(suffix, relay_on[i] ? "ON" : "OFF") == true)
            relay_published[i] = true;
    }
}
