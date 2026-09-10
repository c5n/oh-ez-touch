/* Unit tests for Sitemap::parse().
 *
 * Two hundred lines that decide what every tile on the panel is, and until now
 * nothing reached them: the parse was welded to a fetch, so testing it meant
 * an HTTP client and therefore a network. Splitting the two apart left
 * openhab_connector.cpp including nothing but ArduinoJson and libc, which is
 * what lets this file exist.
 *
 * The pages are the simulator's own fixtures, so a change that breaks the
 * parser breaks these tests and the simulator's screen together rather than
 * one without the other.
 *
 * Run with:
 *   cd test/host && idf.py build && ./build/oh-ez-touch-host-test.elf
 */

#include <unity.h>

#include "openhab/openhab_connector.hpp"
#include "sim/sitemap_fixture.hpp"
#include "test_suites.hpp"

#include <stdlib.h>
#include <string.h>

/* The fixtures are keyed on the page name inside /rest/sitemaps/<map>/<page>,
 * so the host and port here are arbitrary. */
#define FIXTURE_URL(page) "http://fixture/rest/sitemaps/demo/" page

static int parse_fixture(Sitemap &sitemap, const char *url)
{
    const char *page = sim_sitemap_fixture_get(url);

    TEST_ASSERT_NOT_NULL_MESSAGE(page, "no fixture page for that URL");

    return sitemap.parse(page, strlen(page));
}

static void test_home_page_titles_and_counts(void)
{
    Sitemap sitemap;

    TEST_ASSERT_EQUAL_INT(0, parse_fixture(sitemap, FIXTURE_URL("demo")));
    TEST_ASSERT_EQUAL_STRING("OhEzTouch Demo", sitemap.getPageName());

    /* Six widgets and no parent -- the home page is nobody's child. */
    TEST_ASSERT_EQUAL_UINT(6, sitemap.getItemCount());
    TEST_ASSERT_NOT_EQUAL(ItemType::type_parent_link, sitemap.getItem(0)->getType());
}

static void test_widget_types(void)
{
    Sitemap sitemap;

    TEST_ASSERT_EQUAL_INT(0, parse_fixture(sitemap, FIXTURE_URL("demo")));

    /* A Text widget with a linkedPage is a link; a Group is a group; a Text
     * widget over a Number:* item is a number and over a String a string. */
    TEST_ASSERT_EQUAL(ItemType::type_group,  sitemap.getItem(0)->getType());
    TEST_ASSERT_EQUAL(ItemType::type_group,  sitemap.getItem(1)->getType());
    TEST_ASSERT_EQUAL(ItemType::type_number, sitemap.getItem(2)->getType());
    TEST_ASSERT_EQUAL(ItemType::type_string, sitemap.getItem(3)->getType());
    TEST_ASSERT_EQUAL(ItemType::type_switch, sitemap.getItem(4)->getType());
    TEST_ASSERT_EQUAL(ItemType::type_slider, sitemap.getItem(5)->getType());
}

/* The Number range test is a string comparison -- strcmp(type, "Number") >= 0
 * and <= "Number:Z" -- which is how "Number:Temperature" is told from "String"
 * without listing every dimension openHAB has. Worth pinning down, because it
 * is the least obvious line in the parser. */
static void test_dimensioned_number_is_a_number(void)
{
    Sitemap sitemap;

    TEST_ASSERT_EQUAL_INT(0, parse_fixture(sitemap, FIXTURE_URL("demo")));

    /* "Outside Temperature" is a Text widget over a Number:Temperature. */
    TEST_ASSERT_EQUAL(ItemType::type_number, sitemap.getItem(2)->getType());
    /* "Doorbell" is a Text widget over a String, which sorts outside the
     * range and must not be caught by it. */
    TEST_ASSERT_EQUAL(ItemType::type_string, sitemap.getItem(3)->getType());
}

/* A page with a parent puts the back link in slot 0, ahead of the widgets. */
static void test_parent_link_takes_the_first_slot(void)
{
    Sitemap sitemap;

    TEST_ASSERT_EQUAL_INT(0, parse_fixture(sitemap, FIXTURE_URL("living")));
    TEST_ASSERT_EQUAL_STRING("Living Room", sitemap.getPageName());

    TEST_ASSERT_EQUAL(ItemType::type_parent_link, sitemap.getItem(0)->getType());
    TEST_ASSERT_TRUE(sitemap.getItem(0)->hasPageLink());
    TEST_ASSERT_NOT_NULL(strstr(sitemap.getItem(0)->getPageLink(), "/demo/demo"));

    /* One parent plus five widgets is exactly ITEM_COUNT_MAX, so this page is
     * also the boundary of the clamp: no more would fit. */
    TEST_ASSERT_EQUAL_UINT(ITEM_COUNT_MAX, sitemap.getItemCount());
}

/* openHAB puts the formatted value in the label, in brackets. The panel draws
 * the state itself, so the brackets and the space before them come off. */
static void test_label_strips_the_bracketed_value(void)
{
    Sitemap sitemap;

    TEST_ASSERT_EQUAL_INT(0, parse_fixture(sitemap, FIXTURE_URL("living")));

    /* "Floor Lamp [75 %]" and "Thermostat [21.5 °C]" in the fixture. */
    TEST_ASSERT_EQUAL_STRING("Floor Lamp", sitemap.getItem(2)->getLabel());
    TEST_ASSERT_EQUAL_STRING("Thermostat", sitemap.getItem(4)->getLabel());

    /* A label with no brackets is left alone, including its last character --
     * the trim walks back from where the bracket would have been. */
    TEST_ASSERT_EQUAL_STRING("Ceiling Light", sitemap.getItem(1)->getLabel());
}

static void test_setpoint_range_and_pattern(void)
{
    Sitemap sitemap;

    TEST_ASSERT_EQUAL_INT(0, parse_fixture(sitemap, FIXTURE_URL("living")));

    Item *thermostat = sitemap.getItem(4);

    TEST_ASSERT_EQUAL(ItemType::type_setpoint, thermostat->getType());
    TEST_ASSERT_EQUAL_FLOAT(10.0f, thermostat->getMinVal());
    TEST_ASSERT_EQUAL_FLOAT(28.0f, thermostat->getMaxVal());
    TEST_ASSERT_EQUAL_FLOAT(0.5f, thermostat->getStep());
    TEST_ASSERT_EQUAL_STRING("%.1f °C", thermostat->getNumberPattern());

    /* The unit is stripped and the value re-printed through the same "%f" that
     * setStateNumber() uses, so that a poll returning the same reading
     * compares equal as text. */
    TEST_ASSERT_EQUAL_FLOAT(21.5f, thermostat->getStateNumber());
}

/* Absent minValue/maxValue/step fall through the widget, then the item's
 * stateDescription, then to 0 / 100 / 1. */
static void test_missing_range_falls_back_to_defaults(void)
{
    Sitemap sitemap;

    TEST_ASSERT_EQUAL_INT(0, parse_fixture(sitemap, FIXTURE_URL("living")));

    Item *dimmer = sitemap.getItem(2);

    TEST_ASSERT_EQUAL(ItemType::type_slider, dimmer->getType());
    TEST_ASSERT_EQUAL_FLOAT(0.0f, dimmer->getMinVal());
    TEST_ASSERT_EQUAL_FLOAT(100.0f, dimmer->getMaxVal());
    TEST_ASSERT_EQUAL_FLOAT(1.0f, dimmer->getStep());
}

static void test_mappings_become_the_selection(void)
{
    Sitemap sitemap;

    TEST_ASSERT_EQUAL_INT(0, parse_fixture(sitemap, FIXTURE_URL("living")));

    Item *scene = sitemap.getItem(5);

    TEST_ASSERT_EQUAL(ItemType::type_selection, scene->getType());
    TEST_ASSERT_EQUAL_UINT(4, scene->getSelectionCount());
    TEST_ASSERT_EQUAL_STRING("MOVIE", scene->getSelectionCommand(0));
    TEST_ASSERT_EQUAL_STRING("Movie", scene->getSelectionLabel(0));
    TEST_ASSERT_EQUAL_STRING("OFF", scene->getSelectionCommand(3));
    TEST_ASSERT_EQUAL_STRING("Off", scene->getSelectionLabel(3));
}

/* More widgets than the item array holds must stop at the end of it rather
 * than run past it. ITEM_COUNT_MAX is 6, so this page offers ten. */
static void test_widget_count_is_clamped(void)
{
    Sitemap sitemap;
    char page[2048];
    int len = snprintf(page, sizeof(page), "%s", "{\"title\":\"Many\",\"widgets\":[");

    for (int i = 0; i < 10; ++i)
    {
        len += snprintf(page + len, sizeof(page) - len,
                        "%s{\"type\":\"Switch\",\"label\":\"S%d\","
                        "\"item\":{\"type\":\"Switch\",\"state\":\"OFF\","
                        "\"link\":\"http://h/rest/items/S%d\"}}",
                        (i == 0) ? "" : ",", i, i);
    }

    len += snprintf(page + len, sizeof(page) - len, "%s", "]}");

    TEST_ASSERT_TRUE_MESSAGE((size_t)len < sizeof(page), "test page truncated");
    TEST_ASSERT_EQUAL_INT(0, sitemap.parse(page, (size_t)len));
    TEST_ASSERT_EQUAL_UINT(ITEM_COUNT_MAX, sitemap.getItemCount());
    TEST_ASSERT_EQUAL_STRING("S0", sitemap.getItem(0)->getLabel());
    TEST_ASSERT_EQUAL_STRING("S5", sitemap.getItem(ITEM_COUNT_MAX - 1)->getLabel());
}

/* A selection longer than the fixed arrays is clamped the same way. */
static void test_selection_count_is_clamped(void)
{
    Sitemap sitemap;
    char page[2048];
    int len = snprintf(page, sizeof(page), "%s",
                       "{\"title\":\"Sel\",\"widgets\":[{\"type\":\"Selection\","
                       "\"label\":\"Many\",\"mappings\":[");

    for (int i = 0; i < ITEM_SELECTION_COUNT_MAX + 5; ++i)
    {
        len += snprintf(page + len, sizeof(page) - len,
                        "%s{\"command\":\"C%d\",\"label\":\"L%d\"}",
                        (i == 0) ? "" : ",", i, i);
    }

    len += snprintf(page + len, sizeof(page) - len, "%s",
                    "],\"item\":{\"type\":\"String\",\"state\":\"C0\","
                    "\"link\":\"http://h/rest/items/Sel\"}}]}");

    TEST_ASSERT_TRUE_MESSAGE((size_t)len < sizeof(page), "test page truncated");
    TEST_ASSERT_EQUAL_INT(0, sitemap.parse(page, (size_t)len));
    TEST_ASSERT_EQUAL_UINT(ITEM_SELECTION_COUNT_MAX,
                           sitemap.getItem(0)->getSelectionCount());
}

/* openHAB answers a bad sitemap name with a 200 and an error object. That used
 * to "return false", which is 0 and therefore this function's success code, so
 * the caller kept the stale page and never found out. */
static void test_error_object_is_a_failure(void)
{
    Sitemap sitemap;
    static const char page[] =
        "{\"error\":{\"message\":\"Sitemap with id 'nope' cannot be found\","
        "\"http-code\":404}}";

    TEST_ASSERT_EQUAL_INT(-1, sitemap.parse(page, sizeof(page) - 1));
}

static void test_malformed_json_is_a_failure(void)
{
    Sitemap sitemap;
    static const char page[] = "{\"title\":\"Broken\",\"widgets\":[";

    TEST_ASSERT_EQUAL_INT(-1, sitemap.parse(page, sizeof(page) - 1));
}

/* A body cut short by a buffer that was too small must not parse as a short
 * page. This is what passing the length rather than relying on a terminator
 * is for. */
static void test_truncated_body_is_a_failure(void)
{
    Sitemap sitemap;
    const char *page = sim_sitemap_fixture_get(FIXTURE_URL("living"));

    TEST_ASSERT_NOT_NULL(page);
    TEST_ASSERT_EQUAL_INT(-1, sitemap.parse(page, strlen(page) / 2));
}

/* Deeper than DeserializationOption::NestingLimit(15). */
static void test_over_deep_nesting_is_a_failure(void)
{
    Sitemap sitemap;
    char page[256];
    size_t at = 0;

    for (int i = 0; i < 40; ++i)
        page[at++] = '[';

    for (int i = 0; i < 40; ++i)
        page[at++] = ']';

    TEST_ASSERT_EQUAL_INT(-1, sitemap.parse(page, at));
}

/* The contract that makes the client task's buffer management legal: the
 * payload has to stay alive for the call and not one byte longer.
 *
 * ArduinoJson parses in place and the document holds pointers into the buffer,
 * so the only reason this is safe is that every field extracted goes through
 * one of Item's strlcpy() setters. Parsed from the heap, freed, poisoned, then
 * read back -- if anything ever starts keeping a pointer instead of a copy,
 * this is the test that fails. */
static void test_page_need_not_outlive_the_parse(void)
{
    Sitemap sitemap;
    const char *fixture = sim_sitemap_fixture_get(FIXTURE_URL("living"));

    TEST_ASSERT_NOT_NULL(fixture);

    size_t len = strlen(fixture);
    char *page = (char *)malloc(len + 1);

    TEST_ASSERT_NOT_NULL(page);
    memcpy(page, fixture, len + 1);

    TEST_ASSERT_EQUAL_INT(0, sitemap.parse(page, len));

    /* Overwrite before freeing, so that a retained pointer reads rubbish
     * rather than whatever the allocator happens to leave behind. */
    memset(page, 'X', len);
    free(page);

    TEST_ASSERT_EQUAL_STRING("Living Room", sitemap.getPageName());
    TEST_ASSERT_EQUAL_STRING("Ceiling Light", sitemap.getItem(1)->getLabel());
    TEST_ASSERT_EQUAL_STRING("light", sitemap.getItem(1)->getIconName());
    TEST_ASSERT_EQUAL_STRING("ON", sitemap.getItem(1)->getStateText());
    TEST_ASSERT_EQUAL_STRING("Thermostat", sitemap.getItem(4)->getLabel());
    TEST_ASSERT_EQUAL_STRING("%.1f °C", sitemap.getItem(4)->getNumberPattern());
    TEST_ASSERT_EQUAL_STRING("MOVIE", sitemap.getItem(5)->getSelectionCommand(0));
    TEST_ASSERT_NOT_NULL(strstr(sitemap.getItem(1)->getLink(), "/rest/items/Light_Ceiling"));
}

/* The edges of that stripping, each of which the old trimmer got wrong.
 *
 * It stepped back one character before looking at anything -- so an empty
 * label, or one that is nothing but a bracketed value, formed a pointer
 * before the start of the buffer -- and it tested for trailing space with a
 * plain char, which is undefined input to isspace() for any byte above 0x7F.
 * A label like "Küche" reaches it with a negative value on every panel in a
 * German installation. */
static void test_label_trimming_edges(void)
{
    static const struct
    {
        const char *label;
        const char *want;
    } cases[] = {
        /* Nothing at all, and nothing but the value: both used to form
         * buffer - 1. */
        {"", ""},
        {"[21.5]", ""},
        {" [21.5]", ""},

        /* A label that is only spaces trims to nothing rather than walking
         * back past the start looking for one. */
        {"   ", ""},

        /* Non-ASCII, which is the case that reached isspace() negative. The
         * bytes have to survive intact -- a name is what the user sees. */
        {"Küche", "Küche"},
        {"Küche [21.5 degC]", "Küche"},
        {"Büro Süd [ON]", "Büro Süd"},
        {"Außentemperatur [3.5 °C]", "Außentemperatur"},

        /* The ordinary shapes, kept here so the edges are read next to
         * them. */
        {"Kitchen", "Kitchen"},
        {"Kitchen [21.5 degC]", "Kitchen"},
        {"Kitchen   [21.5]", "Kitchen"},
        {"Kitchen[21.5]", "Kitchen"},

        /* A trailing space with no bracket at all is still trailing space. */
        {"Kitchen ", "Kitchen"},

        /* Interior spaces are not trailing ones. */
        {"Living Room Lamp", "Living Room Lamp"},
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
    {
        Sitemap sitemap;
        char    page[512];

        snprintf(page, sizeof(page),
                 "{\"title\":\"T\",\"widgets\":[{\"type\":\"Switch\",\"label\":\"%s\","
                 "\"item\":{\"type\":\"Switch\",\"state\":\"ON\","
                 "\"link\":\"http://h/rest/items/x\"}}]}",
                 cases[i].label);

        TEST_ASSERT_EQUAL_INT(0, sitemap.parse(page, strlen(page)));
        TEST_ASSERT_EQUAL_INT(1, sitemap.getItemCount());
        TEST_ASSERT_EQUAL_STRING_MESSAGE(cases[i].want, sitemap.getItem(0)->getLabel(),
                                         cases[i].label);
    }
}

/* A label longer than the field it lands in is cut to fit rather than
 * overrunning it. */
static void test_an_over_long_label_is_truncated(void)
{
    Sitemap sitemap;
    char    page[512];
    char    label[STR_LABEL_LEN * 2];

    memset(label, 'x', sizeof(label) - 1);
    label[sizeof(label) - 1] = '\0';

    snprintf(page, sizeof(page),
             "{\"title\":\"T\",\"widgets\":[{\"type\":\"Switch\",\"label\":\"%s\","
             "\"item\":{\"type\":\"Switch\",\"state\":\"ON\"}}]}",
             label);

    TEST_ASSERT_EQUAL_INT(0, sitemap.parse(page, strlen(page)));
    TEST_ASSERT_EQUAL_INT(STR_LABEL_LEN - 1, strlen(sitemap.getItem(0)->getLabel()));
}

void test_sitemap_parse_run(void)
{
    RUN_TEST(test_home_page_titles_and_counts);
    RUN_TEST(test_widget_types);
    RUN_TEST(test_dimensioned_number_is_a_number);
    RUN_TEST(test_parent_link_takes_the_first_slot);
    RUN_TEST(test_label_strips_the_bracketed_value);
    RUN_TEST(test_label_trimming_edges);
    RUN_TEST(test_an_over_long_label_is_truncated);
    RUN_TEST(test_setpoint_range_and_pattern);
    RUN_TEST(test_missing_range_falls_back_to_defaults);
    RUN_TEST(test_mappings_become_the_selection);
    RUN_TEST(test_widget_count_is_clamped);
    RUN_TEST(test_selection_count_is_clamped);
    RUN_TEST(test_error_object_is_a_failure);
    RUN_TEST(test_malformed_json_is_a_failure);
    RUN_TEST(test_truncated_body_is_a_failure);
    RUN_TEST(test_over_deep_nesting_is_a_failure);
    RUN_TEST(test_page_need_not_outlive_the_parse);
}
