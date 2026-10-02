/* Unit tests for image_decode_jpeg().
 *
 * What a doorbell sends: a snapshot far larger than the screen, which has to
 * come out inside the box the caller asked for, the right way up and the
 * right colours, without the whole of it ever being in memory. The fixtures
 * in ../fixtures are four flat quadrants -- red, green / blue, white -- made
 * with Pillow at quality 75, so a pixel in the middle of each quadrant says
 * whether the picture was placed and coloured correctly. One is progressive,
 * which TJpgDec cannot read, and has to fail cleanly.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

#include <unity.h>

#include "openhab/image_decode.h"
#include "test_suites.hpp"

/* A stream that hands the bytes over in reads of at most `chunk`, the way a
 * socket does, and can be told to end early. */
struct mem_stream_s
{
    std::vector<uint8_t> data;
    size_t at;
    size_t end;
    size_t chunk;
};

static size_t mem_read(void *ctx, uint8_t *buf, size_t len)
{
    struct mem_stream_s *m = (struct mem_stream_s *)ctx;
    size_t left = m->end - m->at;

    if (len > left)
        len = left;

    if (len > m->chunk)
        len = m->chunk;

    if (buf != NULL)
        memcpy(buf, m->data.data() + m->at, len);

    m->at += len;

    return len;
}

static struct mem_stream_s load(const char *name, size_t chunk = 1460)
{
    char path[512];
    struct mem_stream_s m = {};

    snprintf(path, sizeof(path), "%s/%s", TEST_FIXTURE_DIR, name);

    FILE *f = fopen(path, "rb");

    TEST_ASSERT_NOT_NULL_MESSAGE(f, path);

    int c;

    while ((c = fgetc(f)) != EOF)
        m.data.push_back((uint8_t)c);

    fclose(f);

    m.end = m.data.size();
    m.chunk = chunk;

    return m;
}

/* RGB565 back to 8 bit a channel, roughly. */
static void rgb_at(const struct image_decode_s *img, unsigned x, unsigned y,
                   unsigned *r, unsigned *g, unsigned *b)
{
    uint16_t p = img->pixels[y * img->width + x];

    *r = ((p >> 11) & 0x1F) * 255 / 31;
    *g = ((p >> 5) & 0x3F) * 255 / 63;
    *b = (p & 0x1F) * 255 / 31;
}

/* Each quadrant's middle is its colour, give or take JPEG's error. */
static void assert_quadrants(const struct image_decode_s *img)
{
    static const struct
    {
        unsigned fx, fy, r, g, b;
    } want[] = {
        {1, 1, 255, 0, 0},
        {3, 1, 0, 255, 0},
        {1, 3, 0, 0, 255},
        {3, 3, 255, 255, 255},
    };

    for (const auto &w : want)
    {
        unsigned r, g, b;

        rgb_at(img, img->width * w.fx / 4, img->height * w.fy / 4, &r, &g, &b);

        TEST_ASSERT_UINT_WITHIN(40, w.r, r);
        TEST_ASSERT_UINT_WITHIN(40, w.g, g);
        TEST_ASSERT_UINT_WITHIN(40, w.b, b);
    }
}

static void test_fit_uses_the_descale_first(void)
{
    uint8_t scale;
    uint16_t step, w, h;

    image_decode_fit(640, 480, 160, 120, &scale, &step, &w, &h);
    TEST_ASSERT_EQUAL(2, scale);
    TEST_ASSERT_EQUAL(1, step);
    TEST_ASSERT_EQUAL(160, w);
    TEST_ASSERT_EQUAL(120, h);

    /* 12 is 4 * 3, not 8 * 2: the full width of the box rather than 120. */
    image_decode_fit(1920, 1080, 160, 120, &scale, &step, &w, &h);
    TEST_ASSERT_EQUAL(2, scale);
    TEST_ASSERT_EQUAL(3, step);
    TEST_ASSERT_EQUAL(160, w);
    TEST_ASSERT_EQUAL(90, h);

    /* The landscape body: 6 is 2 * 3, where 4 * 2 would have been 80x60. */
    image_decode_fit(640, 480, 160, 92, &scale, &step, &w, &h);
    TEST_ASSERT_EQUAL(1, scale);
    TEST_ASSERT_EQUAL(3, step);
    TEST_ASSERT_EQUAL(107, w);
    TEST_ASSERT_EQUAL(80, h);

    /* A picture already inside the box is left as it is. */
    image_decode_fit(100, 50, 160, 120, &scale, &step, &w, &h);
    TEST_ASSERT_EQUAL(0, scale);
    TEST_ASSERT_EQUAL(1, step);
    TEST_ASSERT_EQUAL(100, w);
    TEST_ASSERT_EQUAL(50, h);
}

/* Whatever the size, the result is inside the box. */
static void test_fit_never_exceeds_the_box(void)
{
    for (unsigned w = 1; w < 4000; w += 37)
    {
        for (unsigned h = 1; h < 3000; h += 53)
        {
            uint8_t scale;
            uint16_t step, ow, oh;

            image_decode_fit(w, h, 160, 120, &scale, &step, &ow, &oh);

            TEST_ASSERT_LESS_OR_EQUAL(160, ow);
            TEST_ASSERT_LESS_OR_EQUAL(120, oh);
            TEST_ASSERT_GREATER_OR_EQUAL(1, ow);
            TEST_ASSERT_GREATER_OR_EQUAL(1, oh);
        }
    }
}

static void decode_and_check(const char *name, size_t chunk, uint16_t want_w, uint16_t want_h)
{
    struct mem_stream_s m = load(name, chunk);
    struct image_decode_s img;

    TEST_ASSERT_EQUAL(IMAGE_DECODE_OK, image_decode_jpeg(mem_read, &m, 160, 120, &img));
    TEST_ASSERT_NOT_NULL(img.pixels);
    TEST_ASSERT_EQUAL(want_w, img.width);
    TEST_ASSERT_EQUAL(want_h, img.height);

    assert_quadrants(&img);
    free(img.pixels);
}

static void test_vga_snapshot(void)
{
    decode_and_check("q640x480.jpg", 1460, 160, 120);
    decode_and_check("q640x480.jpg", 1, 160, 120);
}

static void test_full_hd_snapshot(void)
{
    decode_and_check("q1920x1080.jpg", 1460, 160, 90);
}

/* Steps that do not divide an MCU, which is where seams or shifted quadrants
 * would show: 800x600 is a factor of 5 with no descale at all, and 640x480
 * into the landscape body a descale of 2 and then every third pixel. */
static void test_odd_step(void)
{
    decode_and_check("q800x600.jpg", 777, 160, 120);

    struct mem_stream_s m = load("q640x480.jpg", 300);
    struct image_decode_s img;

    TEST_ASSERT_EQUAL(IMAGE_DECODE_OK, image_decode_jpeg(mem_read, &m, 160, 92, &img));
    TEST_ASSERT_EQUAL(107, img.width);
    TEST_ASSERT_EQUAL(80, img.height);
    assert_quadrants(&img);
    free(img.pixels);
}

static void test_progressive_is_refused(void)
{
    struct mem_stream_s m = load("q640x480p.jpg");
    struct image_decode_s img;

    TEST_ASSERT_EQUAL(IMAGE_DECODE_UNSUPPORTED, image_decode_jpeg(mem_read, &m, 160, 120, &img));
    TEST_ASSERT_NULL(img.pixels);
}

static void test_truncated_and_garbage_fail(void)
{
    struct image_decode_s img;
    struct mem_stream_s m = load("q640x480.jpg");

    m.end = m.data.size() / 2;
    TEST_ASSERT_EQUAL(IMAGE_DECODE_BROKEN, image_decode_jpeg(mem_read, &m, 160, 120, &img));
    TEST_ASSERT_NULL(img.pixels);

    /* What openHAB answers for an Image item that has no picture yet, if the
     * caller did not catch it first. */
    struct mem_stream_s text = {};
    const char *undef = "UNDEF";

    text.data.assign(undef, undef + 5);
    text.end = text.data.size();
    text.chunk = 64;

    TEST_ASSERT_EQUAL(IMAGE_DECODE_BROKEN, image_decode_jpeg(mem_read, &text, 160, 120, &img));
    TEST_ASSERT_NULL(img.pixels);

    struct mem_stream_s empty = {};

    empty.chunk = 64;
    TEST_ASSERT_EQUAL(IMAGE_DECODE_BROKEN, image_decode_jpeg(mem_read, &empty, 160, 120, &img));
}

void test_image_decode_run(void)
{
    RUN_TEST(test_fit_uses_the_descale_first);
    RUN_TEST(test_fit_never_exceeds_the_box);
    RUN_TEST(test_vga_snapshot);
    RUN_TEST(test_full_hd_snapshot);
    RUN_TEST(test_odd_step);
    RUN_TEST(test_progressive_is_refused);
    RUN_TEST(test_truncated_and_garbage_fail);
}
