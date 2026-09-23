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
 * one without the other. Those fixtures were written from the openHAB REST
 * documentation and corrected on 2026-09-15 against a real openHAB 5.2.1, so
 * the shapes asserted below are shapes a server actually sends -- see the
 * header of main/sim/sitemap_fixture.cpp for the three that had been guessed
 * wrong.
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

    /* A Group is a group; a Text widget with a linkedPage is a link; a Text
     * widget over a Number:* item is a number and over a String a string.
     *
     * Slots 0 and 1 are the two ways a sitemap makes a sub-page, and both are
     * here because a real server sends both: `Group item=gX` becomes a Group
     * widget carrying the group item, and `Text label="..." { ... }` becomes a
     * Text widget with a linkedPage and no item at all. Only the first used to
     * be covered -- the comment claimed the second and no assertion made it. */
    TEST_ASSERT_EQUAL(ItemType::type_group,  sitemap.getItem(0)->getType());
    TEST_ASSERT_EQUAL(ItemType::type_link,   sitemap.getItem(1)->getType());
    TEST_ASSERT_EQUAL(ItemType::type_number, sitemap.getItem(2)->getType());
    TEST_ASSERT_EQUAL(ItemType::type_string, sitemap.getItem(3)->getType());
    TEST_ASSERT_EQUAL(ItemType::type_switch, sitemap.getItem(4)->getType());
    TEST_ASSERT_EQUAL(ItemType::type_slider, sitemap.getItem(5)->getType());
}

/* The sub-page shapes, each carrying what a real server puts on it.
 *
 * The Text form has no "item" key whatsoever, so every json_item[...] lookup
 * below it walks a null variant. That is the case that would fault if anything
 * in the parser ever dereferenced the item's type instead of asking json_str()
 * for it, and it is what the user's own sitemap produced. */
static void test_the_two_link_shapes(void)
{
    Sitemap sitemap;

    TEST_ASSERT_EQUAL_INT(0, parse_fixture(sitemap, FIXTURE_URL("demo")));

    Item *group = sitemap.getItem(0);
    Item *link  = sitemap.getItem(1);

    /* A Group widget has both: the page it opens, and the group item whose
     * state openHAB reports for it. Checked against a real one -- a
     * `Group:Switch:OR(ON,OFF)` on openHAB 5.2.1 arrives with an item carrying
     * "members", "groupType" and "function", a linkedPage, and an aggregated
     * state ("ON" while any member is on). The tile is polled like any other,
     * because that state is real. */
    TEST_ASSERT_EQUAL(ItemType::type_group, group->getType());
    TEST_ASSERT_TRUE(group->hasPageLink());
    TEST_ASSERT_NOT_NULL(strstr(group->getPageLink(), "/demo/living"));
    TEST_ASSERT_NOT_NULL(strstr(group->getLink(), "/rest/items/gLivingRoom"));
    TEST_ASSERT_EQUAL_STRING("OFF", group->getStateText());

    char group_url[STR_URL_LEN];
    TEST_ASSERT_TRUE(group->stateUrl(group_url, sizeof(group_url)));

    /* The Text form has the page and nothing else. An empty link is what
     * stateUrl() refuses on, which is what keeps the panel from polling
     * "<nothing>/state" once every five seconds. */
    TEST_ASSERT_EQUAL_STRING("Bedroom", link->getLabel());
    TEST_ASSERT_TRUE(link->hasPageLink());
    TEST_ASSERT_NOT_NULL(strstr(link->getPageLink(), "/demo/bedroom"));
    TEST_ASSERT_EQUAL_STRING("", link->getLink());

    char url[STR_URL_LEN];
    TEST_ASSERT_FALSE(link->stateUrl(url, sizeof(url)));
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

/* Every widget a real server sends carries a "mappings" array, empty when the
 * sitemap declares none -- so the panel must read an empty one as "no choices"
 * and not as "some choices I could not read".
 *
 * It matters more than it looks, because an empty JSON array is *truthy* to
 * ArduinoJson: `if (widget["mappings"])` is true for every widget openHAB
 * sends, which is why the parser's fallback to the item's
 * commandDescription.commandOptions can never run against a real openHAB 5.
 * The fixture used to omit the key entirely and so exercised the fallback
 * instead of the path a server actually takes. */
static void test_an_empty_mappings_array_is_not_a_selection(void)
{
    Sitemap sitemap;

    TEST_ASSERT_EQUAL_INT(0, parse_fixture(sitemap, FIXTURE_URL("demo")));

    /* The switch and the slider both arrive with "mappings": []. */
    TEST_ASSERT_EQUAL_UINT(0, sitemap.getItem(4)->getSelectionCount());
    TEST_ASSERT_EQUAL_UINT(0, sitemap.getItem(5)->getSelectionCount());

    /* And the link, which has no item behind it to fall back to either. */
    TEST_ASSERT_EQUAL_UINT(0, sitemap.getItem(1)->getSelectionCount());
}

/* The fallback is still reachable for a server that omits the key, which is
 * the only shape that reaches it, so it is pinned where it can be reached. */
static void test_command_options_are_used_when_mappings_are_absent(void)
{
    Sitemap sitemap;
    static const char page[] =
        "{\"title\":\"T\",\"widgets\":[{\"type\":\"Selection\",\"label\":\"Fan\","
        "\"item\":{\"type\":\"String\",\"state\":\"LOW\","
        "\"link\":\"http://h/rest/items/Fan\","
        "\"commandDescription\":{\"commandOptions\":["
        "{\"command\":\"LOW\",\"label\":\"Low\"},"
        "{\"command\":\"HIGH\",\"label\":\"High\"}]}}}]}";

    TEST_ASSERT_EQUAL_INT(0, sitemap.parse(page, sizeof(page) - 1));
    TEST_ASSERT_EQUAL_UINT(2, sitemap.getItem(0)->getSelectionCount());
    TEST_ASSERT_EQUAL_STRING("HIGH", sitemap.getItem(0)->getSelectionCommand(1));
    TEST_ASSERT_EQUAL_STRING("High", sitemap.getItem(0)->getSelectionLabel(1));
}

/* A Player arrives as a Switch widget over a Player item, carrying four
 * mappings openHAB generated by itself from a bare `Default item=...`. The
 * item's type has to win: mappings are read after the type is decided and must
 * not turn the tile into a selection, or the transport screen never opens. */
static void test_a_player_keeps_its_type_despite_its_mappings(void)
{
    Sitemap sitemap;

    TEST_ASSERT_EQUAL_INT(0, parse_fixture(sitemap, FIXTURE_URL("bedroom")));

    Item *player = sitemap.getItem(2); /* slot 0 is the parent link */

    TEST_ASSERT_EQUAL(ItemType::type_player, player->getType());
    TEST_ASSERT_EQUAL_UINT(4, player->getSelectionCount());
    TEST_ASSERT_EQUAL_STRING("PREVIOUS", player->getSelectionCommand(0));
    TEST_ASSERT_EQUAL_STRING("PLAY", player->getSelectionCommand(2));

    /* And the one beside it, which openHAB sends with an empty array. */
    TEST_ASSERT_EQUAL(ItemType::type_rollershutter, sitemap.getItem(1)->getType());
    TEST_ASSERT_EQUAL_UINT(0, sitemap.getItem(1)->getSelectionCount());
}

/* openHAB reports an item it has no value for as the four characters "NULL" --
 * not a JSON null, not an empty string. It reaches the tile verbatim for a
 * type whose state is text.
 *
 * The panel shows those four characters on the tile, which is what a panel
 * pointed at a fresh openHAB really does show. */
/* The item's own rendering of its state, which the tile prefers to the raw one
 * when openHAB sends it (see update_state_widget() in openhab_ui.cpp).
 *
 * Here because it is the one field the parse reads that nothing else in this
 * file asserted, and because Sitemap::parse() now names every key it wants in
 * a filter: a key spelled wrong there does not fail, it silently reads null,
 * and the only place that would have shown up is a tile quietly losing its
 * transformed state. openHAB sends this for an item with a MAP or JSONPATH
 * transformation on its state description. */
static void test_the_transformed_state_is_kept(void)
{
    Sitemap sitemap;
    static const char page[] =
        "{\"title\":\"T\",\"widgets\":[{\"type\":\"Text\",\"label\":\"Mode\","
        "\"item\":{\"type\":\"String\",\"state\":\"eco\","
        "\"transformedState\":\"Economy\","
        "\"link\":\"http://h/rest/items/Mode\"}}]}";

    TEST_ASSERT_EQUAL_INT(0, sitemap.parse(page, sizeof(page) - 1));

    Item *mode = sitemap.getItem(0);

    TEST_ASSERT_EQUAL(ItemType::type_string, mode->getType());
    TEST_ASSERT_EQUAL_STRING("eco", mode->getStateText());
    TEST_ASSERT_EQUAL_STRING("Economy", mode->getTransformedStateText());
}

static void test_a_null_state_arrives_as_text(void)
{
    Sitemap sitemap;

    TEST_ASSERT_EQUAL_INT(0, parse_fixture(sitemap, FIXTURE_URL("bedroom")));

    Item *night = sitemap.getItem(4);

    TEST_ASSERT_EQUAL(ItemType::type_switch, night->getType());
    TEST_ASSERT_EQUAL_STRING("NULL", night->getStateText());
}

/* The same state on a numeric type goes through strtof(), which reads nothing
 * from "NULL" and yields zero -- so an uninitialised setpoint reads 0.0 on the
 * glass while openHAB's own label for it says "- °C".
 *
 * Pinned rather than endorsed: it is what the panel does today, and it is the
 * behaviour to change if a dash is ever wanted instead. */
static void test_a_null_state_on_a_number_reads_as_zero(void)
{
    Sitemap sitemap;
    static const char page[] =
        "{\"title\":\"T\",\"widgets\":[{\"type\":\"Setpoint\",\"label\":\"Sp\","
        "\"mappings\":[],\"minValue\":-10,\"maxValue\":10,\"step\":0.5,"
        "\"item\":{\"type\":\"Number\",\"state\":\"NULL\","
        "\"link\":\"http://h/rest/items/N\"}}]}";

    TEST_ASSERT_EQUAL_INT(0, sitemap.parse(page, sizeof(page) - 1));
    TEST_ASSERT_EQUAL(ItemType::type_setpoint, sitemap.getItem(0)->getType());
    TEST_ASSERT_EQUAL_STRING("0.000000", sitemap.getItem(0)->getStateText());
    TEST_ASSERT_EQUAL_FLOAT(0.0f, sitemap.getItem(0)->getStateNumber());
}

/* A dimensioned item, and the two places openHAB puts its unit.
 *
 * Captured from openHAB 5.2.1: a Number:Temperature holding 21.5 °C arrives
 * with state "21.5 °C", the widget carrying "unit": "°C" and the item carrying
 * "unitSymbol": "°C". The connector reads neither -- it strips the unit off
 * the state with strtof() and formats the number back through the item's
 * pattern, which happens to carry "°C" too, so a temperature comes out right
 * by a route that does not involve the unit at all.
 *
 * That works until the pattern and the unit disagree, which is exactly what
 * Number:Dimensionless does -- see
 * test_a_dimensionless_percentage_is_stored_as_its_ratio() in
 * test_item_state.cpp. This test pins the shape so the fields are on record. */
static void test_a_dimensioned_number_arrives_with_its_unit(void)
{
    Sitemap sitemap;

    TEST_ASSERT_EQUAL_INT(0, parse_fixture(sitemap, FIXTURE_URL("demo")));

    Item *temperature = sitemap.getItem(2);

    TEST_ASSERT_EQUAL(ItemType::type_number, temperature->getType());
    TEST_ASSERT_EQUAL_STRING("Outside Temperature", temperature->getLabel());

    /* "3.5 °C" in, the bare number out. */
    TEST_ASSERT_EQUAL_STRING("3.500000", temperature->getStateText());
    TEST_ASSERT_EQUAL_FLOAT(3.5f, temperature->getStateNumber());

    /* And the unit reaches the tile only because the pattern repeats it. */
    TEST_ASSERT_EQUAL_STRING("%.1f °C", temperature->getNumberPattern());
}

/* A plain Number comes off the wire with six decimals -- "10.000000", not
 * "10" -- because openHAB formats the state and not the pattern. The parser
 * re-prints it through the same "%f", so the two spellings have to agree or
 * every poll would look like a change. */
static void test_a_plain_number_state_keeps_its_printed_form(void)
{
    Sitemap sitemap;
    static const char page[] =
        "{\"title\":\"T\",\"widgets\":[{\"type\":\"Text\",\"label\":\"N [10,0 °C]\","
        "\"mappings\":[],\"pattern\":\"%.1f °C\","
        "\"item\":{\"type\":\"Number\",\"state\":\"10.000000\","
        "\"stateDescription\":{\"pattern\":\"%.1f °C\"},"
        "\"link\":\"http://h/rest/items/N\"}}]}";

    TEST_ASSERT_EQUAL_INT(0, sitemap.parse(page, sizeof(page) - 1));

    Item *item = sitemap.getItem(0);

    TEST_ASSERT_EQUAL(ItemType::type_number, item->getType());
    TEST_ASSERT_EQUAL_STRING("10.000000", item->getStateText());

    /* The label openHAB built carries the value in a German decimal comma;
     * all of it comes off, comma included. */
    TEST_ASSERT_EQUAL_STRING("N", item->getLabel());

    /* The pattern is taken from the item's stateDescription. openHAB fills the
     * widget's own "pattern" field from that same place -- verified against
     * 5.2.1, where the .items label carried the pattern and the .sitemap line
     * did not -- so reading either gives the same answer. */
    TEST_ASSERT_EQUAL_STRING("%.1f °C", item->getNumberPattern());

    /* An unchanged reading polled again is not a change. */
    TEST_ASSERT_EQUAL_INT(0, item->applyState("10.000000", strlen("10.000000")));
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

/* A body that is an error object rather than a page is a failure. That used to
 * "return false", which is 0 and therefore this function's success code, so
 * the caller kept the stale page and never found out.
 *
 * This test used to say openHAB answers a bad sitemap name with a *200* and
 * this object. It does not. Measured against openHAB 5.2.1:
 *
 *   /rest/sitemaps/nope/nope            404, and the body is a JSON *string*
 *                                       containing JSON -- see the next test
 *   /rest/items/<missing>/state         404, and the body is this bare object
 *
 * so openhab_http.cpp's status check rejects both before a byte reaches here.
 * Refusing the object is still the right thing -- it costs nothing and the
 * shape is what a server would send if it ever answered 200 -- but the guard
 * that actually protects the panel today is the status check, not this. */
static void test_error_object_is_a_failure(void)
{
    Sitemap sitemap;
    static const char page[] =
        "{\"error\":{\"message\":\"Sitemap with id 'nope' cannot be found\","
        "\"http-code\":404}}";

    TEST_ASSERT_EQUAL_INT(-1, sitemap.parse(page, sizeof(page) - 1));
}

/* What openHAB 5.2.1 really answers for a sitemap that is not there, captured
 * verbatim: a 404 whose body is JSON-encoded *twice*, so the top level is a
 * string and not an object at all.
 *
 * It parses, because a bare JSON string is valid JSON, and there is no "error"
 * object in it to catch -- so the page comes out titled "no title" with no
 * widgets, and parse() calls that success. Nothing reaches this state today,
 * because the 404 is refused by the status check first; the test is here to
 * record that parse() alone does not recognise a non-page, and that a caller
 * which ever stops checking the status would blank the screen rather than keep
 * the page it had. */
static void test_a_body_that_is_not_a_page_yields_an_empty_page(void)
{
    Sitemap sitemap;
    static const char page[] =
        "\"{\\\"error\\\":{\\\"message\\\":\\\"HTTP 404 Not Found\\\","
        "\\\"http-code\\\":404}}\"";

    TEST_ASSERT_EQUAL_INT(0, sitemap.parse(page, sizeof(page) - 1));
    TEST_ASSERT_EQUAL_UINT(0, sitemap.getItemCount());
    TEST_ASSERT_EQUAL_STRING("no title", sitemap.getPageName());
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

/* The device path: the document's pool comes from a caller-provided scratch
 * area -- on the panel the tail of the page's body buffer -- rather than the
 * heap. The parse must come out the same, or nothing above the pool knows
 * which memory it ran on. */
static void test_parse_from_a_scratch_arena(void)
{
    const char *page = sim_sitemap_fixture_get(FIXTURE_URL("living"));

    TEST_ASSERT_NOT_NULL(page);

    size_t page_len = strlen(page);
    size_t body = (page_len + 1 + 3) & ~(size_t)3;

    /* Body plus the pool the document asks for. On the 64-bit host
     * ArduinoJson's pool blocks are 4 KB each and its string nodes twice as
     * wide as on the panel, so the figure is the host's, not the device's. */
    static char buf[4368 + 8192];

    TEST_ASSERT_TRUE(body + 4096 <= sizeof(buf));

    memcpy(buf, page, page_len + 1);

    Sitemap sitemap;

    TEST_ASSERT_EQUAL_INT(0, sitemap.parse(buf, page_len, buf + body, sizeof(buf) - body));

    /* The same page the heap path produces: the parent link in slot 0, then
     * the widgets. */
    TEST_ASSERT_EQUAL_STRING("Living Room", sitemap.getPageName());
    TEST_ASSERT_EQUAL_UINT(6, sitemap.getItemCount());
    TEST_ASSERT_EQUAL_INT(ItemType::type_parent_link, sitemap.getItem(0)->getType());
    TEST_ASSERT_EQUAL_STRING("http://localhost:8080/rest/sitemaps/demo/demo",
                             sitemap.getItem(0)->getPageLink());
    TEST_ASSERT_EQUAL_INT(ItemType::type_selection, sitemap.getItem(5)->getType());
    TEST_ASSERT_EQUAL_UINT(4, sitemap.getItem(5)->getSelectionCount());
    TEST_ASSERT_EQUAL_STRING("PARTY", sitemap.getItem(5)->getSelectionCommand(2));
}

/* And its failure mode: a scratch too small for the document falls back to
 * the heap and the page parses anyway -- the arena is the fast path, not a
 * second way to lose a page. */
static void test_a_scratch_too_small_falls_back_to_the_heap(void)
{
    const char *page = sim_sitemap_fixture_get(FIXTURE_URL("living"));

    TEST_ASSERT_NOT_NULL(page);

    /* A quarter of what the page's document costs, measured with a counting
     * allocator -- small enough that deserializeJson() must run out. */
    static char scratch[1024];

    Sitemap sitemap;

    TEST_ASSERT_EQUAL_INT(0, sitemap.parse(page, strlen(page), scratch, sizeof(scratch)));
    TEST_ASSERT_EQUAL_STRING("Living Room", sitemap.getPageName());
    TEST_ASSERT_EQUAL_UINT(6, sitemap.getItemCount());
}

void test_sitemap_parse_run(void)
{
    RUN_TEST(test_parse_from_a_scratch_arena);
    RUN_TEST(test_a_scratch_too_small_falls_back_to_the_heap);
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
    RUN_TEST(test_the_two_link_shapes);
    RUN_TEST(test_an_empty_mappings_array_is_not_a_selection);
    RUN_TEST(test_command_options_are_used_when_mappings_are_absent);
    RUN_TEST(test_a_player_keeps_its_type_despite_its_mappings);
    RUN_TEST(test_the_transformed_state_is_kept);
    RUN_TEST(test_a_null_state_arrives_as_text);
    RUN_TEST(test_a_null_state_on_a_number_reads_as_zero);
    RUN_TEST(test_a_plain_number_state_keeps_its_printed_form);
    RUN_TEST(test_a_dimensioned_number_arrives_with_its_unit);
    RUN_TEST(test_widget_count_is_clamped);
    RUN_TEST(test_selection_count_is_clamped);
    RUN_TEST(test_error_object_is_a_failure);
    RUN_TEST(test_a_body_that_is_not_a_page_yields_an_empty_page);
    RUN_TEST(test_malformed_json_is_a_failure);
    RUN_TEST(test_truncated_body_is_a_failure);
    RUN_TEST(test_over_deep_nesting_is_a_failure);
    RUN_TEST(test_page_need_not_outlive_the_parse);
}
