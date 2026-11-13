/* Unit tests for Item::stateUrl() and Item::iconUrl().
 *
 * The client task carries URLs and nothing else, so these two are the whole of
 * what it is ever asked to fetch for an item. Both refuse rather than produce
 * something unusable: a URL that has been silently truncated fails every
 * request it is used for, and enough failures restart the panel.
 */

#include <unity.h>

#include "openhab/openhab_connector.hpp"
#include "test_suites.hpp"

#include <string.h>

/* openHAB's own port. It used to read 8780 here, which is the *panel's* web
 * server -- harmless to the assertions, and misleading to anyone reading them
 * for what a configured website looks like. */
#define WEBSITE "http://openhabian:8080"

static void test_state_url(void)
{
    Item item;
    char url[STR_URL_LEN];

    item.cleanItem();
    item.setLink(WEBSITE "/rest/items/Light_Hallway");

    TEST_ASSERT_TRUE(item.stateUrl(url, sizeof(url)));
    TEST_ASSERT_EQUAL_STRING(WEBSITE "/rest/items/Light_Hallway/state", url);
}

/* Link and group widgets often carry only a page link. Asking for "/state"
 * then requests something else entirely, and would fail on every poll -- which
 * is why openhab_ui_loop() skips those types, and why this refuses as well. */
static void test_state_url_without_a_link(void)
{
    Item item;
    char url[STR_URL_LEN];

    item.cleanItem();

    TEST_ASSERT_FALSE(item.stateUrl(url, sizeof(url)));
}

static void test_state_url_that_does_not_fit(void)
{
    Item item;
    char url[16];

    item.cleanItem();
    item.setLink(WEBSITE "/rest/items/Light_Hallway");

    TEST_ASSERT_FALSE(item.stateUrl(url, sizeof(url)));
}

static void test_icon_url(void)
{
    Item item;
    char url[STR_URL_LEN];

    item.cleanItem();
    item.setIconName("light");
    item.setStateText("ON");

    TEST_ASSERT_TRUE(item.iconUrl(WEBSITE, url, sizeof(url)));
    TEST_ASSERT_EQUAL_STRING(WEBSITE "/icon/light?state=ON&format=png", url);
}

/* Plenty of widgets have no icon, and that is not a failure to report -- it
 * just means there is nothing to fetch. Building the URL anyway would ask
 * openHAB for "/icon/?state=..." once per poll. */
/* A static icon is asked for without the state: the picture does not depend
 * on it, and a state with a space in it would make a URL the client refuses. */
static void test_icon_url_of_a_static_icon(void)
{
    Item item;
    char url[STR_URL_LEN];

    item.cleanItem();
    item.setIconName("temperature");
    item.setStateText("Hello World");
    item.setStaticIcon(true);

    TEST_ASSERT_TRUE(item.iconUrl(WEBSITE, url, sizeof(url)));
    TEST_ASSERT_EQUAL_STRING(WEBSITE "/icon/temperature?format=png", url);
}

static void test_icon_url_without_a_name(void)
{
    Item item;
    char url[STR_URL_LEN];

    item.cleanItem();
    item.setStateText("ON");

    TEST_ASSERT_FALSE(item.iconUrl(WEBSITE, url, sizeof(url)));
}

/* An item with no state yet still has an icon: openHAB serves the plain one
 * for an empty state. Checked against openHAB 5.2.1 -- "/icon/light?state="
 * and "/icon/light" both answer 200 with the same bytes as "?state=ON". */
static void test_icon_url_without_a_state(void)
{
    Item item;
    char url[STR_URL_LEN];

    item.cleanItem();
    item.setIconName("light");

    TEST_ASSERT_TRUE(item.iconUrl(WEBSITE, url, sizeof(url)));
    TEST_ASSERT_EQUAL_STRING(WEBSITE "/icon/light?state=&format=png", url);
}

/* The regression this pair was introduced for. getIcon() built into a
 * STR_LINK_LEN (128) buffer a URL that needs 217 at the field widths this
 * header declares, so a long host plus a long state truncated it in silence.
 * A truncation is now reported, and STR_URL_LEN is wide enough that the
 * widest legal item does not reach it. */
static void test_icon_url_at_the_widest_legal_item(void)
{
    Item item;
    char url[STR_URL_LEN];
    /* STR_WEBSITE_LEN in openhab_ui.cpp, which is where current_website is
     * built. Repeated rather than included, because that file is LVGL from
     * the first line and this test deliberately links no UI. */
    char website[128];
    char name[STR_ICON_NAME_LEN];
    char state[STR_STATE_TEXT_LEN];

    memset(website, 'w', sizeof(website) - 1);
    website[sizeof(website) - 1] = '\0';

    memset(name, 'n', sizeof(name) - 1);
    name[sizeof(name) - 1] = '\0';

    memset(state, 's', sizeof(state) - 1);
    state[sizeof(state) - 1] = '\0';

    item.cleanItem();
    item.setIconName(name);
    item.setStateText(state);

    TEST_ASSERT_TRUE_MESSAGE(item.iconUrl(website, url, sizeof(url)),
                             "STR_URL_LEN is too narrow for the widest item");

    /* And one byte narrower must refuse rather than truncate. */
    char tight[64];
    TEST_ASSERT_FALSE(item.iconUrl(website, tight, sizeof(tight)));
}

/* A state is percent-encoded into the query string.
 *
 * Driven against openHAB 5.2.1: esp_http_client_set_url() refuses a URL with
 * a raw space or a raw "ü" in it --
 *
 *     E HTTP_CLIENT:  Error parse url http://h:8080/icon/trafficlight?state=grün&format=png
 *
 * so the request never reached the server, the keep-alive was torn down, and
 * the tile kept its old icon. Encoded, /icon/text?state=Hello%20World answers
 * 200. An "&" in a state was the quieter version: the parameter ended early,
 * and "a&b" fetched the icon for "a". */
static void test_a_state_with_a_space_is_encoded(void)
{
    Item item;
    char url[STR_URL_LEN];

    item.cleanItem();
    item.setIconName("text");
    item.setStateText("Hello World");

    TEST_ASSERT_TRUE(item.iconUrl(WEBSITE, url, sizeof(url)));
    TEST_ASSERT_EQUAL_STRING(WEBSITE "/icon/text?state=Hello%20World&format=png", url);
}

/* Every byte of a UTF-8 sequence, the bug as it was seen on the panel. */
static void test_a_state_with_an_umlaut_is_encoded(void)
{
    Item item;
    char url[STR_URL_LEN];

    item.cleanItem();
    item.setIconName("trafficlight");
    item.setStateText("gr\xC3\xBCn");

    TEST_ASSERT_TRUE(item.iconUrl(WEBSITE, url, sizeof(url)));
    TEST_ASSERT_EQUAL_STRING(WEBSITE "/icon/trafficlight?state=gr%C3%BCn&format=png", url);
}

/* The reserved characters of a query string, and the unreserved ones left
 * alone. */
static void test_reserved_characters_are_encoded(void)
{
    Item item;
    char url[STR_URL_LEN];

    item.cleanItem();
    item.setIconName("text");
    item.setStateText("a&b=c%d+e/f-g.h_i~j");

    TEST_ASSERT_TRUE(item.iconUrl(WEBSITE, url, sizeof(url)));
    TEST_ASSERT_EQUAL_STRING(WEBSITE "/icon/text?state=a%26b%3Dc%25d%2Be%2Ff-g.h_i~j&format=png",
                             url);
}

/* Encoding triples a reserved character, and STR_URL_LEN is not widened for
 * it: at the widest host and icon name, a state of reserved characters only
 * does not fit. That must be a refusal, never a truncated URL. */
static void test_an_encoded_state_that_does_not_fit_is_refused(void)
{
    Item item;
    char url[STR_URL_LEN];
    char website[128];
    char name[STR_ICON_NAME_LEN];
    char state[STR_STATE_TEXT_LEN];

    memset(website, 'w', sizeof(website) - 1);
    website[sizeof(website) - 1] = '\0';

    memset(name, 'n', sizeof(name) - 1);
    name[sizeof(name) - 1] = '\0';

    memset(state, ' ', sizeof(state) - 1);
    state[sizeof(state) - 1] = '\0';

    item.cleanItem();
    item.setIconName(name);
    item.setStateText(state);

    TEST_ASSERT_FALSE(item.iconUrl(website, url, sizeof(url)));
}

/* What the event stream knows an item by. */
static void test_name(void)
{
    Item item;
    char name[64];

    item.cleanItem();
    item.setLink(WEBSITE "/rest/items/Light_Hallway");

    TEST_ASSERT_TRUE(item.name(name, sizeof(name)));
    TEST_ASSERT_EQUAL_STRING("Light_Hallway", name);
}

static void test_name_without_a_link(void)
{
    Item item;
    char name[64];

    item.cleanItem();

    TEST_ASSERT_FALSE(item.name(name, sizeof(name)));
}

/* Refused rather than cut short: a truncated name would be somebody else's. */
static void test_name_that_does_not_fit(void)
{
    Item item;
    char name[8];

    item.cleanItem();
    item.setLink(WEBSITE "/rest/items/Light_Hallway");

    TEST_ASSERT_FALSE(item.name(name, sizeof(name)));
}

void test_item_urls_run(void)
{
    RUN_TEST(test_name);
    RUN_TEST(test_name_without_a_link);
    RUN_TEST(test_icon_url_of_a_static_icon);
    RUN_TEST(test_name_that_does_not_fit);
    RUN_TEST(test_state_url);
    RUN_TEST(test_state_url_without_a_link);
    RUN_TEST(test_state_url_that_does_not_fit);
    RUN_TEST(test_icon_url);
    RUN_TEST(test_icon_url_without_a_name);
    RUN_TEST(test_icon_url_without_a_state);
    RUN_TEST(test_a_state_with_a_space_is_encoded);
    RUN_TEST(test_a_state_with_an_umlaut_is_encoded);
    RUN_TEST(test_reserved_characters_are_encoded);
    RUN_TEST(test_an_encoded_state_that_does_not_fit_is_refused);
    RUN_TEST(test_icon_url_at_the_widest_legal_item);
}
