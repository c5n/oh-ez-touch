/* Unit tests for icon_night_invert(). */

#include <unity.h>

#include "icons/icon_night.h"
#include "test_suites.hpp"

static void flip_one(uint8_t b, uint8_t g, uint8_t r, uint8_t a, uint8_t out[4])
{
    out[0] = b;
    out[1] = g;
    out[2] = r;
    out[3] = a;
    icon_night_invert(out, 1);
}

/* Black outlines turn white, white fills a dark grey -- not black, or a lamp
 * that is off vanishes into a dark tile. */
static void test_grey_flips(void)
{
    uint8_t px[4];

    flip_one(0, 0, 0, 255, px);
    TEST_ASSERT_EQUAL_UINT8(255, px[0]);
    TEST_ASSERT_EQUAL_UINT8(255, px[1]);
    TEST_ASSERT_EQUAL_UINT8(255, px[2]);

    flip_one(255, 255, 255, 255, px);
    TEST_ASSERT_EQUAL_UINT8(255 - 255 * ICON_NIGHT_FILL_SCALE / 256, px[0]);
    TEST_ASSERT_EQUAL_UINT8(px[0], px[1]);
    TEST_ASSERT_EQUAL_UINT8(px[0], px[2]);

    /* The classic set's dark outline grey comes out light. */
    flip_one(0x3D, 0x3D, 0x3D, 255, px);
    TEST_ASSERT_GREATER_THAN_UINT8(200, px[0]);
}

/* A colour that carries meaning -- a lamp that is on -- keeps it. */
static void test_colour_kept(void)
{
    uint8_t px[4];

    flip_one(0x00, 0xCC, 0xFF, 255, px);
    TEST_ASSERT_EQUAL_UINT8(0x00, px[0]);
    TEST_ASSERT_EQUAL_UINT8(0xCC, px[1]);
    TEST_ASSERT_EQUAL_UINT8(0xFF, px[2]);
}

/* Between grey and colour the flip fades: a pixel there moves, but less than
 * a grey of the same brightness would. */
static void test_partial_between(void)
{
    uint8_t px[4];
    const uint8_t chroma = (ICON_NIGHT_GREY_CHROMA + ICON_NIGHT_COLOUR_CHROMA) / 2;

    flip_one(20, 20, (uint8_t)(20 + chroma), 255, px);
    TEST_ASSERT_GREATER_THAN_UINT8(20, px[0]);
    TEST_ASSERT_LESS_THAN_UINT8(255 - 20 * ICON_NIGHT_FILL_SCALE / 256, px[0]);
}

/* Alpha is the shape; it never changes, and a transparent pixel is skipped. */
static void test_alpha_untouched(void)
{
    uint8_t px[4];

    flip_one(10, 10, 10, 128, px);
    TEST_ASSERT_EQUAL_UINT8(128, px[3]);

    flip_one(10, 10, 10, 0, px);
    TEST_ASSERT_EQUAL_UINT8(10, px[0]);
    TEST_ASSERT_EQUAL_UINT8(0, px[3]);
}

/* A whole row, as the decoder hands it over. */
static void test_row(void)
{
    uint8_t row[3 * 4] = {0, 0, 0, 255, 0, 0, 255, 255, 255, 255, 255, 0};

    icon_night_invert(row, 3);
    TEST_ASSERT_EQUAL_UINT8(255, row[0]);  /* black flipped */
    TEST_ASSERT_EQUAL_UINT8(0, row[4]);    /* red kept      */
    TEST_ASSERT_EQUAL_UINT8(255, row[6]);
    TEST_ASSERT_EQUAL_UINT8(255, row[8]);  /* transparent   */
}

void test_icon_night_run(void)
{
    RUN_TEST(test_grey_flips);
    RUN_TEST(test_colour_kept);
    RUN_TEST(test_partial_between);
    RUN_TEST(test_alpha_untouched);
    RUN_TEST(test_row);
}
