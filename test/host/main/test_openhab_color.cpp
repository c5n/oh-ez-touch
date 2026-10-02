/* Unit tests for openhab_color_parse().
 *
 * What a sitemap's labelcolor, valuecolor and iconcolor rules arrive as: a
 * name, "#rrggbb", or whatever somebody typed. The last must cost the colour
 * and nothing else.
 */

#include <unity.h>

#include "openhab/openhab_color.h"
#include "test_suites.hpp"

static void test_names_are_the_css_colours(void)
{
    TEST_ASSERT_EQUAL_HEX32(0xFF0000, openhab_color_parse("red"));
    TEST_ASSERT_EQUAL_HEX32(0x008000, openhab_color_parse("green"));
    TEST_ASSERT_EQUAL_HEX32(0xFFD700, openhab_color_parse("gold"));
    TEST_ASSERT_EQUAL_HEX32(0x000000, openhab_color_parse("black"));
    TEST_ASSERT_EQUAL_HEX32(0x808080, openhab_color_parse("grey"));
}

static void test_names_ignore_case(void)
{
    TEST_ASSERT_EQUAL_HEX32(0xFFA500, openhab_color_parse("Orange"));
    TEST_ASSERT_EQUAL_HEX32(0x00FFFF, openhab_color_parse("AQUA"));
}

static void test_hex_long_and_short(void)
{
    TEST_ASSERT_EQUAL_HEX32(0x12AB9F, openhab_color_parse("#12ab9F"));
    TEST_ASSERT_EQUAL_HEX32(0xFF8800, openhab_color_parse("#f80"));
    TEST_ASSERT_EQUAL_HEX32(0x000000, openhab_color_parse("#000000"));
}

static void test_anything_else_is_no_colour(void)
{
    TEST_ASSERT_EQUAL_HEX32(OPENHAB_COLOR_NONE, openhab_color_parse(NULL));
    TEST_ASSERT_EQUAL_HEX32(OPENHAB_COLOR_NONE, openhab_color_parse(""));
    TEST_ASSERT_EQUAL_HEX32(OPENHAB_COLOR_NONE, openhab_color_parse("reddish"));
    TEST_ASSERT_EQUAL_HEX32(OPENHAB_COLOR_NONE, openhab_color_parse("#12345"));
    TEST_ASSERT_EQUAL_HEX32(OPENHAB_COLOR_NONE, openhab_color_parse("#1234567"));
    TEST_ASSERT_EQUAL_HEX32(OPENHAB_COLOR_NONE, openhab_color_parse("#12345g"));
    TEST_ASSERT_EQUAL_HEX32(OPENHAB_COLOR_NONE, openhab_color_parse("#"));
    TEST_ASSERT_EQUAL_HEX32(OPENHAB_COLOR_NONE, openhab_color_parse("12ab9f"));
}

void test_openhab_color_run(void)
{
    RUN_TEST(test_names_are_the_css_colours);
    RUN_TEST(test_names_ignore_case);
    RUN_TEST(test_hex_long_and_short);
    RUN_TEST(test_anything_else_is_no_colour);
}
