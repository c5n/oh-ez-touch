/* Unit tests for the request tokeniser in main/testif/testif_parse.c.
 *
 * The simulator's control interface reads whole commands off a UDP socket, and
 * this is the part that decides where one token ends and the next begins. It
 * qualifies for this binary on the same terms as web/multipart.c: it touches
 * neither LVGL nor a socket, and it parses bytes that a caller outside the
 * process chose.
 *
 * The interesting cases are the ones a hand-typed `nc -u` line produces and the
 * CLI never does -- a trailing newline, a doubled space, an unclosed quote, a
 * lone `@` -- plus the two limits, because a datagram is free to carry more
 * tokens than argv holds.
 */

#include <unity.h>

#include <string.h>

#include "test_suites.hpp"
#include "testif/testif_parse.h"

/* testif_parse() terminates tokens in place, so every case needs a buffer it
 * is allowed to write to. */
static testif_cmd_t parse(char *buffer, const char *request)
{
    testif_cmd_t cmd;

    strcpy(buffer, request);
    testif_parse(buffer, &cmd);

    return cmd;
}

static void test_plain_command(void)
{
    char         buffer[TESTIF_LINE_MAX];
    testif_cmd_t cmd = parse(buffer, "tap 160 120");

    TEST_ASSERT_EQUAL_UINT(3, cmd.argc);
    TEST_ASSERT_EQUAL_STRING("tap", cmd.argv[0]);
    TEST_ASSERT_EQUAL_STRING("160", cmd.argv[1]);
    TEST_ASSERT_EQUAL_STRING("120", cmd.argv[2]);
    TEST_ASSERT_NULL(cmd.id);
    TEST_ASSERT_FALSE(cmd.truncated);
}

/* A line typed into `nc -u` carries the newline the CLI does not send, and
 * both have to reach the same command. */
static void test_trailing_newline(void)
{
    char         buffer[TESTIF_LINE_MAX];
    testif_cmd_t cmd = parse(buffer, "ping\r\n");

    TEST_ASSERT_EQUAL_UINT(1, cmd.argc);
    TEST_ASSERT_EQUAL_STRING("ping", cmd.argv[0]);
}

static void test_runs_of_separators(void)
{
    char         buffer[TESTIF_LINE_MAX];
    testif_cmd_t cmd = parse(buffer, "  tap \t 160   120  ");

    TEST_ASSERT_EQUAL_UINT(3, cmd.argc);
    TEST_ASSERT_EQUAL_STRING("tap", cmd.argv[0]);
    TEST_ASSERT_EQUAL_STRING("120", cmd.argv[2]);
}

static void test_correlation_id(void)
{
    char         buffer[TESTIF_LINE_MAX];
    testif_cmd_t cmd = parse(buffer, "@42 screen");

    TEST_ASSERT_EQUAL_STRING("42", cmd.id);
    TEST_ASSERT_EQUAL_UINT(1, cmd.argc);
    TEST_ASSERT_EQUAL_STRING("screen", cmd.argv[0]);
}

/* Only the first token is an id. A setting value is free to begin with '@',
 * and an MQTT password plausibly does. */
static void test_at_sign_later_is_an_argument(void)
{
    char         buffer[TESTIF_LINE_MAX];
    testif_cmd_t cmd = parse(buffer, "set mqtt_pass @secret");

    TEST_ASSERT_NULL(cmd.id);
    TEST_ASSERT_EQUAL_UINT(3, cmd.argc);
    TEST_ASSERT_EQUAL_STRING("@secret", cmd.argv[2]);
}

static void test_bare_at_is_not_an_id(void)
{
    char         buffer[TESTIF_LINE_MAX];
    testif_cmd_t cmd = parse(buffer, "@ ping");

    TEST_ASSERT_NULL(cmd.id);
    TEST_ASSERT_EQUAL_UINT(2, cmd.argc);
    TEST_ASSERT_EQUAL_STRING("@", cmd.argv[0]);
}

static void test_quoted_value(void)
{
    char         buffer[TESTIF_LINE_MAX];
    testif_cmd_t cmd = parse(buffer, "set hostname \"two words\"");

    TEST_ASSERT_EQUAL_UINT(3, cmd.argc);
    TEST_ASSERT_EQUAL_STRING("two words", cmd.argv[2]);
}

/* Lenient on purpose: a quote nobody closed ends at the end of the line rather
 * than losing the token, because the alternative is a command that fails with
 * nothing useful to say. */
static void test_unclosed_quote_runs_to_the_end(void)
{
    char         buffer[TESTIF_LINE_MAX];
    testif_cmd_t cmd = parse(buffer, "set hostname \"no end");

    TEST_ASSERT_EQUAL_UINT(3, cmd.argc);
    TEST_ASSERT_EQUAL_STRING("no end", cmd.argv[2]);
}

static void test_empty_request(void)
{
    char         buffer[TESTIF_LINE_MAX];
    testif_cmd_t cmd;

    strcpy(buffer, "   ");

    TEST_ASSERT_FALSE(testif_parse(buffer, &cmd));
    TEST_ASSERT_EQUAL_UINT(0, cmd.argc);
}

/* An id and nothing else is not a command, and the caller has to be able to
 * tell -- it still owes that id a reply. */
static void test_id_with_no_command(void)
{
    char         buffer[TESTIF_LINE_MAX];
    testif_cmd_t cmd;

    strcpy(buffer, "@9");

    TEST_ASSERT_FALSE(testif_parse(buffer, &cmd));
    TEST_ASSERT_EQUAL_UINT(0, cmd.argc);
}

/* Over the limit the request is refused rather than silently shortened: a
 * swipe whose last argument was dropped would run for the default duration and
 * look like it worked. */
static void test_too_many_tokens(void)
{
    char         buffer[TESTIF_LINE_MAX];
    testif_cmd_t cmd = parse(buffer, "a b c d e f g h i j k");

    TEST_ASSERT_TRUE(cmd.truncated);
    TEST_ASSERT_EQUAL_UINT(TESTIF_ARGV_MAX, cmd.argc);
}

/* The longest real request, which must not be near the limit. */
static void test_longest_real_request(void)
{
    char         buffer[TESTIF_LINE_MAX];
    testif_cmd_t cmd = parse(buffer, "@1 swipe 300 120 20 120 200");

    TEST_ASSERT_FALSE(cmd.truncated);
    TEST_ASSERT_EQUAL_UINT(6, cmd.argc);
    TEST_ASSERT_EQUAL_STRING("200", cmd.argv[5]);
}

static void test_null_line(void)
{
    testif_cmd_t cmd;

    TEST_ASSERT_FALSE(testif_parse(NULL, &cmd));
    TEST_ASSERT_EQUAL_UINT(0, cmd.argc);
}

void test_testif_parse_run(void)
{
    RUN_TEST(test_plain_command);
    RUN_TEST(test_trailing_newline);
    RUN_TEST(test_runs_of_separators);
    RUN_TEST(test_correlation_id);
    RUN_TEST(test_at_sign_later_is_an_argument);
    RUN_TEST(test_bare_at_is_not_an_id);
    RUN_TEST(test_quoted_value);
    RUN_TEST(test_unclosed_quote_runs_to_the_end);
    RUN_TEST(test_empty_request);
    RUN_TEST(test_id_with_no_command);
    RUN_TEST(test_too_many_tokens);
    RUN_TEST(test_longest_real_request);
    RUN_TEST(test_null_line);
}
