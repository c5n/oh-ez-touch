/* Unit tests for the built-in icon set's name handling and lookup.
 *
 * Two halves, and they fail differently.
 *
 * icon_url_split() and icon_name_qualify() take a URL the connector built and
 * pull the icon's name and state back out of it. A mistake there is loud: the
 * lookup misses, the request goes to openHAB, and the tile draws anyway.
 *
 * icon_set_get() is the half worth the tests. It answers *before* the network
 * does, so once the set is compiled in, its rules are the ones the panel obeys
 * -- openHAB no longer gets a say. A rule implemented wrongly here does not
 * fail, it silently draws a different picture than the server would have: a
 * dimmer at 44 shown at full brightness looks exactly like a dimmer at 44.
 *
 * The table these run against is test/host/main/icon_set_fixture.h, not the
 * generated one; see its header comment for why.
 */

#include <unity.h>

#include "icons/icon_set.hpp"
#include "test_suites.hpp"

#include <string.h>

#define WEBSITE "http://openhabian:8080"

/* The fixture stores each icon's name as its payload, so this is "the lookup
 * returned the entry I meant" without needing to know any offsets. */
static void assert_icon(const char *expected, const char *name, const char *state)
{
    size_t               size = 0;
    const unsigned char *data = icon_set_get(name, state, &size);

    TEST_ASSERT_NOT_NULL(data);
    TEST_ASSERT_EQUAL_size_t(strlen(expected), size);
    TEST_ASSERT_EQUAL_MEMORY(expected, data, size);
}

static void assert_no_icon(const char *name, const char *state)
{
    size_t size = 123;

    TEST_ASSERT_NULL(icon_set_get(name, state, &size));
    /* Callers pass this straight to lodepng on a hit; a miss must not leave a
     * stale length behind for one that forgot to check the pointer. */
    TEST_ASSERT_EQUAL_size_t(0, size);
}

/* ----------------------------------------------------------- splitting URLs */

static void test_split_a_url_the_connector_built(void)
{
    char name[ICON_SET_NAME_MAX];
    char state[ICON_SET_NAME_MAX];

    TEST_ASSERT_TRUE(icon_url_split(WEBSITE "/icon/light?state=ON&format=png",
                                    name, sizeof(name), state, sizeof(state)));
    TEST_ASSERT_EQUAL_STRING("light", name);
    TEST_ASSERT_EQUAL_STRING("ON", state);
}

/* iconUrl() always appends "&format=png", but a URL typed by hand or built by
 * an older firmware ends at the state, so both endings have to work. */
static void test_split_a_url_that_ends_at_the_state(void)
{
    char name[ICON_SET_NAME_MAX];
    char state[ICON_SET_NAME_MAX];

    TEST_ASSERT_TRUE(icon_url_split(WEBSITE "/icon/light?state=ON",
                                    name, sizeof(name), state, sizeof(state)));
    TEST_ASSERT_EQUAL_STRING("light", name);
    TEST_ASSERT_EQUAL_STRING("ON", state);
}

static void test_split_a_url_with_no_query_at_all(void)
{
    char name[ICON_SET_NAME_MAX];
    char state[ICON_SET_NAME_MAX];

    TEST_ASSERT_TRUE(icon_url_split(WEBSITE "/icon/light",
                                    name, sizeof(name), state, sizeof(state)));
    TEST_ASSERT_EQUAL_STRING("light", name);
    TEST_ASSERT_EQUAL_STRING("", state);
}

/* A state openHAB sends verbatim: iconUrl() does not percent-encode, so the
 * space survives into the URL and has to survive back out of it. */
static void test_split_a_state_with_a_space(void)
{
    char name[ICON_SET_NAME_MAX];
    char state[ICON_SET_NAME_MAX];

    TEST_ASSERT_TRUE(icon_url_split(WEBSITE "/icon/text?state=Hello World&format=png",
                                    name, sizeof(name), state, sizeof(state)));
    TEST_ASSERT_EQUAL_STRING("text", name);
    TEST_ASSERT_EQUAL_STRING("Hello World", state);
}

static void test_a_url_that_is_not_an_icon_url_is_refused(void)
{
    char name[ICON_SET_NAME_MAX];
    char state[ICON_SET_NAME_MAX];

    TEST_ASSERT_FALSE(icon_url_split(WEBSITE "/rest/items/Light_Hallway/state",
                                     name, sizeof(name), state, sizeof(state)));
    TEST_ASSERT_FALSE(icon_url_split(NULL, name, sizeof(name), state, sizeof(state)));
}

static void test_an_empty_name_is_refused(void)
{
    char name[ICON_SET_NAME_MAX];
    char state[ICON_SET_NAME_MAX];

    TEST_ASSERT_FALSE(icon_url_split(WEBSITE "/icon/?state=ON",
                                     name, sizeof(name), state, sizeof(state)));
}

/* A truncated name is a different icon, and looking *that* one up is worse
 * than finding nothing: it would draw the wrong picture with no miss to fall
 * through to the server on. */
static void test_a_name_that_does_not_fit_is_refused(void)
{
    char name[8];
    char state[ICON_SET_NAME_MAX];

    TEST_ASSERT_FALSE(icon_url_split(WEBSITE "/icon/temperature?state=ON",
                                     name, sizeof(name), state, sizeof(state)));
}

static void test_a_state_that_does_not_fit_is_refused(void)
{
    char name[ICON_SET_NAME_MAX];
    char state[4];

    TEST_ASSERT_FALSE(icon_url_split(WEBSITE "/icon/light?state=Hello World",
                                     name, sizeof(name), state, sizeof(state)));
}

static void test_qualify_joins_and_lowercases(void)
{
    char out[ICON_SET_NAME_MAX];

    TEST_ASSERT_TRUE(icon_name_qualify("Light", "ON", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("light-on", out);
}

static void test_qualify_refuses_an_empty_state(void)
{
    char out[ICON_SET_NAME_MAX];

    TEST_ASSERT_FALSE(icon_name_qualify("light", "", out, sizeof(out)));
    TEST_ASSERT_FALSE(icon_name_qualify("light", NULL, out, sizeof(out)));
}

static void test_qualify_refuses_what_does_not_fit(void)
{
    char out[8];

    TEST_ASSERT_FALSE(icon_name_qualify("temperature", "ON", out, sizeof(out)));
}

/* ------------------------------------------------- rule 1: the exact variant */

static void test_a_state_specific_icon_wins(void)
{
    assert_icon("light-on", "light", "ON");
    assert_icon("light-off", "light", "OFF");
}

/* A sitemap is under no obligation to write icon names the way openHAB stores
 * them, and the table is lowercase throughout. */
static void test_lookup_ignores_case_in_both_halves(void)
{
    assert_icon("light-on", "LIGHT", "on");
    assert_icon("light-on", "Light", "On");
    assert_icon("alarm", "ALARM", "");
}

/* ------------------------------------------------ rule 2: the nearest step */

static void test_an_exact_step_is_found(void)
{
    assert_icon("light-0", "light", "0");
    assert_icon("light-20", "light", "20");
    assert_icon("light-100", "light", "100");
}

/* The rule that earns its keep. Without it every dimmer between the steps
 * falls through to the plain icon and the panel draws them all identically --
 * which is exactly what the server had been getting right. */
static void test_a_state_between_steps_rounds_down(void)
{
    assert_icon("light-0", "light", "9");
    assert_icon("light-10", "light", "11");
    assert_icon("light-20", "light", "44");
    assert_icon("light-90", "light", "99");
    assert_icon("light-100", "light", "100");
}

static void test_a_fractional_state_rounds_down_too(void)
{
    assert_icon("light-20", "light", "44.5");
    assert_icon("light-0", "light", "0.0");
}

/* "contact" has named variants and no numeric ones, as door, lock, presence
 * and seventeen others in the classic set also do. They share the "contact-"
 * prefix the numeric search walks, and strtoul("ajar") is 0 -- so a lookup that
 * did not insist on digits would find a step where the family has none and
 * answer contact-ajar for every numeric state. Nothing outranks it, because
 * there is nothing else in the run.
 *
 * "light" cannot catch that. The same bug reads light-off as 0 there too, but
 * 0 loses to light-20 and the wrong answer never reaches the caller -- which is
 * worth saying out loud, because a test that asserted it on "light" would pass
 * with the digit check deleted. */
static void test_a_named_variant_is_never_read_as_a_step(void)
{
    assert_icon("contact", "contact", "5");
    assert_icon("contact", "contact", "0");
    assert_icon("contact", "contact", "99.5");
}

/* Rule 1 still answers for the states those variants are actually named after,
 * which is the whole reason the family exists. */
static void test_a_named_only_family_still_resolves_by_name(void)
{
    assert_icon("contact-open", "contact", "OPEN");
    assert_icon("contact-closed", "contact", "CLOSED");
    assert_icon("contact-ajar", "contact", "ajar");
}

/* Below every step there is no step to round down to, so rule 3 takes over --
 * rather than picking the lowest, which would be rounding *up*. */
static void test_a_state_below_every_step_falls_through(void)
{
    assert_icon("light", "light", "-5");
}

static void test_a_state_above_every_step_takes_the_highest(void)
{
    assert_icon("light-100", "light", "400");
}

/* Only a state that is *entirely* a number is a step. A colour item's state is
 * three of them and a switch's is a word; strtod() would happily read the
 * leading number out of the first and make it a brightness. */
static void test_a_state_that_merely_starts_with_a_number(void)
{
    assert_icon("light", "light", "120,100,50");
    assert_icon("light", "light", "21.5 °C");
}

/* ---------------------------------------------------- rule 3: the plain icon */

static void test_an_unknown_state_falls_back_to_the_plain_icon(void)
{
    assert_icon("light", "light", "PURPLE");
    assert_icon("temperature", "temperature", "21.5");
}

static void test_no_state_finds_the_plain_icon(void)
{
    assert_icon("light", "light", "");
    assert_icon("light", "light", NULL);
}

/* ------------------------------------------------------------------- misses */

/* What keeps custom icons working: anything dropped into a server's own
 * icons/classic/ exists on that one openHAB and no firmware can ship it, so a
 * miss has to fall through to HTTP rather than draw a placeholder. */
static void test_an_unknown_name_misses(void)
{
    assert_no_icon("nosuchicon", "ON");
    assert_no_icon("nosuchicon", "");
}

static void test_a_partial_name_is_not_a_match(void)
{
    assert_no_icon("lig", "");
    assert_no_icon("lights", "");
}

/* "light-on" as a *name* is a different question from "light" in state "ON",
 * and both are legal: the table holds it, so it answers. */
static void test_a_variant_name_can_be_asked_for_directly(void)
{
    assert_icon("light-on", "light-on", "");
}

static void test_a_null_or_empty_name_misses(void)
{
    assert_no_icon(NULL, "ON");
    assert_no_icon("", "ON");
}

/* ------------------------------------------------------------ by URL, end to end */

static void test_lookup_straight_from_a_url(void)
{
    size_t               size = 0;
    const unsigned char *data =
        icon_set_get_by_url(WEBSITE "/icon/light?state=44&format=png", &size);

    TEST_ASSERT_NOT_NULL(data);
    TEST_ASSERT_EQUAL_size_t(strlen("light-20"), size);
    TEST_ASSERT_EQUAL_MEMORY("light-20", data, size);
}

static void test_a_url_that_does_not_split_misses(void)
{
    size_t size = 123;

    TEST_ASSERT_NULL(icon_set_get_by_url(WEBSITE "/rest/items/X/state", &size));
    TEST_ASSERT_EQUAL_size_t(0, size);
}

/* ------------------------------------------------------------------- the set */

static void test_the_set_reports_what_it_holds(void)
{
    TEST_ASSERT_EQUAL_size_t(14, icon_set_count());
    TEST_ASSERT_EQUAL_size_t(123, icon_set_bytes());
}

/* The binary search and the prefix walk both assume it, and a table that drifts
 * out of order does not fail loudly -- it just stops finding some icons. */
static void test_the_table_is_sorted(void)
{
    static const char *const names[] = {
        "alarm", "contact", "contact-ajar", "contact-closed", "contact-open",
        "light", "light-0", "light-10", "light-100",
        "light-20", "light-90", "light-off", "light-on", "temperature",
    };
    size_t count = sizeof(names) / sizeof(names[0]);

    TEST_ASSERT_EQUAL_size_t(icon_set_count(), count);

    for (size_t i = 0; i < count; ++i)
    {
        size_t size = 0;

        TEST_ASSERT_NOT_NULL(icon_set_get(names[i], "", &size));

        if (i > 0)
            TEST_ASSERT_TRUE(strcmp(names[i - 1], names[i]) < 0);
    }
}

void test_icon_set_run(void)
{
    RUN_TEST(test_split_a_url_the_connector_built);
    RUN_TEST(test_split_a_url_that_ends_at_the_state);
    RUN_TEST(test_split_a_url_with_no_query_at_all);
    RUN_TEST(test_split_a_state_with_a_space);
    RUN_TEST(test_a_url_that_is_not_an_icon_url_is_refused);
    RUN_TEST(test_an_empty_name_is_refused);
    RUN_TEST(test_a_name_that_does_not_fit_is_refused);
    RUN_TEST(test_a_state_that_does_not_fit_is_refused);
    RUN_TEST(test_qualify_joins_and_lowercases);
    RUN_TEST(test_qualify_refuses_an_empty_state);
    RUN_TEST(test_qualify_refuses_what_does_not_fit);

    RUN_TEST(test_a_state_specific_icon_wins);
    RUN_TEST(test_lookup_ignores_case_in_both_halves);

    RUN_TEST(test_an_exact_step_is_found);
    RUN_TEST(test_a_state_between_steps_rounds_down);
    RUN_TEST(test_a_fractional_state_rounds_down_too);
    RUN_TEST(test_a_named_variant_is_never_read_as_a_step);
    RUN_TEST(test_a_named_only_family_still_resolves_by_name);
    RUN_TEST(test_a_state_below_every_step_falls_through);
    RUN_TEST(test_a_state_above_every_step_takes_the_highest);
    RUN_TEST(test_a_state_that_merely_starts_with_a_number);

    RUN_TEST(test_an_unknown_state_falls_back_to_the_plain_icon);
    RUN_TEST(test_no_state_finds_the_plain_icon);

    RUN_TEST(test_an_unknown_name_misses);
    RUN_TEST(test_a_partial_name_is_not_a_match);
    RUN_TEST(test_a_variant_name_can_be_asked_for_directly);
    RUN_TEST(test_a_null_or_empty_name_misses);

    RUN_TEST(test_lookup_straight_from_a_url);
    RUN_TEST(test_a_url_that_does_not_split_misses);

    RUN_TEST(test_the_set_reports_what_it_holds);
    RUN_TEST(test_the_table_is_sorted);
}
