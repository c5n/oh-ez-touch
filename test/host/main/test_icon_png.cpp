/* Unit tests for icon_png_decode().
 *
 * The reference is lodepng, which decoded every server icon before: each
 * fixture in ../fixtures (made by make_icon_pngs.py there) is decoded by both,
 * and the result has to match pixel for pixel. lodepng's RGBA is brought down
 * to the set's size here by the same block average icon_png.h describes, so a
 * difference is the decoder's and not the downscale's.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

#include <unity.h>

#include "icons/icon_png.h"
#include "lodepng/lodepng.h"
#include "test_suites.hpp"

#define MIN_SIZE 32

static std::vector<uint8_t> load(const char *name)
{
    char path[512];

    snprintf(path, sizeof(path), "%s/%s", TEST_FIXTURE_DIR, name);

    std::vector<uint8_t> data;
    FILE *f = fopen(path, "rb");

    TEST_ASSERT_NOT_NULL_MESSAGE(f, path);

    uint8_t buf[4096];
    size_t n;

    while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
        data.insert(data.end(), buf, buf + n);

    fclose(f);

    return data;
}

/* lodepng, then the block average, then LVGL's byte order. */
static std::vector<uint8_t> reference(const std::vector<uint8_t> &png, unsigned *out_w,
                                      unsigned *out_h)
{
    unsigned char *rgba;
    unsigned w, h;

    TEST_ASSERT_EQUAL_UINT(0, lodepng_decode32(&rgba, &w, &h, png.data(), png.size()));

    unsigned factor = 1;

    while ((w / factor) % 2 == 0 && (h / factor) % 2 == 0 && w / factor / 2 >= MIN_SIZE
           && h / factor / 2 >= MIN_SIZE)
        factor *= 2;

    *out_w = w / factor;
    *out_h = h / factor;

    std::vector<uint8_t> out((size_t)*out_w * *out_h * 4);

    for (unsigned y = 0; y < *out_h; y++)
    {
        for (unsigned x = 0; x < *out_w; x++)
        {
            uint32_t r = 0, g = 0, b = 0, a = 0;

            for (unsigned dy = 0; dy < factor; dy++)
            {
                for (unsigned dx = 0; dx < factor; dx++)
                {
                    const uint8_t *p = rgba + ((size_t)(y * factor + dy) * w + x * factor + dx) * 4;

                    a += p[3];
                    r += (uint32_t)p[0] * p[3];
                    g += (uint32_t)p[1] * p[3];
                    b += (uint32_t)p[2] * p[3];
                }
            }

            uint8_t *d = &out[((size_t)y * *out_w + x) * 4];
            const uint8_t *p = rgba + ((size_t)y * w + x) * 4;

            if (factor == 1)
            {
                d[0] = p[2];
                d[1] = p[1];
                d[2] = p[0];
                d[3] = p[3];
            }
            else if (a == 0)
            {
                d[0] = d[1] = d[2] = d[3] = 0;
            }
            else
            {
                d[0] = (uint8_t)(b / a);
                d[1] = (uint8_t)(g / a);
                d[2] = (uint8_t)(r / a);
                d[3] = (uint8_t)(a / (factor * factor));
            }
        }
    }

    free(rgba);

    return out;
}

static void check_matches_lodepng(const char *name, unsigned expect_w, unsigned expect_h,
                                  size_t scratch_len = 0)
{
    std::vector<uint8_t> png = load(name);
    unsigned ref_w, ref_h;
    std::vector<uint8_t> ref = reference(png, &ref_w, &ref_h);

    uint8_t *pixels = NULL;
    unsigned w = 0, h = 0;

    /* Odd-sized and not aligned, as the tail of the receive buffer is. */
    std::vector<uint8_t> scratch(scratch_len + 1, 0xa5);
    void *area = (scratch_len > 0) ? scratch.data() + 1 : NULL;

    TEST_ASSERT_EQUAL_MESSAGE(ICON_PNG_OK,
                              icon_png_decode(png.data(), png.size(), MIN_SIZE, area,
                                              scratch_len, &pixels, &w, &h),
                              name);
    TEST_ASSERT_EQUAL_UINT_MESSAGE(expect_w, w, name);
    TEST_ASSERT_EQUAL_UINT_MESSAGE(expect_h, h, name);
    TEST_ASSERT_EQUAL_UINT(ref_w, w);
    TEST_ASSERT_EQUAL_UINT(ref_h, h);
    TEST_ASSERT_EQUAL_MEMORY_MESSAGE(ref.data(), pixels, ref.size(), name);

    free(pixels);
}

/* The format the custom icons on a server come in, halved to 32: a dynamic
 * Huffman code, every row filter, the data split over many IDAT chunks. */
static void test_rgba_dynamic(void)
{
    check_matches_lodepng("icon_rgba64_dynamic.png", 32, 32);
}

/* The same with the receive buffer's tail lent: the first five of nine
 * segments are in it, the rest on the heap. Then one that holds them all. */
static void test_rgba_with_scratch(void)
{
    check_matches_lodepng("icon_rgba64_dynamic.png", 32, 32, 10399);
    check_matches_lodepng("icon_rgba64_dynamic.png", 32, 32, 20000);
    check_matches_lodepng("icon_ga128.png", 32, 32, 2047);
}

static void test_rgba_fixed_code(void)
{
    check_matches_lodepng("icon_rgba64_fixed.png", 32, 32);
}

/* Not halved; stored blocks. */
static void test_rgb_stored(void)
{
    check_matches_lodepng("icon_rgb32_stored.png", 32, 32);
}

/* 40 is even, but 20 is below the set's size. */
static void test_palette_with_transparency(void)
{
    check_matches_lodepng("icon_pal40_trns.png", 40, 40);
}

static void test_grey_with_colour_key(void)
{
    check_matches_lodepng("icon_grey32_trns.png", 32, 32);
}

/* Two halvings in one step. */
static void test_grey_alpha_quartered(void)
{
    check_matches_lodepng("icon_ga128.png", 32, 32);
}

/* Valid, and left to lodepng. */
static void test_sixteen_bit_unsupported(void)
{
    std::vector<uint8_t> png = load("icon_rgba32_16bit.png");
    uint8_t *pixels = NULL;
    unsigned w, h;

    TEST_ASSERT_EQUAL(ICON_PNG_UNSUPPORTED,
                      icon_png_decode(png.data(), png.size(), MIN_SIZE, NULL, 0, &pixels, &w, &h));
    TEST_ASSERT_NULL(pixels);
}

static void test_interlaced_unsupported(void)
{
    std::vector<uint8_t> png = load("icon_rgb32_stored.png");
    uint8_t *pixels = NULL;
    unsigned w, h;

    /* The interlace byte of IHDR; the CRC is not checked. */
    png[28] = 1;

    TEST_ASSERT_EQUAL(ICON_PNG_UNSUPPORTED,
                      icon_png_decode(png.data(), png.size(), MIN_SIZE, NULL, 0, &pixels, &w, &h));
    TEST_ASSERT_NULL(pixels);
}

/* Cut anywhere, damaged anywhere in the image data, or not a PNG at all: an
 * error and nothing allocated that is not freed, never a crash. Leaks are for
 * a sanitizer build to find; this checks that every cut is refused. */
static void test_broken(void)
{
    std::vector<uint8_t> png = load("icon_rgba64_dynamic.png");
    uint8_t *pixels = NULL;
    unsigned w, h;

    /* Short of the Adler-32, which is not read, and the chunk ends. */
    for (size_t cut = 0; cut < png.size() - 24; cut += 97)
    {
        TEST_ASSERT_EQUAL(ICON_PNG_BROKEN,
                          icon_png_decode(png.data(), cut, MIN_SIZE, NULL, 0, &pixels, &w, &h));
    }

    TEST_ASSERT_NULL(pixels);

    const uint8_t text[] = "<html>404</html> and some more bytes to pass the size check";

    TEST_ASSERT_EQUAL(ICON_PNG_BROKEN,
                      icon_png_decode(text, sizeof(text), MIN_SIZE, NULL, 0, &pixels, &w, &h));

    /* Flipped bits in the compressed data: some still decode to a wrong
     * picture, which the unchecked Adler-32 lets through. What may not happen
     * is anything but a result code. */
    for (size_t at = 100; at < png.size() - 20; at += 211)
    {
        std::vector<uint8_t> damaged = png;

        damaged[at] ^= 0x5a;

        enum icon_png_e result = icon_png_decode(damaged.data(), damaged.size(), MIN_SIZE,
                                                 NULL, 0, &pixels, &w, &h);

        if (result == ICON_PNG_OK)
        {
            free(pixels);
            pixels = NULL;
        }
        else
        {
            TEST_ASSERT_EQUAL(ICON_PNG_BROKEN, result);
        }
    }
}

void test_icon_png_run(void)
{
    RUN_TEST(test_rgba_dynamic);
    RUN_TEST(test_rgba_with_scratch);
    RUN_TEST(test_rgba_fixed_code);
    RUN_TEST(test_rgb_stored);
    RUN_TEST(test_palette_with_transparency);
    RUN_TEST(test_grey_with_colour_key);
    RUN_TEST(test_grey_alpha_quartered);
    RUN_TEST(test_sixteen_bit_unsupported);
    RUN_TEST(test_interlaced_unsupported);
    RUN_TEST(test_broken);
}
