/* Unit tests for json_squeeze().
 *
 * What an Image item does to a sitemap page: its state is the picture, and it
 * arrives inside the page twice. The page has to come out the other side
 * shorter, still JSON, and with everything else in it untouched -- whatever
 * the reads it arrived in happened to split.
 */

#include <string.h>
#include <string>

#include <unity.h>
#include <ArduinoJson.h>

#include "openhab/json_squeeze.h"
#include "test_suites.hpp"

/* Run `in` through the filter in reads of `chunk` bytes. */
static std::string squeeze(const std::string &in, size_t chunk)
{
    struct json_squeeze_s s;
    std::string out;

    json_squeeze_reset(&s);

    for (size_t at = 0; at < in.size(); at += chunk)
    {
        std::string part = in.substr(at, chunk);
        size_t n = json_squeeze(&s, &part[0], part.size());

        out.append(part.data(), n);
    }

    return out;
}

static std::string page_with_image(size_t state_len)
{
    std::string state = "data:image/jpeg;base64,";

    while (state.size() < state_len)
        state += "/9j/4AAQSkZJRg+=";

    return "{\"widgets\":[{\"type\":\"Image\",\"label\":\"Door\",\"item\":{\"state\":\""
           + state + "\",\"lastState\":\"" + state
           + "\",\"name\":\"Bell\"}},{\"type\":\"Switch\",\"label\":\"Light\"}]}";
}

static void test_short_json_passes_unchanged(void)
{
    std::string in = "{\"a\":\"b \\\" c\",\"n\":[1,2,{\"x\":\"\\u00e9\"}]}";

    TEST_ASSERT_EQUAL_STRING(in.c_str(), squeeze(in, 1).c_str());
    TEST_ASSERT_EQUAL_STRING(in.c_str(), squeeze(in, 1000).c_str());
}

static void test_long_strings_are_cut_and_still_parse(void)
{
    std::string in = page_with_image(60000);

    for (size_t chunk : {1u, 7u, 512u, 1024u, 100000u})
    {
        std::string out = squeeze(in, chunk);

        TEST_ASSERT_LESS_THAN(2 * JSON_SQUEEZE_STRING_MAX + 200, out.size());

        JsonDocument doc;

        TEST_ASSERT_EQUAL(DeserializationError::Ok, deserializeJson(doc, out).code());

        const char *state = doc["widgets"][0]["item"]["state"];

        TEST_ASSERT_EQUAL(JSON_SQUEEZE_STRING_MAX, strlen(state));
        TEST_ASSERT_EQUAL(0, strncmp(state, "data:image/jpeg;base64,", 23));
        TEST_ASSERT_EQUAL_STRING("Bell", doc["widgets"][0]["item"]["name"]);
        TEST_ASSERT_EQUAL_STRING("Light", doc["widgets"][1]["label"]);
    }
}

/* A cut that lands on an escape would leave "\" in front of the closing quote,
 * or half of a \u sequence; both are invalid JSON. */
static void test_cut_never_splits_an_escape(void)
{
    for (size_t pad = 0; pad < 8; ++pad)
    {
        std::string body(JSON_SQUEEZE_STRING_MAX - 3 + pad, 'a');

        body += "\\\"\\u00e9\\\\";
        body += std::string(100, 'b');

        std::string in = "{\"s\":\"" + body + "\",\"t\":\"ok\"}";
        std::string out = squeeze(in, 5);

        JsonDocument doc;

        TEST_ASSERT_EQUAL_MESSAGE(DeserializationError::Ok,
                                  deserializeJson(doc, out).code(), out.c_str());
        TEST_ASSERT_EQUAL_STRING("ok", doc["t"]);
    }
}

static void test_cut_never_splits_utf8(void)
{
    std::string body(JSON_SQUEEZE_STRING_MAX - 1, 'a');

    for (int i = 0; i < 50; ++i)
        body += "\xc3\xa9";

    std::string out = squeeze("\"" + body + "\"", 3);

    /* The last kept byte before the closing quote is not a lead byte left
     * without its continuation. */
    unsigned char last = (unsigned char)out[out.size() - 2];

    TEST_ASSERT_FALSE((last & 0xE0u) == 0xC0u);
}

/* An event stream is lines; a line that ended inside a cut string -- it
 * cannot in JSON, but a stream can be cut off -- must not swallow the next. */
static void test_newline_starts_afresh(void)
{
    std::string in = "data: {\"s\":\"" + std::string(2000, 'x') + "\n"
                     "data: {\"t\":\"kept\"}\n";
    std::string out = squeeze(in, 64);

    TEST_ASSERT_NOT_EQUAL(std::string::npos, out.find("data: {\"t\":\"kept\"}\n"));
}

void test_json_squeeze_run(void)
{
    RUN_TEST(test_short_json_passes_unchanged);
    RUN_TEST(test_long_strings_are_cut_and_still_parse);
    RUN_TEST(test_cut_never_splits_an_escape);
    RUN_TEST(test_cut_never_splits_utf8);
    RUN_TEST(test_newline_starts_afresh);
}
