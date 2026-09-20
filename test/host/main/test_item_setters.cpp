/* Unit tests for the string setters of Item.
 *
 * The setters copy strings of unknown length straight out of the sitemap JSON
 * that openHAB serves, so a label, state or mapping longer than the fixed
 * destination must truncate cleanly. They used strncpy(), which does not
 * terminate the destination when the source fills it, so an over-long value
 * left the field running into whatever followed it in memory. These tests pin
 * down the strlcpy() behaviour that replaced it.
 *
 * Host-only; the setters are inline in openhab_connector.hpp, so nothing from
 * main/ needs to be linked. Run with:
 *   cd test/host && idf.py build && ./build/oh-ez-touch-host-test.elf
 */

#include <unity.h>

#include "openhab/openhab_connector.hpp"
#include "test_suites.hpp"

#include <stddef.h>
#include <string.h>

/* Longer than every destination in Item. */
static const char *const LONG_VALUE =
    "0123456789012345678901234567890123456789012345678901234567890123456789"
    "0123456789012345678901234567890123456789012345678901234567890123456789";

#define CANARY_BYTE 0xAA
#define CANARY_LEN  16

/* Item followed by a canary, so that a copy running past the end of the last
 * field is visible. The whole probe is poisoned rather than zeroed on purpose:
 * against a zero-filled object an unterminated copy would still look
 * terminated, and the regression these tests guard against would pass. */
struct Probe
{
    Item item;
    unsigned char canary[CANARY_LEN];
};

static void poison(Probe &probe)
{
    /* Filled byte by byte rather than with memset(): Item is not a trivial
     * type, and -Wclass-memaccess rightly objects to memset() on one. */
    unsigned char *bytes = reinterpret_cast<unsigned char *>(&probe);

    for (size_t i = 0; i < sizeof(probe); ++i)
        bytes[i] = CANARY_BYTE;
}

static void check_canary(Probe &probe)
{
    for (size_t i = 0; i < CANARY_LEN; ++i)
        TEST_ASSERT_EQUAL_HEX8_MESSAGE(CANARY_BYTE, probe.canary[i],
                                       "a setter wrote past the end of Item");
}

/* One entry per string field, so a failure names the field that broke. */
struct FieldCase
{
    const char *name;
    void (*set)(Item &item, const char *value);
    const char *(*get)(Item &item);
    size_t capacity;
};

static const FieldCase FIELD_CASES[] = {
    { "label",
      [](Item &i, const char *v) { i.setLabel(v); },
      [](Item &i) -> const char * { return i.getLabel(); },
      STR_LABEL_LEN },
    { "icon_name",
      [](Item &i, const char *v) { i.setIconName(v); },
      [](Item &i) -> const char * { return i.getIconName(); },
      STR_ICON_NAME_LEN },
    { "state_text",
      [](Item &i, const char *v) { i.setStateText(v); },
      [](Item &i) -> const char * { return i.getStateText(); },
      STR_STATE_TEXT_LEN },
    { "transformedstate_text",
      [](Item &i, const char *v) { i.setTransformedStateText(v); },
      [](Item &i) -> const char * { return i.getTransformedStateText(); },
      STR_TRANSFORMEDSTATE_TEXT_LEN },
    { "pattern",
      [](Item &i, const char *v) { i.setNumberPattern(v); },
      [](Item &i) -> const char * { return i.getNumberPattern(); },
      STR_PATTERN_LEN },
    { "link",
      [](Item &i, const char *v) { i.setLink(v); },
      [](Item &i) -> const char * { return i.getLink(); },
      STR_LINK_LEN },
    { "page_link",
      [](Item &i, const char *v) { i.setPageLink(v); },
      [](Item &i) -> const char * { return i.getPageLink(); },
      STR_LINK_LEN },
    { "selection_label",
      [](Item &i, const char *v) { i.setSelectionLabel(0, v); },
      [](Item &i) -> const char * { return i.getSelectionLabel(0); },
      ITEM_SELECTION_LABEL_LEN_MAX },
    { "selection_command",
      [](Item &i, const char *v) { i.setSelectionCommand(0, v); },
      [](Item &i) -> const char * { return i.getSelectionCommand(0); },
      ITEM_SELECTION_COMMAND_LEN_MAX },
};

#define FIELD_CASE_COUNT (sizeof(FIELD_CASES) / sizeof(FIELD_CASES[0]))

/* An over-long value truncates to capacity - 1 and stays terminated. Before
 * the strlcpy() change, strlen() ran hundreds of bytes past every field. */
static void test_over_long_values_truncate_and_terminate(void)
{
    Probe probe;

    poison(probe);

    for (size_t i = 0; i < FIELD_CASE_COUNT; ++i)
        FIELD_CASES[i].set(probe.item, LONG_VALUE);

    for (size_t i = 0; i < FIELD_CASE_COUNT; ++i)
        TEST_ASSERT_EQUAL_UINT_MESSAGE((unsigned)(FIELD_CASES[i].capacity - 1),
                                       (unsigned)strlen(FIELD_CASES[i].get(probe.item)),
                                       FIELD_CASES[i].name);

    check_canary(probe);
}

/* A value that fits must survive unchanged -- truncation must not be the only
 * behaviour the setters get right. */
static void test_short_values_are_copied_verbatim(void)
{
    Probe probe;

    poison(probe);

    for (size_t i = 0; i < FIELD_CASE_COUNT; ++i)
    {
        FIELD_CASES[i].set(probe.item, "ok");
        TEST_ASSERT_EQUAL_STRING_MESSAGE("ok", FIELD_CASES[i].get(probe.item),
                                         FIELD_CASES[i].name);
    }

    check_canary(probe);
}

/* A value of exactly capacity - 1 is the longest that fits. strncpy() handled
 * this length correctly too -- it is here so that truncation is not the only
 * behaviour under test. */
static void test_exact_fit_values_are_copied_verbatim(void)
{
    Probe probe;

    poison(probe);

    for (size_t i = 0; i < FIELD_CASE_COUNT; ++i)
    {
        char exact[STR_LINK_LEN];
        size_t exact_len = FIELD_CASES[i].capacity - 1;

        memset(exact, 'x', exact_len);
        exact[exact_len] = '\0';

        FIELD_CASES[i].set(probe.item, exact);
        TEST_ASSERT_EQUAL_STRING_MESSAGE(exact, FIELD_CASES[i].get(probe.item),
                                         FIELD_CASES[i].name);
    }

    check_canary(probe);
}

/* The selection tables are contiguous rows of ITEM_SELECTION_*_LEN_MAX bytes.
 * strncpy() never wrote past its row, but it left the row unterminated, so
 * reading an over-long mapping ran on into the next one. openHAB mapping
 * labels routinely exceed 19 characters, which makes this the likeliest way
 * for the old behaviour to show up on a real sitemap. */
static void test_over_long_mapping_does_not_read_into_next_slot(void)
{
    Probe probe;

    poison(probe);

    probe.item.setSelectionLabel(1, "next");
    probe.item.setSelectionCommand(1, "NEXT");

    probe.item.setSelectionLabel(0, LONG_VALUE);
    probe.item.setSelectionCommand(0, LONG_VALUE);

    TEST_ASSERT_EQUAL_UINT(ITEM_SELECTION_LABEL_LEN_MAX - 1,
                           (unsigned)strlen(probe.item.getSelectionLabel(0)));
    TEST_ASSERT_EQUAL_UINT(ITEM_SELECTION_COMMAND_LEN_MAX - 1,
                           (unsigned)strlen(probe.item.getSelectionCommand(0)));

    /* The neighbour must also still be intact. */
    TEST_ASSERT_EQUAL_STRING("next", probe.item.getSelectionLabel(1));
    TEST_ASSERT_EQUAL_STRING("NEXT", probe.item.getSelectionCommand(1));

    check_canary(probe);
}

/* A value of exactly capacity characters is the shortest that does not fit,
 * and the length at which strncpy() first stopped terminating. */
static void test_values_of_exactly_capacity_still_terminate(void)
{
    Probe probe;

    poison(probe);

    for (size_t i = 0; i < FIELD_CASE_COUNT; ++i)
    {
        char exact[STR_LINK_LEN + 1];
        size_t exact_len = FIELD_CASES[i].capacity;

        memset(exact, 'x', exact_len);
        exact[exact_len] = '\0';

        FIELD_CASES[i].set(probe.item, exact);
        TEST_ASSERT_EQUAL_UINT_MESSAGE((unsigned)(FIELD_CASES[i].capacity - 1),
                                       (unsigned)strlen(FIELD_CASES[i].get(probe.item)),
                                       FIELD_CASES[i].name);
    }

    check_canary(probe);
}

/* Every mapping slot must be writable; the last one borders on other members. */
static void test_all_mapping_slots_hold_their_own_value(void)
{
    Probe probe;

    poison(probe);

    for (size_t i = 0; i < ITEM_SELECTION_COUNT_MAX; ++i)
    {
        char value[ITEM_SELECTION_LABEL_LEN_MAX];

        snprintf(value, sizeof(value), "slot-%u", (unsigned)i);
        probe.item.setSelectionLabel(i, value);
    }

    for (size_t i = 0; i < ITEM_SELECTION_COUNT_MAX; ++i)
    {
        char expected[ITEM_SELECTION_LABEL_LEN_MAX];

        snprintf(expected, sizeof(expected), "slot-%u", (unsigned)i);
        TEST_ASSERT_EQUAL_STRING(expected, probe.item.getSelectionLabel(i));
    }

    check_canary(probe);
}

void test_item_setters_run(void)
{
    /* Unity records the file from the UNITY_BEGIN() call site, which is the
     * runner, while RUN_TEST records the line from here -- so without this a
     * failure would be reported against the wrong file. */
    Unity.TestFile = __FILE__;

    RUN_TEST(test_over_long_values_truncate_and_terminate);
    RUN_TEST(test_short_values_are_copied_verbatim);
    RUN_TEST(test_exact_fit_values_are_copied_verbatim);
    RUN_TEST(test_values_of_exactly_capacity_still_terminate);
    RUN_TEST(test_over_long_mapping_does_not_read_into_next_slot);
    RUN_TEST(test_all_mapping_slots_hold_their_own_value);
}
