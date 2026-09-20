/* Unit tests for SitemapList::parse().
 *
 * What the panel offers when it asks a server which sitemaps it has. The body
 * is GET /rest/sitemaps, and the shape asserted here is the one openHAB 5.2.1
 * actually sends -- captured from the local test server on 2026-09-18, and
 * kept in main/sim/sitemap_fixture.cpp as the simulator's offline answer, so
 * these tests and the simulator's list are fed by the same bytes.
 *
 * The parse runs under an ArduinoJson filter, which is the one thing here that
 * would fail silently: with the filter wrong every field would simply be
 * missing, and an empty list looks exactly like a server that has no sitemaps.
 * Half of what follows is therefore about the two keys surviving it and the
 * "homepage" object next to them not.
 *
 * Run with:
 *   cd test/host && idf.py build && ./build/oh-ez-touch-host-test.elf
 */

#include <unity.h>

#include "openhab/openhab_connector.hpp"
#include "sim/sitemap_fixture.hpp"
#include "test_suites.hpp"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int parse_text(SitemapList &list, const char *body)
{
    return list.parse(body, strlen(body));
}

/* The real thing, minus two of the three entries. Written out here rather than
 * taken from the fixture so that one test is against bytes nobody has tidied:
 * the key order is the server's, and "homepage" comes before "name". */
static const char body_real[] =
    "[{\"link\":\"http://localhost:8080/rest/sitemaps/demo\",\"homepage\":"
    "{\"link\":\"http://localhost:8080/rest/sitemaps/demo/demo\",\"leaf\":false,"
    "\"timeout\":false,\"widgets\":[]},\"name\":\"demo\",\"label\":\"OhEzTouch Test\"}]";

static void test_a_real_response_yields_name_and_label(void)
{
    SitemapList list;

    TEST_ASSERT_EQUAL_INT(0, parse_text(list, body_real));
    TEST_ASSERT_EQUAL_UINT(1, list.getCount());
    TEST_ASSERT_EQUAL_UINT(1, list.getTotal());
    TEST_ASSERT_EQUAL_STRING("demo", list.getName(0));
    TEST_ASSERT_EQUAL_STRING("OhEzTouch Test", list.getLabel(0));
}

/* The simulator's own answer, which is what offline mode shows and therefore
 * the list anyone working on the settings screen sees. */
static void test_the_fixture_list_parses(void)
{
    SitemapList list;
    const char *body = sim_sitemap_fixture_list();

    TEST_ASSERT_NOT_NULL_MESSAGE(body, "no fixture list of sitemaps");
    TEST_ASSERT_EQUAL_INT(0, parse_text(list, body));
    TEST_ASSERT_EQUAL_UINT(3, list.getCount());
    TEST_ASSERT_EQUAL_STRING("demo", list.getName(0));
    TEST_ASSERT_EQUAL_STRING("kitchen", list.getName(1));
    TEST_ASSERT_EQUAL_STRING("garden", list.getName(2));
}

/* openHAB does not require a sitemap to have a label, and a blank row in the
 * list would name nothing at all. */
static void test_a_missing_label_falls_back_to_the_name(void)
{
    SitemapList list;

    TEST_ASSERT_EQUAL_INT(0, parse_text(list, "[{\"name\":\"garden\"}]"));
    TEST_ASSERT_EQUAL_STRING("garden", list.getName(0));
    TEST_ASSERT_EQUAL_STRING("garden", list.getLabel(0));
}

/* An entry with no name cannot be requested and cannot be stored, so it is not
 * a choice -- and it is not counted as one either, or the count would report a
 * sitemap nobody can pick. */
static void test_a_nameless_entry_is_not_a_choice(void)
{
    SitemapList list;

    TEST_ASSERT_EQUAL_INT(0, parse_text(list, "[{\"label\":\"Nameless\"},{\"name\":\"ok\"}]"));
    TEST_ASSERT_EQUAL_UINT(1, list.getCount());
    TEST_ASSERT_EQUAL_UINT(1, list.getTotal());
    TEST_ASSERT_EQUAL_STRING("ok", list.getName(0));
}

/* A name too long for the char[32] Config keeps it in. Offering it would store
 * a truncated name and fetch a page that does not exist, so it is left out --
 * but it is counted, because the count and the total disagreeing is how both
 * front ends say "there are more than these". */
static void test_an_over_long_name_is_counted_but_not_offered(void)
{
    SitemapList list;
    char        body[128];

    snprintf(body, sizeof(body), "[{\"name\":\"%.*s\"},{\"name\":\"short\"}]",
             STR_SITEMAP_NAME_LEN, "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");

    TEST_ASSERT_EQUAL_INT(0, parse_text(list, body));
    TEST_ASSERT_EQUAL_UINT(1, list.getCount());
    TEST_ASSERT_EQUAL_UINT(2, list.getTotal());
    TEST_ASSERT_EQUAL_STRING("short", list.getName(0));
}

/* A label longer than the list holds is truncated rather than dropped: unlike
 * the name it is only ever printed. */
static void test_an_over_long_label_is_truncated(void)
{
    SitemapList list;
    char        body[256];
    char        label[STR_LABEL_LEN + 20];

    memset(label, 'L', sizeof(label) - 1);
    label[sizeof(label) - 1] = '\0';

    snprintf(body, sizeof(body), "[{\"name\":\"demo\",\"label\":\"%s\"}]", label);

    TEST_ASSERT_EQUAL_INT(0, parse_text(list, body));
    TEST_ASSERT_EQUAL_UINT(STR_LABEL_LEN - 1, strlen(list.getLabel(0)));
}

static void test_the_list_is_clamped(void)
{
    SitemapList list;
    char        body[1024];
    size_t      len = 0;

    len += (size_t)snprintf(body + len, sizeof(body) - len, "%s", "[");

    for (size_t i = 0; i < SITEMAP_LIST_COUNT_MAX + 3; i++)
        len += (size_t)snprintf(body + len, sizeof(body) - len, "%s{\"name\":\"map%u\"}",
                                (i == 0) ? "" : ",", (unsigned)i);

    snprintf(body + len, sizeof(body) - len, "%s", "]");

    TEST_ASSERT_EQUAL_INT(0, parse_text(list, body));
    TEST_ASSERT_EQUAL_UINT(SITEMAP_LIST_COUNT_MAX, list.getCount());
    TEST_ASSERT_EQUAL_UINT(SITEMAP_LIST_COUNT_MAX + 3, list.getTotal());

    /* The ones that fitted are the first ones, in the order the server listed
     * them. */
    TEST_ASSERT_EQUAL_STRING("map0", list.getName(0));
    TEST_ASSERT_EQUAL_STRING("map11", list.getName(SITEMAP_LIST_COUNT_MAX - 1));
}

/* Out of range reads print as nothing rather than crashing a caller that did
 * not check -- both front ends print these straight into a row. */
static void test_an_index_past_the_end_is_empty(void)
{
    SitemapList list;

    TEST_ASSERT_EQUAL_INT(0, parse_text(list, "[{\"name\":\"demo\"}]"));
    TEST_ASSERT_EQUAL_STRING("", list.getName(1));
    TEST_ASSERT_EQUAL_STRING("", list.getLabel(SITEMAP_LIST_COUNT_MAX + 5));
}

static void test_an_empty_array_is_a_server_with_no_sitemaps(void)
{
    SitemapList list;

    TEST_ASSERT_EQUAL_INT(0, parse_text(list, "[]"));
    TEST_ASSERT_EQUAL_UINT(0, list.getCount());
    TEST_ASSERT_EQUAL_UINT(0, list.getTotal());
}

/* Not an array: an openHAB error object, or something else entirely listening
 * at that address. Both are failures, and neither may leave a list behind. */
static void test_an_object_is_a_failure(void)
{
    SitemapList list;

    TEST_ASSERT_EQUAL_INT(-1,
                          parse_text(list, "{\"error\":{\"message\":\"nope\",\"http-code\":404}}"));
    TEST_ASSERT_EQUAL_UINT(0, list.getCount());
}

static void test_malformed_json_is_a_failure(void)
{
    SitemapList list;

    TEST_ASSERT_EQUAL_INT(-1, parse_text(list, "[{\"name\":\"demo\""));
    TEST_ASSERT_EQUAL_UINT(0, list.getCount());
}

/* A failed refresh must not leave the previous server's sitemaps on offer:
 * picking one of them would point the panel at a sitemap the host being
 * configured does not have. */
static void test_a_failure_empties_a_filled_list(void)
{
    SitemapList list;

    TEST_ASSERT_EQUAL_INT(0, parse_text(list, body_real));
    TEST_ASSERT_EQUAL_UINT(1, list.getCount());

    TEST_ASSERT_EQUAL_INT(-1, parse_text(list, "not json at all"));
    TEST_ASSERT_EQUAL_UINT(0, list.getCount());
    TEST_ASSERT_EQUAL_UINT(0, list.getTotal());
    TEST_ASSERT_EQUAL_STRING("", list.getName(0));
}

/* The body is parsed in place and nothing may point into it afterwards -- the
 * client frees the payload as soon as this returns. */
static void test_the_body_need_not_outlive_the_parse(void)
{
    SitemapList list;
    size_t      len = strlen(body_real);
    char       *copy = (char *)malloc(len + 1);

    TEST_ASSERT_NOT_NULL(copy);
    memcpy(copy, body_real, len + 1);

    TEST_ASSERT_EQUAL_INT(0, list.parse(copy, len));

    memset(copy, 'x', len);
    free(copy);

    TEST_ASSERT_EQUAL_STRING("demo", list.getName(0));
    TEST_ASSERT_EQUAL_STRING("OhEzTouch Test", list.getLabel(0));
}

void test_sitemap_list_run(void)
{
    RUN_TEST(test_a_real_response_yields_name_and_label);
    RUN_TEST(test_the_fixture_list_parses);
    RUN_TEST(test_a_missing_label_falls_back_to_the_name);
    RUN_TEST(test_a_nameless_entry_is_not_a_choice);
    RUN_TEST(test_an_over_long_name_is_counted_but_not_offered);
    RUN_TEST(test_an_over_long_label_is_truncated);
    RUN_TEST(test_the_list_is_clamped);
    RUN_TEST(test_an_index_past_the_end_is_empty);
    RUN_TEST(test_an_empty_array_is_a_server_with_no_sitemaps);
    RUN_TEST(test_an_object_is_a_failure);
    RUN_TEST(test_malformed_json_is_a_failure);
    RUN_TEST(test_a_failure_empties_a_filled_list);
    RUN_TEST(test_the_body_need_not_outlive_the_parse);
}
