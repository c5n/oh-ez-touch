/* Unit tests for Item::applyState().
 *
 * The half of the old Item::update() that decides what a poll changed, now
 * that the half that waited on a socket lives on the client task. It is
 * reached with a body straight off the network, so it takes a length rather
 * than a terminated string -- and it is what the connection-error watchdog
 * ultimately counts, through the "did this change" answer it returns.
 */

#include <unity.h>

#include "openhab/openhab_connector.hpp"
#include "test_suites.hpp"

#include <stdlib.h>
#include <string.h>

static Item make_item(enum ItemType type, const char *state)
{
    Item item;

    item.cleanItem();
    item.setType(type);
    item.setStateText(state);

    return item;
}

/* A text state is stored as it arrives, and only a difference counts. */
static void test_string_state_change_is_reported_once(void)
{
    Item item = make_item(ItemType::type_string, "idle");

    TEST_ASSERT_EQUAL_INT(1, item.applyState("ringing", strlen("ringing")));
    TEST_ASSERT_EQUAL_STRING("ringing", item.getStateText());

    /* The same reading again is not a change, which is what stops the UI
     * redrawing and re-fetching an icon every five seconds. */
    TEST_ASSERT_EQUAL_INT(0, item.applyState("ringing", strlen("ringing")));
}

/* openHAB appends the unit to a dimensioned number. The unit comes off and the
 * value is re-printed through the same "%f" setStateNumber() uses, so that an
 * unchanged reading compares equal as text.
 *
 * Both spellings are checked now, against a Number:Temperature added to the
 * 5.2.1 instance for exactly this question, and they are not equals:
 *
 *   "21.5 °C"    is what it emits. /rest/items/<n>/state answers those bytes
 *                verbatim, degree sign and all, and the panel shows 21.5 °C.
 *   "22.5 degC"  it will not take at all -- POSTing it is a 400, and no
 *                endpoint on that server produces it.
 *
 * So the degC case below is defensive and nothing more: it costs one line, and
 * strtof() stopping at the first non-numeric byte is what makes both work. The
 * one to keep honest is the degree-sign case. */
static void test_numeric_state_loses_its_unit(void)
{
    Item item = make_item(ItemType::type_number, "");

    TEST_ASSERT_EQUAL_INT(1, item.applyState("21.5 degC", strlen("21.5 degC")));
    TEST_ASSERT_EQUAL_STRING("21.500000", item.getStateText());
    TEST_ASSERT_EQUAL_FLOAT(21.5f, item.getStateNumber());
}

/* And the point of that: the same reading in a different spelling is still the
 * same reading. "21.5 degC" and "21.50 degC" must both compare equal to what
 * is already stored, or every poll would look like a change. */
static void test_equivalent_numeric_state_is_not_a_change(void)
{
    Item item = make_item(ItemType::type_number, "");

    TEST_ASSERT_EQUAL_INT(1, item.applyState("21.5 degC", strlen("21.5 degC")));
    TEST_ASSERT_EQUAL_INT(0, item.applyState("21.50 degC", strlen("21.50 degC")));
    TEST_ASSERT_EQUAL_INT(0, item.applyState("21.5 °C", strlen("21.5 °C")));
    TEST_ASSERT_EQUAL_INT(1, item.applyState("21.6 degC", strlen("21.6 degC")));
}

/* Setpoints and sliders take the same treatment as plain numbers. */
static void test_setpoint_and_slider_are_numeric_too(void)
{
    Item setpoint = make_item(ItemType::type_setpoint, "");
    Item slider = make_item(ItemType::type_slider, "");

    TEST_ASSERT_EQUAL_INT(1, setpoint.applyState("18 °C", strlen("18 °C")));
    TEST_ASSERT_EQUAL_STRING("18.000000", setpoint.getStateText());

    TEST_ASSERT_EQUAL_INT(1, slider.applyState("75", strlen("75")));
    TEST_ASSERT_EQUAL_STRING("75.000000", slider.getStateText());
}

/* What a plain Number really looks like on the wire, captured from openHAB
 * 5.2.1: /rest/items/OHEZTOUCH_Number/state answers "10.000000" -- six
 * decimals and no unit, whatever the item's display pattern says. So the
 * re-printing is not only for stripping units; it is what makes the value the
 * parser stored out of the sitemap page compare equal to the one the poll
 * brings back. */
static void test_a_plain_number_state_is_six_decimals(void)
{
    Item item = make_item(ItemType::type_number, "");

    TEST_ASSERT_EQUAL_INT(1, item.applyState("10.000000", strlen("10.000000")));
    TEST_ASSERT_EQUAL_STRING("10.000000", item.getStateText());

    /* The same reading, and the same reading spelled shorter, are both the
     * value already held. */
    TEST_ASSERT_EQUAL_INT(0, item.applyState("10.000000", strlen("10.000000")));
    TEST_ASSERT_EQUAL_INT(0, item.applyState("10", strlen("10")));
    TEST_ASSERT_EQUAL_INT(0, item.applyState("10.0", strlen("10.0")));
}

/* openHAB answers the four characters "NULL" for an item it has no value for,
 * and it is an ordinary poll result rather than an error: the tile shows it.
 *
 * Confirmed against 5.2.1, where the Player and Rollershutter items read NULL
 * until something first commanded them. */
static void test_a_null_state_is_ordinary_text(void)
{
    Item item = make_item(ItemType::type_switch, "ON");

    TEST_ASSERT_EQUAL_INT(1, item.applyState("NULL", strlen("NULL")));
    TEST_ASSERT_EQUAL_STRING("NULL", item.getStateText());

    /* Still NULL next time round, and still not a change. */
    TEST_ASSERT_EQUAL_INT(0, item.applyState("NULL", strlen("NULL")));

    /* "UNDEF" is the other one openHAB uses, and is no more special. */
    TEST_ASSERT_EQUAL_INT(1, item.applyState("UNDEF", strlen("UNDEF")));
    TEST_ASSERT_EQUAL_STRING("UNDEF", item.getStateText());
}

/* On a numeric type the same answer goes through strtof(), which reads nothing
 * from it and yields zero. An uninitialised setpoint therefore reads 0.0 on
 * the glass while openHAB's own label for it says "- °C".
 *
 * Pinned rather than endorsed -- this is the line to change if a dash is
 * wanted instead -- and worth knowing that NULL is then indistinguishable from
 * a real zero. */
static void test_a_null_state_on_a_number_is_zero(void)
{
    Item item = make_item(ItemType::type_setpoint, "");

    TEST_ASSERT_EQUAL_INT(1, item.applyState("NULL", strlen("NULL")));
    TEST_ASSERT_EQUAL_STRING("0.000000", item.getStateText());
    TEST_ASSERT_EQUAL_FLOAT(0.0f, item.getStateNumber());

    /* And a genuine zero arriving afterwards is not reported as a change,
     * because it is not one as far as the item can tell. */
    TEST_ASSERT_EQUAL_INT(0, item.applyState("0", strlen("0")));
}

/* The dimensioned form, captured verbatim from openHAB 5.2.1: a
 * Number:Temperature holding 21.5 °C answers "21.5 °C" on /state, and the
 * degree sign is two UTF-8 bytes that strtof() has to stop in front of. */
static void test_a_dimensioned_state_loses_its_degree_sign(void)
{
    Item item = make_item(ItemType::type_number, "");

    TEST_ASSERT_EQUAL_INT(1, item.applyState("21.5 °C", strlen("21.5 °C")));
    TEST_ASSERT_EQUAL_STRING("21.500000", item.getStateText());
    TEST_ASSERT_EQUAL_FLOAT(21.5f, item.getStateNumber());

    /* The same reading again is not a change, which is the whole point of
     * re-printing it. */
    TEST_ASSERT_EQUAL_INT(0, item.applyState("21.5 °C", strlen("21.5 °C")));
}

/* Number:Dimensionless is the one numeric item whose state the panel cannot
 * render from the state alone, and this pins how far off it is.
 *
 * Measured on openHAB 5.2.1. Commanding such an item "48 %" makes it store the
 * *ratio*: /state answers "0.48", while the sitemap label openHAB builds for
 * the same item says "48 %". The panel keeps 0.480000 and prints it through
 * the item's own "%d %%" pattern, so the tile reads "0 %" -- confirmed on the
 * glass, not merely derived.
 *
 * Going the other way is no better: a bare "48" is stored as "48" and the
 * panel then reads "48 %", but openHAB's label for it says "4800 %".
 *
 * Closing this needs the unit the panel currently drops -- openHAB sends it
 * twice, as the widget's "unit" and as the item's "unitSymbol", and the
 * connector reads neither. Pinned as today's behaviour, not endorsed. */
static void test_a_dimensionless_percentage_is_stored_as_its_ratio(void)
{
    Item item = make_item(ItemType::type_number, "");

    TEST_ASSERT_EQUAL_INT(1, item.applyState("0.48", strlen("0.48")));
    TEST_ASSERT_EQUAL_STRING("0.480000", item.getStateText());

    /* What a "%d %%" pattern makes of that is the "0 %" seen on the tile. */
    TEST_ASSERT_EQUAL_UINT16(0, (uint16_t)item.getStateNumber());

    /* And the other spelling, which the panel gets right and openHAB labels
     * as 4800 %. */
    TEST_ASSERT_EQUAL_INT(1, item.applyState("48", strlen("48")));
    TEST_ASSERT_EQUAL_UINT16(48, (uint16_t)item.getStateNumber());
}

/* A switch is not a number, so its state is kept verbatim. */
static void test_switch_state_is_verbatim(void)
{
    Item item = make_item(ItemType::type_switch, "OFF");

    TEST_ASSERT_EQUAL_INT(1, item.applyState("ON", strlen("ON")));
    TEST_ASSERT_EQUAL_STRING("ON", item.getStateText());
}

/* An over-long state truncates cleanly into the fixed field, as
 * HTTPClient::getString() plus strlcpy() did. STR_STATE_TEXT_LEN is 32, so 31
 * characters and a terminator survive. */
static void test_over_long_state_truncates(void)
{
    Item item = make_item(ItemType::type_string, "");
    static const char *const LONG =
        "0123456789012345678901234567890123456789012345678901234567890123456789";

    TEST_ASSERT_EQUAL_INT(1, item.applyState(LONG, strlen(LONG)));
    TEST_ASSERT_EQUAL_UINT(STR_STATE_TEXT_LEN - 1, strlen(item.getStateText()));
    TEST_ASSERT_EQUAL_STRING_LEN(LONG, item.getStateText(), STR_STATE_TEXT_LEN - 1);
}

/* The reason for the explicit length: a body off the network carries no
 * terminator, so applyState() must read exactly `len` bytes and no further.
 * The buffer here is deliberately not terminated, and is followed by text that
 * would show up in the result if it were read.
 *
 * Heap-allocated so that a read past the end is something the allocator can be
 * asked about, rather than a stack neighbour that happens to be harmless. */
static void test_state_need_not_be_terminated(void)
{
    Item item = make_item(ItemType::type_string, "");
    static const char raw[] = { 'O', 'N', 'X', 'X', 'X', 'X', 'X', 'X' };
    char *body = (char *)malloc(sizeof(raw));

    TEST_ASSERT_NOT_NULL(body);
    memcpy(body, raw, sizeof(raw));

    TEST_ASSERT_EQUAL_INT(1, item.applyState(body, 2));
    free(body);

    TEST_ASSERT_EQUAL_STRING("ON", item.getStateText());
}

/* An empty body is a state of its own -- openHAB answers "NULL" for an
 * uninitialised item, but a zero-length response must not walk off the front
 * of the buffer either. */
static void test_empty_state(void)
{
    Item item = make_item(ItemType::type_string, "something");

    TEST_ASSERT_EQUAL_INT(1, item.applyState("", 0));
    TEST_ASSERT_EQUAL_STRING("", item.getStateText());
    TEST_ASSERT_EQUAL_INT(0, item.applyState("", 0));
}

/* ------------------------------------------------------- the colour state */

/* A colorpicker's state is "h,s,v". This used to be parsed by two identical
 * copies in the UI, both of which restarted at endptr + 1 without checking
 * that endptr was not the terminator -- so every case below that stops early
 * read past the end of the item's state buffer. */
static void test_hsv_state_is_parsed(void)
{
    Item     item;
    uint16_t h;
    uint8_t  s, v;

    item.setStateText("120,50,75");
    TEST_ASSERT_TRUE(item.getStateHsv(&h, &s, &v));
    TEST_ASSERT_EQUAL_UINT16(120, h);
    TEST_ASSERT_EQUAL_UINT8(50, s);
    TEST_ASSERT_EQUAL_UINT8(75, v);

    /* All three at their limits, which is what a fully saturated red is. */
    item.setStateText("359,100,100");
    TEST_ASSERT_TRUE(item.getStateHsv(&h, &s, &v));
    TEST_ASSERT_EQUAL_UINT16(359, h);
    TEST_ASSERT_EQUAL_UINT8(100, s);
    TEST_ASSERT_EQUAL_UINT8(100, v);

    item.setStateText("0,0,0");
    TEST_ASSERT_TRUE(item.getStateHsv(&h, &s, &v));
    TEST_ASSERT_EQUAL_UINT16(0, h);
    TEST_ASSERT_EQUAL_UINT8(0, s);
    TEST_ASSERT_EQUAL_UINT8(0, v);
}

/* Every one of these truncates the triple somewhere. The old parser walked
 * off the end of the buffer on all of them; this one has to refuse and leave
 * black behind. */
static void test_a_short_hsv_state_is_refused(void)
{
    static const char *const truncated[] = {
        "",        /* an item openHAB has no value for at all */
        "0",       /* one field, and endptr is the terminator */
        "120",
        "120,",    /* a separator with nothing after it       */
        "120,50",  /* two of three                            */
        "120,50,", /* three fields, the last one empty        */
        "NULL",    /* what openHAB sends for an undefined item */
        "UNDEF",
        "ON",
        ",,",
    };

    for (size_t i = 0; i < sizeof(truncated) / sizeof(truncated[0]); i++)
    {
        Item     item;
        uint16_t h = 1;
        uint8_t  s = 1, v = 1;

        item.setStateText(truncated[i]);

        TEST_ASSERT_FALSE_MESSAGE(item.getStateHsv(&h, &s, &v), truncated[i]);

        /* Not merely "false": the outputs have to be defined, because the
         * caller paints a swatch with them whatever the answer was. */
        TEST_ASSERT_EQUAL_UINT16(0, h);
        TEST_ASSERT_EQUAL_UINT8(0, s);
        TEST_ASSERT_EQUAL_UINT8(0, v);
    }
}

/* The shape the old parser actually failed on, kept as a regression: a long
 * state followed by a short one leaves digits behind in the fixed field, and
 * restarting at endptr + 1 picked them up. So the swatch of an item that has
 * gone to NULL must not inherit the colour of the item before it. */
static void test_a_short_hsv_state_does_not_see_the_previous_one(void)
{
    Item     item;
    uint16_t h = 1;
    uint8_t  s = 1, v = 1;

    item.setStateText("111,22,33");
    TEST_ASSERT_TRUE(item.getStateHsv(&h, &s, &v));
    TEST_ASSERT_EQUAL_UINT16(111, h);

    /* Shorter than what it replaces, so ",22,33" is still sitting in the
     * field past the new terminator. */
    item.setStateText("7");

    TEST_ASSERT_FALSE(item.getStateHsv(&h, &s, &v));
    TEST_ASSERT_EQUAL_UINT16(0, h);
    TEST_ASSERT_EQUAL_UINT8(0, s);
    TEST_ASSERT_EQUAL_UINT8(0, v);
}

/* A server being loose with a value still means something, so it is clamped
 * rather than refused -- lv_color_hsv_to_rgb() range checks none of these. */
static void test_an_out_of_range_hsv_state_is_clamped(void)
{
    Item     item;
    uint16_t h;
    uint8_t  s, v;

    item.setStateText("400,150,999");
    TEST_ASSERT_TRUE(item.getStateHsv(&h, &s, &v));
    TEST_ASSERT_EQUAL_UINT16(359, h);
    TEST_ASSERT_EQUAL_UINT8(100, s);
    TEST_ASSERT_EQUAL_UINT8(100, v);

    item.setStateText("-30,-1,-100");
    TEST_ASSERT_TRUE(item.getStateHsv(&h, &s, &v));
    TEST_ASSERT_EQUAL_UINT16(0, h);
    TEST_ASSERT_EQUAL_UINT8(0, s);
    TEST_ASSERT_EQUAL_UINT8(0, v);
}

void test_item_state_run(void)
{
    RUN_TEST(test_string_state_change_is_reported_once);
    RUN_TEST(test_numeric_state_loses_its_unit);
    RUN_TEST(test_equivalent_numeric_state_is_not_a_change);
    RUN_TEST(test_setpoint_and_slider_are_numeric_too);
    RUN_TEST(test_a_plain_number_state_is_six_decimals);
    RUN_TEST(test_a_dimensioned_state_loses_its_degree_sign);
    RUN_TEST(test_a_dimensionless_percentage_is_stored_as_its_ratio);
    RUN_TEST(test_a_null_state_is_ordinary_text);
    RUN_TEST(test_a_null_state_on_a_number_is_zero);
    RUN_TEST(test_switch_state_is_verbatim);
    RUN_TEST(test_over_long_state_truncates);
    RUN_TEST(test_state_need_not_be_terminated);
    RUN_TEST(test_empty_state);
    RUN_TEST(test_hsv_state_is_parsed);
    RUN_TEST(test_a_short_hsv_state_is_refused);
    RUN_TEST(test_an_out_of_range_hsv_state_is_clamped);
    RUN_TEST(test_a_short_hsv_state_does_not_see_the_previous_one);
}
