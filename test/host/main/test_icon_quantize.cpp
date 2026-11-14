/* Unit tests for icon_quantize_i4().
 *
 * The checks read the I4 image back with a decoder written here from the
 * format in icon_quantize.h, so a fault in the packing shows up as a pixel
 * that comes back wrong.
 */

#include <stdlib.h>
#include <string.h>
#include <vector>

#include <unity.h>

#include "icons/icon_png.h"
#include "icons/icon_quantize.h"
#include "test_suites.hpp"

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

/* One pixel of the I4 image as B, G, R, A. */
static void i4_pixel(const uint8_t *img, unsigned width, unsigned x, unsigned y, uint8_t px[4])
{
    uint8_t packed = img[64 + y * (width / 2) + x / 2];
    unsigned index = (x % 2 == 0) ? (packed >> 4) : (packed & 0x0F);

    memcpy(px, img + index * 4, 4);
}

static std::vector<uint8_t> quantize(const std::vector<uint8_t> &argb, unsigned w, unsigned h)
{
    std::vector<uint16_t> work(ICON_QUANTIZE_WORK_SIZE(w, h) / sizeof(uint16_t));
    std::vector<uint8_t> out(ICON_QUANTIZE_I4_SIZE(w, h), 0xEE);

    TEST_ASSERT_TRUE(icon_quantize_i4(argb.data(), w, h, work.data(), out.data()));
    return out;
}

/* Twelve colours and transparency: every pixel comes back exactly. */
static void test_few_colours_exact(void)
{
    const unsigned w = 32, h = 32;
    std::vector<uint8_t> argb(w * h * 4);

    for (unsigned p = 0; p < w * h; p++)
    {
        unsigned c = p % 13;

        if (c == 12)
        {
            /* Transparent, with a colour that must not leak anywhere. */
            argb[p * 4 + 0] = 0xFF;
            argb[p * 4 + 1] = 0x00;
            argb[p * 4 + 2] = 0xFF;
            argb[p * 4 + 3] = 0;
        }
        else
        {
            argb[p * 4 + 0] = (uint8_t)(c * 20);
            argb[p * 4 + 1] = (uint8_t)(255 - c * 20);
            argb[p * 4 + 2] = (uint8_t)(c * 7);
            argb[p * 4 + 3] = (uint8_t)(c < 6 ? 255 : 40 + c * 10);
        }
    }

    std::vector<uint8_t> img = quantize(argb, w, h);

    /* Index 0 is the transparent entry. */
    TEST_ASSERT_EQUAL_UINT8(0, img[3]);

    for (unsigned p = 0; p < w * h; p++)
    {
        uint8_t px[4];

        i4_pixel(img.data(), w, p % w, p / w, px);

        if (argb[p * 4 + 3] == 0)
            TEST_ASSERT_EQUAL_UINT8(0, px[3]);
        else
            TEST_ASSERT_EQUAL_UINT8_ARRAY(&argb[p * 4], px, 4);
    }
}

/* A picture with no transparent pixel spends all 16 entries on colours. */
static void test_opaque_uses_sixteen(void)
{
    const unsigned w = 16, h = 16;
    std::vector<uint8_t> argb(w * h * 4);

    for (unsigned p = 0; p < w * h; p++)
    {
        argb[p * 4 + 0] = (uint8_t)((p % 16) * 16);
        argb[p * 4 + 1] = 0;
        argb[p * 4 + 2] = 0;
        argb[p * 4 + 3] = 255;
    }

    std::vector<uint8_t> img = quantize(argb, w, h);

    for (unsigned p = 0; p < w * h; p++)
    {
        uint8_t px[4];

        i4_pixel(img.data(), w, p % w, p / w, px);
        TEST_ASSERT_EQUAL_UINT8_ARRAY(&argb[p * 4], px, 4);
    }
}

/* A smooth gradient with 1024 colours: at most 16 entries, a bounded error,
 * and no visible pixel turned transparent. */
static void test_gradient_bounded(void)
{
    const unsigned w = 32, h = 32;
    std::vector<uint8_t> argb(w * h * 4);

    for (unsigned y = 0; y < h; y++)
    {
        for (unsigned x = 0; x < w; x++)
        {
            uint8_t *px = &argb[(y * w + x) * 4];

            px[0] = (uint8_t)(x * 8);
            px[1] = (uint8_t)(y * 8);
            px[2] = (uint8_t)((x + y) * 4);
            px[3] = (uint8_t)(x < 2 ? 0 : 8 + y * 7);
        }
    }

    std::vector<uint8_t> img = quantize(argb, w, h);
    unsigned worst = 0;

    for (unsigned p = 0; p < w * h; p++)
    {
        uint8_t px[4];

        i4_pixel(img.data(), w, p % w, p / w, px);

        if (argb[p * 4 + 3] == 0)
        {
            TEST_ASSERT_EQUAL_UINT8(0, px[3]);
            continue;
        }

        TEST_ASSERT_NOT_EQUAL(0, px[3]);

        for (unsigned c = 0; c < 4; c++)
        {
            unsigned d = (unsigned)abs((int)px[c] - (int)argb[p * 4 + c]);

            if (d > worst)
                worst = d;
        }
    }

    /* 15 entries over a 4-channel ramp: the worst channel error stays well
     * under a quarter of the range. */
    TEST_ASSERT_LESS_THAN_UINT(64, worst);
}

static void test_fully_transparent(void)
{
    const unsigned w = 8, h = 8;
    std::vector<uint8_t> argb(w * h * 4, 0);

    std::vector<uint8_t> img = quantize(argb, w, h);

    for (size_t i = 0; i < img.size(); i++)
        TEST_ASSERT_EQUAL_UINT8(0, img[i]);
}

static void test_refuses_odd_width(void)
{
    std::vector<uint8_t> argb(3 * 2 * 4, 255);
    std::vector<uint16_t> work(6);
    uint8_t out[ICON_QUANTIZE_I4_SIZE(4, 2)];

    memset(out, 0xEE, sizeof(out));
    TEST_ASSERT_FALSE(icon_quantize_i4(argb.data(), 3, 2, work.data(), out));
    TEST_ASSERT_EQUAL_UINT8(0xEE, out[0]);
}

/* A real server icon, through the decoder the panel uses: the I4 image
 * stays close to the ARGB8888 one. */
static void test_server_icon(void)
{
    std::vector<uint8_t> png = load("icon_rgba64_dynamic.png");
    uint8_t *pixels = NULL;
    unsigned w = 0, h = 0;

    TEST_ASSERT_EQUAL(ICON_PNG_OK, icon_png_decode(png.data(), png.size(), 32, NULL, 0,
                                                   &pixels, &w, &h));

    std::vector<uint8_t> argb(pixels, pixels + w * h * 4);

    free(pixels);

    std::vector<uint8_t> img = quantize(argb, w, h);
    unsigned long total = 0;

    for (unsigned p = 0; p < w * h; p++)
    {
        uint8_t px[4];

        i4_pixel(img.data(), w, p % w, p / w, px);

        if (argb[p * 4 + 3] == 0)
        {
            TEST_ASSERT_EQUAL_UINT8(0, px[3]);
            continue;
        }

        TEST_ASSERT_NOT_EQUAL(0, px[3]);

        for (unsigned c = 0; c < 4; c++)
            total += (unsigned long)abs((int)px[c] - (int)argb[p * 4 + c]);
    }

    /* The mean error per channel and pixel. */
    TEST_ASSERT_LESS_THAN_UINT(8, (unsigned)(total / (w * h * 4)));
}

void test_icon_quantize_run(void)
{
    RUN_TEST(test_few_colours_exact);
    RUN_TEST(test_opaque_uses_sixteen);
    RUN_TEST(test_gradient_bounded);
    RUN_TEST(test_fully_transparent);
    RUN_TEST(test_refuses_odd_width);
    RUN_TEST(test_server_icon);
}
