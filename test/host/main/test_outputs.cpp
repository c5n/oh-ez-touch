/* Unit tests for the relays and the LEDs in main/peripherals/.
 *
 * These are the first two modules whose whole interface is a broker: there is
 * no widget to press and no setting to look at, so "does ON turn it on" is a
 * question only a test can answer without a Lanbon on the bench and a
 * mosquitto next to it.
 *
 * Both files are reachable from here for the same reason ble_beacon.cpp is:
 * the pin is behind port_relay.h and port_led.h and the broker is behind
 * ohez_mqtt.hpp, so what is left in peripherals/ is the part worth testing --
 * what a payload means, what state it leaves behind, and what gets published
 * about it. The doubles below stand in for both edges, and the relay and LED
 * handlers are reached through the real ohez_mqtt_subscribe() call each module
 * makes in its setup(), so the registration is under test too.
 *
 * Run with:
 *   cd test/host && idf.py build && ./build/oh-ez-touch-host-test.elf
 */

#include <unity.h>

#include <stdio.h>
#include <string.h>

#include "mqtt/ohez_mqtt.hpp"
#include "peripherals/led.hpp"
#include "peripherals/relay.hpp"
#include "port/port_led.h"
#include "port/port_relay.h"
#include "test_suites.hpp"

/* ------------------------------------------------------------- the doubles */

#define FAKE_OUTPUTS 3
#define FAKE_PUBLISHES 16

/* What the pins were last told. -1 is "never written", which is how the tests
 * tell "set to off" apart from "not touched". */
static int fake_relay[FAKE_OUTPUTS];
static int fake_led[FAKE_OUTPUTS];
static bool fake_relay_inited;
static bool fake_led_inited;

/* What went to the broker, oldest first. */
static char fake_topic[FAKE_PUBLISHES][48];
static char fake_payload[FAKE_PUBLISHES][16];
static unsigned fake_publishes;

/* Whether the next publish is allowed to succeed, and whether there is a
 * connection at all: the two failure modes relay_loop() has to survive. */
static bool fake_publish_ok = true;
static bool fake_connected = true;

/* The filters and handlers the two setups registered. */
static const char *fake_filter[4];
static ohez_mqtt_command_fn fake_handler[4];
static unsigned fake_subs;

static void fake_reset(void)
{
    for (unsigned i = 0; i < FAKE_OUTPUTS; i++)
    {
        fake_relay[i] = -1;
        fake_led[i] = -1;
    }

    fake_relay_inited = false;
    fake_led_inited = false;
    fake_publishes = 0;
    fake_publish_ok = true;
    fake_connected = true;
    fake_subs = 0;
}

/* Feed one message to whichever handler registered for it, the way
 * commands_dispatch() does. Fails the test if nothing did, since a filter
 * that no longer matches its own topics is exactly the bug this catches. */
static void fake_deliver(const char *topic, const char *value)
{
    for (unsigned i = 0; i < fake_subs; i++)
    {
        /* The filters here are all "<head>/+/set", so matching the head is
         * enough to route between them -- the real matcher lives in
         * ohez_mqtt.cpp and is not what this suite is about. */
        size_t head = strcspn(fake_filter[i], "/");

        if (strncmp(fake_filter[i], topic, head) == 0 && topic[head] == '/')
        {
            fake_handler[i](topic, value);
            return;
        }
    }

    TEST_FAIL_MESSAGE("no handler registered for that topic");
}

static int published_index(const char *topic)
{
    for (unsigned i = 0; i < fake_publishes; i++)
        if (strcmp(fake_topic[i], topic) == 0)
            return (int)i;

    return -1;
}

/* The last thing published to this topic, or NULL. Last rather than first
 * because a republish is meant to overwrite what a subscriber believes. */
static const char *published(const char *topic)
{
    const char *value = NULL;

    for (unsigned i = 0; i < fake_publishes; i++)
        if (strcmp(fake_topic[i], topic) == 0)
            value = fake_payload[i];

    return value;
}

unsigned port_relay_count(void)
{
    return FAKE_OUTPUTS;
}

void port_relay_init(void)
{
    fake_relay_inited = true;
}

void port_relay_set(unsigned index, bool on)
{
    if (index < FAKE_OUTPUTS)
        fake_relay[index] = on ? 1 : 0;
}

static const char *fake_led_names[FAKE_OUTPUTS] = { "red", "green", "blue" };

unsigned port_led_count(void)
{
    return FAKE_OUTPUTS;
}

const char *port_led_name(unsigned index)
{
    return (index < FAKE_OUTPUTS) ? fake_led_names[index] : NULL;
}

void port_led_init(void)
{
    fake_led_inited = true;
}

void port_led_set(unsigned index, uint8_t percent)
{
    if (index < FAKE_OUTPUTS)
        fake_led[index] = percent;
}

bool ohez_mqtt_subscribe(const char *filter, ohez_mqtt_command_fn handler)
{
    if (fake_subs >= sizeof(fake_filter) / sizeof(fake_filter[0]))
        return false;

    fake_filter[fake_subs] = filter;
    fake_handler[fake_subs] = handler;
    fake_subs++;

    return true;
}

bool ohez_mqtt_connected(void)
{
    return fake_connected;
}

bool ohez_mqtt_publish_value(const char *suffix, const char *value)
{
    if (fake_publish_ok == false)
        return false;

    if (fake_publishes < FAKE_PUBLISHES)
    {
        snprintf(fake_topic[fake_publishes], sizeof(fake_topic[0]), "%s", suffix);
        snprintf(fake_payload[fake_publishes], sizeof(fake_payload[0]), "%s", value);
    }

    fake_publishes++;

    return true;
}

/* Only the four above are called by the code under test; these two complete
 * the header so the link succeeds. */
bool ohez_mqtt_clear_value(const char *suffix)
{
    (void)suffix;
    return true;
}

void ohez_mqtt_publish_bme280(float t, float h, float p)
{
    (void)t;
    (void)h;
    (void)p;
}

/* Put both modules into the state a freshly booted panel is in: every output
 * off, every topic published, nothing outstanding.
 *
 * Not by restarting them, because they cannot be restarted -- they are
 * firmware and keep static state that is set up once and never torn down.
 * Everything below therefore goes through the real handlers, and the record
 * of what that took is wiped afterwards so that a test sees only what it
 * caused. That the modules cannot be reset is worth having felt here: it is
 * also why relay_command() skips the pin when the state already matches, and
 * a test that quietly reset them would not have to think about either. */
static void outputs_known_state(void)
{
    fake_reset();

    relay_setup();
    led_setup();

    fake_deliver("relay/1/set", "OFF");
    fake_deliver("relay/2/set", "OFF");
    fake_deliver("relay/3/set", "OFF");
    fake_deliver("led/red/set", "0");
    fake_deliver("led/green/set", "0");
    fake_deliver("led/blue/set", "0");

    /* The connection appearing, which is what makes both modules publish
     * everything they know -- the edge a booted panel crosses once. */
    fake_connected = false;
    relay_loop();
    led_loop();
    fake_connected = true;
    relay_loop();
    led_loop();

    for (unsigned i = 0; i < FAKE_OUTPUTS; i++)
    {
        fake_relay[i] = -1;
        fake_led[i] = -1;
    }

    fake_publishes = 0;
}

/* The broker going away and coming back. Both modules watch this edge, and
 * neither may assume the broker kept anything across it. */
static void broker_reconnects(void)
{
    fake_connected = false;
    relay_loop();
    led_loop();
    fake_connected = true;
    relay_loop();
    led_loop();
}

/* ------------------------------------------------------------------ relays */

static void test_relay_setup_claims_its_pins_and_topic(void)
{
    fake_reset();
    relay_setup();

    TEST_ASSERT_TRUE(fake_relay_inited);
    TEST_ASSERT_EQUAL_UINT(1, fake_subs);
    TEST_ASSERT_EQUAL_STRING("relay/+/set", fake_filter[0]);
}

static void test_relay_announces_all_of_them(void)
{
    outputs_known_state();
    broker_reconnects();

    /* Off, and said so: a subscriber that connects before the first command
     * must not have to guess, and there is no reading back from a coil. */
    TEST_ASSERT_EQUAL_STRING("OFF", published("relay/1"));
    TEST_ASSERT_EQUAL_STRING("OFF", published("relay/2"));
    TEST_ASSERT_EQUAL_STRING("OFF", published("relay/3"));
}

static void test_relay_on_and_off(void)
{
    outputs_known_state();

    fake_deliver("relay/2/set", "ON");
    relay_loop();

    TEST_ASSERT_EQUAL_INT(1, fake_relay[1]);
    TEST_ASSERT_EQUAL_STRING("ON", published("relay/2"));

    /* Only the one that changed. */
    TEST_ASSERT_EQUAL_INT(-1, fake_relay[0]);
    TEST_ASSERT_NULL(published("relay/1"));

    fake_deliver("relay/2/set", "OFF");
    relay_loop();

    TEST_ASSERT_EQUAL_INT(0, fake_relay[1]);
    TEST_ASSERT_EQUAL_STRING("OFF", published("relay/2"));
}

static void test_relay_payload_spellings(void)
{
    static const char *on[] = { "ON", "on", "On", "true", "TRUE", "yes", "1" };
    static const char *off[] = { "OFF", "off", "false", "no", "0", "", "banana" };

    for (unsigned i = 0; i < sizeof(on) / sizeof(on[0]); i++)
    {
        outputs_known_state();
        fake_deliver("relay/1/set", on[i]);
        TEST_ASSERT_EQUAL_INT_MESSAGE(1, fake_relay[0], on[i]);
    }

    /* Each starts from on, so "did nothing" cannot pass for "turned off". */
    for (unsigned i = 0; i < sizeof(off) / sizeof(off[0]); i++)
    {
        outputs_known_state();
        fake_deliver("relay/1/set", "ON");
        fake_deliver("relay/1/set", off[i]);
        TEST_ASSERT_EQUAL_INT_MESSAGE(0, fake_relay[0], off[i]);
    }
}

static void test_relay_toggle(void)
{
    outputs_known_state();

    fake_deliver("relay/3/set", "TOGGLE");
    TEST_ASSERT_EQUAL_INT(1, fake_relay[2]);

    fake_deliver("relay/3/set", "toggle");
    TEST_ASSERT_EQUAL_INT(0, fake_relay[2]);
}

static void test_relay_out_of_range_is_ignored(void)
{
    outputs_known_state();

    fake_deliver("relay/0/set", "ON");
    fake_deliver("relay/4/set", "ON");
    fake_deliver("relay/x/set", "ON");
    relay_loop();

    for (unsigned i = 0; i < FAKE_OUTPUTS; i++)
        TEST_ASSERT_EQUAL_INT(-1, fake_relay[i]);

    TEST_ASSERT_EQUAL_UINT(0, fake_publishes);
}

static void test_relay_repeat_is_not_republished(void)
{
    outputs_known_state();

    fake_deliver("relay/1/set", "ON");
    relay_loop();
    TEST_ASSERT_EQUAL_UINT(1, fake_publishes);

    /* A broker replays its retained commands on every reconnect. That must
     * not turn into traffic of its own. */
    fake_deliver("relay/1/set", "ON");
    relay_loop();
    TEST_ASSERT_EQUAL_UINT(1, fake_publishes);
}

static void test_relay_republishes_after_a_reconnect(void)
{
    outputs_known_state();

    fake_deliver("relay/1/set", "ON");
    relay_loop();
    fake_publishes = 0;

    /* A broker that has just come back holds none of the last session's
     * retained messages, so all three have to be said again -- including the
     * two that have not changed since. */
    broker_reconnects();

    TEST_ASSERT_EQUAL_STRING("ON", published("relay/1"));
    TEST_ASSERT_EQUAL_STRING("OFF", published("relay/2"));
    TEST_ASSERT_EQUAL_STRING("OFF", published("relay/3"));
}

static void test_relay_switches_while_offline(void)
{
    outputs_known_state();
    fake_connected = false;

    /* A command that arrived just before the link dropped is still applied --
     * the pin is the point, the topic is the report. */
    fake_deliver("relay/1/set", "ON");
    relay_loop();

    TEST_ASSERT_EQUAL_INT(1, fake_relay[0]);
    TEST_ASSERT_EQUAL_UINT(0, fake_publishes);

    fake_connected = true;
    relay_loop();

    TEST_ASSERT_EQUAL_STRING("ON", published("relay/1"));
}

static void test_relay_failed_publish_is_retried(void)
{
    outputs_known_state();

    fake_publish_ok = false;
    fake_deliver("relay/1/set", "ON");
    relay_loop();

    TEST_ASSERT_EQUAL_INT(1, fake_relay[0]);
    TEST_ASSERT_NULL(published("relay/1"));

    fake_publish_ok = true;
    relay_loop();

    TEST_ASSERT_EQUAL_STRING("ON", published("relay/1"));
}

/* -------------------------------------------------------------------- LEDs */

static void test_led_setup_claims_its_pins_and_topic(void)
{
    fake_reset();
    led_setup();

    TEST_ASSERT_TRUE(fake_led_inited);
    TEST_ASSERT_EQUAL_UINT(1, fake_subs);
    TEST_ASSERT_EQUAL_STRING("led/+/set", fake_filter[0]);
}

static void test_led_publishes_by_name(void)
{
    outputs_known_state();
    broker_reconnects();

    /* The board's names, not indices: port_led_name() is what a topic is
     * built from, so "led/0" is never published by anything. */
    TEST_ASSERT_EQUAL_STRING("0", published("led/red"));
    TEST_ASSERT_EQUAL_STRING("0", published("led/green"));
    TEST_ASSERT_EQUAL_STRING("0", published("led/blue"));
    TEST_ASSERT_NULL(published("led/0"));
}

static void test_led_percentages(void)
{
    outputs_known_state();

    fake_deliver("led/green/set", "42");
    led_loop();

    TEST_ASSERT_EQUAL_INT(42, fake_led[1]);
    TEST_ASSERT_EQUAL_STRING("42", published("led/green"));

    /* Out of range is clamped rather than wrapped. */
    fake_deliver("led/green/set", "255");
    TEST_ASSERT_EQUAL_INT(100, fake_led[1]);

    fake_deliver("led/green/set", " 7 ");
    TEST_ASSERT_EQUAL_INT(7, fake_led[1]);
}

static void test_led_number_beats_the_word(void)
{
    outputs_known_state();

    /* The one payload where the two readings disagree: as a switch "1" is on,
     * as a dimmer it is one percent. A dimmer that jumped to full when asked
     * for its lowest setting would be useless. */
    fake_deliver("led/red/set", "1");
    TEST_ASSERT_EQUAL_INT(1, fake_led[0]);

    fake_deliver("led/red/set", "ON");
    TEST_ASSERT_EQUAL_INT(100, fake_led[0]);

    fake_deliver("led/red/set", "OFF");
    TEST_ASSERT_EQUAL_INT(0, fake_led[0]);

    fake_deliver("led/red/set", "banana");
    TEST_ASSERT_EQUAL_INT(0, fake_led[0]);
}

static void test_led_unknown_name_is_ignored(void)
{
    outputs_known_state();

    fake_deliver("led/purple/set", "100");
    fake_deliver("led/re/set", "100");
    fake_deliver("led/reddish/set", "100");
    led_loop();

    for (unsigned i = 0; i < FAKE_OUTPUTS; i++)
        TEST_ASSERT_EQUAL_INT(-1, fake_led[i]);

    TEST_ASSERT_EQUAL_UINT(0, fake_publishes);
}

static void test_led_republishes_after_a_reconnect(void)
{
    outputs_known_state();

    fake_deliver("led/blue/set", "60");
    led_loop();
    fake_publishes = 0;

    broker_reconnects();

    TEST_ASSERT_EQUAL_STRING("0", published("led/red"));
    TEST_ASSERT_EQUAL_STRING("0", published("led/green"));
    TEST_ASSERT_EQUAL_STRING("60", published("led/blue"));
}

static void test_led_and_relay_do_not_share_topics(void)
{
    outputs_known_state();

    fake_deliver("relay/1/set", "ON");
    fake_deliver("led/red/set", "100");
    relay_loop();
    led_loop();

    TEST_ASSERT_EQUAL_INT(1, fake_relay[0]);
    TEST_ASSERT_EQUAL_INT(100, fake_led[0]);

    /* Two modules, two subtrees, no overlap: the relay never publishes an
     * led/ topic and the LED never publishes a relay/ one. */
    TEST_ASSERT_TRUE(published_index("relay/1") >= 0);
    TEST_ASSERT_TRUE(published_index("led/red") >= 0);
    TEST_ASSERT_NULL(published("led/1"));
    TEST_ASSERT_NULL(published("relay/red"));
}

void test_outputs_run(void)
{
    RUN_TEST(test_relay_setup_claims_its_pins_and_topic);
    RUN_TEST(test_relay_announces_all_of_them);
    RUN_TEST(test_relay_on_and_off);
    RUN_TEST(test_relay_payload_spellings);
    RUN_TEST(test_relay_toggle);
    RUN_TEST(test_relay_out_of_range_is_ignored);
    RUN_TEST(test_relay_repeat_is_not_republished);
    RUN_TEST(test_relay_republishes_after_a_reconnect);
    RUN_TEST(test_relay_switches_while_offline);
    RUN_TEST(test_relay_failed_publish_is_retried);

    RUN_TEST(test_led_setup_claims_its_pins_and_topic);
    RUN_TEST(test_led_publishes_by_name);
    RUN_TEST(test_led_percentages);
    RUN_TEST(test_led_number_beats_the_word);
    RUN_TEST(test_led_unknown_name_is_ignored);
    RUN_TEST(test_led_republishes_after_a_reconnect);
    RUN_TEST(test_led_and_relay_do_not_share_topics);
}
