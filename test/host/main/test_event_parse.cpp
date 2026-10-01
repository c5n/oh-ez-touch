/* Unit tests for openhab_event_parse() and the SSE line splitter.
 *
 * The events are the shapes openHAB 5 sends on /rest/events, copied from
 * SseResource and ItemEventFactory: an outer object whose "payload" is itself
 * JSON, encoded as a string.
 */

#include <unity.h>

#include "openhab/openhab_event_parse.hpp"
#include "test_suites.hpp"

#include <string.h>

static char name[OPENHAB_EVENT_NAME_LEN];
static char value[32];

static enum openhab_event_e parse(const char *json)
{
    name[0] = '\0';
    value[0] = '\0';

    return openhab_event_parse(json, strlen(json), name, sizeof(name), value, sizeof(value));
}

static void test_state_changed(void)
{
    TEST_ASSERT_EQUAL(OPENHAB_EVENT_STATE, parse(
        "{\"topic\":\"openhab/items/Kitchen_Light/statechanged\","
        "\"payload\":\"{\\\"type\\\":\\\"OnOff\\\",\\\"value\\\":\\\"ON\\\","
        "\\\"oldType\\\":\\\"OnOff\\\",\\\"oldValue\\\":\\\"OFF\\\"}\","
        "\"type\":\"ItemStateChangedEvent\"}"));
    TEST_ASSERT_EQUAL_STRING("Kitchen_Light", name);
    TEST_ASSERT_EQUAL_STRING("ON", value);
}

/* The group is the item whose state changed; the member is why. */
static void test_group_state_changed_names_the_group(void)
{
    TEST_ASSERT_EQUAL(OPENHAB_EVENT_STATE, parse(
        "{\"topic\":\"openhab/items/gLights/Kitchen_Light/statechanged\","
        "\"payload\":\"{\\\"type\\\":\\\"OnOff\\\",\\\"value\\\":\\\"ON\\\"}\","
        "\"type\":\"GroupItemStateChangedEvent\"}"));
    TEST_ASSERT_EQUAL_STRING("gLights", name);
    TEST_ASSERT_EQUAL_STRING("ON", value);
}

/* A quantity, with its unit and a non-ASCII character, both as UTF-8 and as
 * the \u escape a JSON writer is free to use instead. */
static void test_quantity(void)
{
    TEST_ASSERT_EQUAL(OPENHAB_EVENT_STATE, parse(
        "{\"topic\":\"openhab/items/Temp/statechanged\","
        "\"payload\":\"{\\\"type\\\":\\\"Quantity\\\",\\\"value\\\":\\\"21.5 \xc2\xb0" "C\\\"}\","
        "\"type\":\"ItemStateChangedEvent\"}"));
    TEST_ASSERT_EQUAL_STRING("21.5 \xc2\xb0" "C", value);

    TEST_ASSERT_EQUAL(OPENHAB_EVENT_STATE, parse(
        "{\"topic\":\"openhab/items/Temp/statechanged\","
        "\"payload\":\"{\\\"type\\\":\\\"Quantity\\\",\\\"value\\\":\\\"21.5 \\\\u00b0C\\\"}\","
        "\"type\":\"ItemStateChangedEvent\"}"));
    TEST_ASSERT_EQUAL_STRING("21.5 \xc2\xb0" "C", value);
}

static void test_null_and_undef(void)
{
    TEST_ASSERT_EQUAL(OPENHAB_EVENT_STATE, parse(
        "{\"topic\":\"openhab/items/A/statechanged\","
        "\"payload\":\"{\\\"type\\\":\\\"UnDef\\\",\\\"value\\\":\\\"NULL\\\"}\","
        "\"type\":\"ItemStateChangedEvent\"}"));
    TEST_ASSERT_EQUAL_STRING("NULL", value);

    TEST_ASSERT_EQUAL(OPENHAB_EVENT_STATE, parse(
        "{\"topic\":\"openhab/items/A/statechanged\","
        "\"payload\":\"{\\\"type\\\":\\\"UnDef\\\",\\\"value\\\":\\\"UNDEF\\\"}\","
        "\"type\":\"ItemStateChangedEvent\"}"));
    TEST_ASSERT_EQUAL_STRING("UNDEF", value);
}

/* openHAB 2 called the root segment "smarthome". */
static void test_smarthome_root(void)
{
    TEST_ASSERT_EQUAL(OPENHAB_EVENT_STATE, parse(
        "{\"topic\":\"smarthome/items/Old_Item/statechanged\","
        "\"payload\":\"{\\\"type\\\":\\\"Decimal\\\",\\\"value\\\":\\\"3\\\"}\","
        "\"type\":\"ItemStateChangedEvent\"}"));
    TEST_ASSERT_EQUAL_STRING("Old_Item", name);
    TEST_ASSERT_EQUAL_STRING("3", value);
}

static void test_alive(void)
{
    TEST_ASSERT_EQUAL(OPENHAB_EVENT_ALIVE, parse("{\"type\":\"ALIVE\",\"interval\":10}"));
}

/* A plain update, or a command, says nothing about a change. */
static void test_other_events(void)
{
    TEST_ASSERT_EQUAL(OPENHAB_EVENT_OTHER, parse(
        "{\"topic\":\"openhab/items/A/command\","
        "\"payload\":\"{\\\"type\\\":\\\"OnOff\\\",\\\"value\\\":\\\"ON\\\"}\","
        "\"type\":\"ItemCommandEvent\"}"));
    TEST_ASSERT_EQUAL(OPENHAB_EVENT_OTHER, parse(
        "{\"topic\":\"openhab/items/A/stateupdated\","
        "\"payload\":\"{\\\"type\\\":\\\"OnOff\\\",\\\"value\\\":\\\"ON\\\"}\","
        "\"type\":\"ItemStateUpdatedEvent\"}"));
}

static void test_malformed(void)
{
    TEST_ASSERT_EQUAL(OPENHAB_EVENT_INVALID, parse(""));
    TEST_ASSERT_EQUAL(OPENHAB_EVENT_INVALID, parse("not json"));
    TEST_ASSERT_EQUAL(OPENHAB_EVENT_INVALID, parse("{\"topic\":\"openhab/items/A/statechanged\""));
    /* No "type". */
    TEST_ASSERT_EQUAL(OPENHAB_EVENT_INVALID, parse("{\"topic\":\"openhab/items/A/statechanged\"}"));
    /* A payload that is not JSON. */
    TEST_ASSERT_EQUAL(OPENHAB_EVENT_INVALID, parse(
        "{\"topic\":\"openhab/items/A/statechanged\",\"payload\":\"ON\","
        "\"type\":\"ItemStateChangedEvent\"}"));
    /* A topic with no item in it. */
    TEST_ASSERT_EQUAL(OPENHAB_EVENT_INVALID, parse(
        "{\"topic\":\"openhab/things/A/statechanged\","
        "\"payload\":\"{\\\"value\\\":\\\"ON\\\"}\",\"type\":\"ItemStateChangedEvent\"}"));
    TEST_ASSERT_EQUAL(OPENHAB_EVENT_INVALID, parse(
        "{\"topic\":\"openhab/items//statechanged\","
        "\"payload\":\"{\\\"value\\\":\\\"ON\\\"}\",\"type\":\"ItemStateChangedEvent\"}"));
}

/* Cut to the field like a polled state, always terminated. */
static void test_long_value_is_truncated(void)
{
    TEST_ASSERT_EQUAL(OPENHAB_EVENT_STATE, parse(
        "{\"topic\":\"openhab/items/S/statechanged\","
        "\"payload\":\"{\\\"type\\\":\\\"String\\\",\\\"value\\\":"
        "\\\"0123456789012345678901234567890123456789\\\"}\","
        "\"type\":\"ItemStateChangedEvent\"}"));
    TEST_ASSERT_EQUAL_STRING("0123456789012345678901234567890", value);
}

/* --- the line splitter ------------------------------------------------- */

static char lines[4][SSE_LINE_LEN];
static size_t line_count;

static void collect(const char *data, size_t len, void *ctx)
{
    (void)ctx;

    if (line_count < 4)
    {
        memcpy(lines[line_count], data, len);
        lines[line_count][len] = '\0';
    }

    line_count++;
}

static void feed(struct sse_reader *r, const char *s)
{
    sse_reader_feed(r, s, strlen(s), collect, NULL);
}

static void test_reader_data_lines_only(void)
{
    struct sse_reader r;

    sse_reader_reset(&r);
    line_count = 0;

    feed(&r, "event: alive\ndata: {\"type\":\"ALIVE\"}\n\n: comment\nid: 3\ndata:x\n\n");

    TEST_ASSERT_EQUAL(2, line_count);
    TEST_ASSERT_EQUAL_STRING("{\"type\":\"ALIVE\"}", lines[0]);
    TEST_ASSERT_EQUAL_STRING("x", lines[1]);
}

/* A socket read ends wherever it likes, CRLF included. */
static void test_reader_split_across_reads(void)
{
    struct sse_reader r;

    sse_reader_reset(&r);
    line_count = 0;

    feed(&r, "da");
    feed(&r, "ta: hel");
    TEST_ASSERT_EQUAL(0, line_count);
    feed(&r, "lo\r");
    feed(&r, "\n\r\ndata: two\r\n");

    TEST_ASSERT_EQUAL(2, line_count);
    TEST_ASSERT_EQUAL_STRING("hello", lines[0]);
    TEST_ASSERT_EQUAL_STRING("two", lines[1]);
}

/* An over-long line is dropped whole, and the next one is read as usual. */
static void test_reader_overflow(void)
{
    struct sse_reader r;
    static char big[SSE_LINE_LEN + 64];

    sse_reader_reset(&r);
    line_count = 0;

    memset(big, 'a', sizeof(big) - 1);
    big[sizeof(big) - 1] = '\0';
    memcpy(big, "data:", 5);

    feed(&r, big);
    feed(&r, "\ndata: next\n");

    TEST_ASSERT_EQUAL(1, line_count);
    TEST_ASSERT_EQUAL_STRING("next", lines[0]);
}

void test_event_parse_run(void)
{
    RUN_TEST(test_state_changed);
    RUN_TEST(test_group_state_changed_names_the_group);
    RUN_TEST(test_quantity);
    RUN_TEST(test_null_and_undef);
    RUN_TEST(test_smarthome_root);
    RUN_TEST(test_alive);
    RUN_TEST(test_other_events);
    RUN_TEST(test_malformed);
    RUN_TEST(test_long_value_is_truncated);
    RUN_TEST(test_reader_data_lines_only);
    RUN_TEST(test_reader_split_across_reads);
    RUN_TEST(test_reader_overflow);
}
