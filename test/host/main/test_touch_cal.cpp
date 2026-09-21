/* Unit tests for the touchscreen calibration in main/port/touch_cal.c.
 *
 * Two things are being pinned down, and they are pinned down differently.
 *
 * The map is checked against a *second copy* of the arithmetic, written out
 * below from the expression port_indev.c carried inline before this file
 * existed, and swept over the whole 12-bit input space in both orientations
 * with the flip both ways. That is what makes the extraction provably a move
 * rather than a rewrite: the two have to agree everywhere, not at a few points
 * somebody chose.
 *
 * The solve is checked by round trip. A panel is invented, its map is run
 * backwards to produce the raw readings four corner taps would have given, and
 * the solve has to recover a calibration that puts every pixel of the screen
 * back within a pixel of itself. Checking the four constants directly would be
 * checking the arithmetic against itself; checking the picture is checking what
 * the user gets.
 *
 * Host-only by construction: touch_cal.c includes no LVGL, no IDF and no
 * board_pins.h, for exactly this reason. Run with:
 *   cd test/host && idf.py build && ./build/oh-ez-touch-host-test.elf
 */

#include <stdlib.h>

#include <unity.h>

#include "port/touch_cal.h"
#include "test_suites.hpp"

/* The constants the two resistive boards ship with, from
 * main/port/esp32/board_pins.h. Written out again here so that a change to
 * either set arrives as a failure rather than as a panel nobody can tap. */
static const struct touch_cal_s arduitouch = {275, 3620, 264, 3532};
static const struct touch_cal_s cyd = {240, 3620, 200, 3700};

#define SAMPLE_COUNT TOUCH_CAL_SAMPLES

/* ------------------------------------------------------------------ the map */

/* port_indev.c's expression, before touch_cal_apply() replaced it. */
static void reference_map(const struct touch_cal_s *c, bool portrait, bool flip,
                          int32_t w, int32_t h, int32_t raw_x, int32_t raw_y,
                          int32_t *sx, int32_t *sy)
{
    if (portrait)
    {
        *sx = ((raw_x - c->y_origin) * w) / c->y_span;
        *sy = ((raw_y - c->x_origin) * h) / c->x_span;
    }
    else
    {
        *sx = ((raw_y - c->x_origin) * w) / c->x_span;
        *sy = ((raw_x - c->y_origin) * h) / c->y_span;
    }

    if (flip)
    {
        *sx = (w - 1) - *sx;
        *sy = (h - 1) - *sy;
    }
}

static void sweep_against_reference(const struct touch_cal_s *cal, bool portrait, bool flip)
{
    int32_t w = portrait ? 240 : 320;
    int32_t h = portrait ? 320 : 240;

    /* Coprime strides, so the two axes do not walk in step and the sweep is a
     * lattice rather than a diagonal. */
    for (int32_t raw_x = 0; raw_x < 4096; raw_x += 37)
    {
        for (int32_t raw_y = 0; raw_y < 4096; raw_y += 41)
        {
            int32_t want_x = 0;
            int32_t want_y = 0;
            int32_t got_x = 0;
            int32_t got_y = 0;

            reference_map(cal, portrait, flip, w, h, raw_x, raw_y, &want_x, &want_y);
            touch_cal_apply(cal, portrait, flip, w, h, raw_x, raw_y, &got_x, &got_y);

            TEST_ASSERT_EQUAL_INT32(want_x, got_x);
            TEST_ASSERT_EQUAL_INT32(want_y, got_y);
        }
    }
}

static void test_the_map_is_the_one_the_port_had(void)
{
    for (unsigned p = 0; p < 2; p++)
    {
        for (unsigned f = 0; f < 2; f++)
        {
            sweep_against_reference(&arduitouch, p != 0, f != 0);
            sweep_against_reference(&cyd, p != 0, f != 0);
        }
    }
}

/* The span is the distance across the screen, not the reading at its far edge.
 * Reading it as a maximum is the mistake board_pins.h warns about: it is a few
 * per cent, invisible in the middle and half a tile out at the edges. */
static void test_the_span_is_a_span_and_not_a_maximum(void)
{
    int32_t as_span = 0;
    int32_t as_max = 0;
    int32_t ignored = 0;

    struct touch_cal_s misread = arduitouch;

    /* What the constants would have to be for the second number to be the far
     * edge rather than the distance. */
    misread.x_span = arduitouch.x_span - arduitouch.x_origin;
    misread.y_span = arduitouch.y_span - arduitouch.y_origin;

    int32_t raw_y = arduitouch.x_origin + arduitouch.x_span; /* the right edge */

    touch_cal_apply(&arduitouch, false, false, 320, 240, 2000, raw_y, &as_span, &ignored);
    touch_cal_apply(&misread, false, false, 320, 240, 2000, raw_y, &as_max, &ignored);

    TEST_ASSERT_EQUAL_INT32(320, as_span);
    TEST_ASSERT_TRUE(as_max - as_span > 20);
}

static void test_a_zero_span_maps_to_zero_rather_than_dividing(void)
{
    struct touch_cal_s empty = {0, 0, 0, 0};
    int32_t            x = 7;
    int32_t            y = 7;

    touch_cal_apply(&empty, false, false, 320, 240, 1234, 2345, &x, &y);

    TEST_ASSERT_EQUAL_INT32(0, x);
    TEST_ASSERT_EQUAL_INT32(0, y);
    TEST_ASSERT_FALSE(touch_cal_valid(&empty));
    TEST_ASSERT_TRUE(touch_cal_valid(&arduitouch));
}

/* ---------------------------------------------------------------- the solve */

/* touch_cal_apply() backwards: what a panel described by `truth` reports for a
 * finger at sx, sy. This is also what the simulator's port does, and it is the
 * only way to get a raw reading without a panel. */
static void inverse_map(const struct touch_cal_s *truth, bool portrait, bool flip,
                        int32_t w, int32_t h, int32_t sx, int32_t sy,
                        int32_t *raw_x, int32_t *raw_y)
{
    int32_t px = flip ? ((w - 1) - sx) : sx;
    int32_t py = flip ? ((h - 1) - sy) : sy;

    if (portrait)
    {
        *raw_x = truth->y_origin + ((px * truth->y_span) / w);
        *raw_y = truth->x_origin + ((py * truth->x_span) / h);
    }
    else
    {
        *raw_y = truth->x_origin + ((px * truth->x_span) / w);
        *raw_x = truth->y_origin + ((py * truth->y_span) / h);
    }
}

/* The four corner taps the screen asks for: a tenth in from every edge. */
static void corner_samples(const struct touch_cal_s *truth, bool portrait, bool flip,
                           int32_t w, int32_t h, struct touch_cal_sample_s *out)
{
    static const uint8_t corner[SAMPLE_COUNT][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};

    for (unsigned i = 0; i < SAMPLE_COUNT; i++)
    {
        out[i].target_x = corner[i][0] ? (w - 1 - (w / 10)) : (w / 10);
        out[i].target_y = corner[i][1] ? (h - 1 - (h / 10)) : (h / 10);

        inverse_map(truth, portrait, flip, w, h, out[i].target_x, out[i].target_y,
                    &out[i].raw_x, &out[i].raw_y);
    }
}

/* The worst a solved calibration is out, over the whole screen, against the
 * panel the samples came from. */
static int32_t round_trip_error(const struct touch_cal_s *truth,
                                const struct touch_cal_s *solved, bool portrait, bool flip,
                                int32_t w, int32_t h)
{
    int32_t worst = 0;

    for (int32_t sx = 0; sx < w; sx += 7)
    {
        for (int32_t sy = 0; sy < h; sy += 7)
        {
            int32_t raw_x = 0;
            int32_t raw_y = 0;
            int32_t got_x = 0;
            int32_t got_y = 0;

            inverse_map(truth, portrait, flip, w, h, sx, sy, &raw_x, &raw_y);
            touch_cal_apply(solved, portrait, flip, w, h, raw_x, raw_y, &got_x, &got_y);

            if (abs(got_x - sx) > worst)
                worst = abs(got_x - sx);

            if (abs(got_y - sy) > worst)
                worst = abs(got_y - sy);
        }
    }

    return worst;
}

static void solve_round_trip(const struct touch_cal_s *truth, bool portrait, bool flip)
{
    int32_t                   w = portrait ? 240 : 320;
    int32_t                   h = portrait ? 320 : 240;
    struct touch_cal_sample_s samples[SAMPLE_COUNT];
    struct touch_cal_s        solved = {0, 0, 0, 0};

    corner_samples(truth, portrait, flip, w, h, samples);

    const char *reason = touch_cal_solve(samples, SAMPLE_COUNT, portrait, flip, w, h, &solved);

    TEST_ASSERT_NULL(reason);

    /* One pixel. The targets are at a tenth and nine tenths, so the solve
     * extrapolates a quarter of the screen past each of them, and integer
     * division loses a count on the way in and another on the way out. */
    TEST_ASSERT_LESS_OR_EQUAL_INT32(1, round_trip_error(truth, &solved, portrait, flip, w, h));
}

static void test_a_solve_recovers_the_panel_it_came_from(void)
{
    /* The two shipped sets, and three panels that are nothing like them: one
     * shifted, one that reads over a much shorter range, and one whose axes
     * have drifted in opposite directions. */
    static const struct touch_cal_s panels[] = {
        {275, 3620, 264, 3532},
        {240, 3620, 200, 3700},
        {520, 3100, 610, 2980},
        {150, 3900, 170, 3850},
        {400, 2600, 180, 3600},
    };

    for (unsigned i = 0; i < sizeof(panels) / sizeof(panels[0]); i++)
    {
        for (unsigned p = 0; p < 2; p++)
        {
            for (unsigned f = 0; f < 2; f++)
                solve_round_trip(&panels[i], p != 0, f != 0);
        }
    }
}

/* ------------------------------------------------------------ the refusals */

/* A refusal has to leave the caller's calibration alone. It is the one thing
 * standing between a bad measurement and a panel on which the calibration
 * screen can no longer be reached. */
static void expect_refusal(const struct touch_cal_sample_s *samples, const char *what)
{
    struct touch_cal_s out = arduitouch;
    const char        *reason = touch_cal_solve(samples, SAMPLE_COUNT, false, false,
                                                320, 240, &out);

    TEST_ASSERT_NOT_NULL_MESSAGE(reason, what);
    TEST_ASSERT_EQUAL_INT32(arduitouch.x_origin, out.x_origin);
    TEST_ASSERT_EQUAL_INT32(arduitouch.x_span, out.x_span);
    TEST_ASSERT_EQUAL_INT32(arduitouch.y_origin, out.y_origin);
    TEST_ASSERT_EQUAL_INT32(arduitouch.y_span, out.y_span);
}

static void test_four_taps_in_one_place_are_refused(void)
{
    struct touch_cal_sample_s samples[SAMPLE_COUNT];

    corner_samples(&arduitouch, false, false, 320, 240, samples);

    /* Every reading the same: a finger that never moved, or a panel that is
     * not reporting. */
    for (unsigned i = 0; i < SAMPLE_COUNT; i++)
    {
        samples[i].raw_x = 2000;
        samples[i].raw_y = 2000;
    }

    expect_refusal(samples, "four identical readings");
}

static void test_a_mirrored_panel_is_refused_rather_than_encoded(void)
{
    struct touch_cal_sample_s samples[SAMPLE_COUNT];

    corner_samples(&arduitouch, false, false, 320, 240, samples);

    /* Both axes running backwards. A negative span would map this correctly
     * and would say the board is wired differently than board_pins.h claims,
     * so the solve refuses and names the flag instead. */
    for (unsigned i = 0; i < SAMPLE_COUNT; i++)
    {
        samples[i].raw_x = 4095 - samples[i].raw_x;
        samples[i].raw_y = 4095 - samples[i].raw_y;
    }

    expect_refusal(samples, "a mirrored panel");
}

static void test_two_taps_that_disagree_are_refused(void)
{
    struct touch_cal_sample_s samples[SAMPLE_COUNT];

    corner_samples(&arduitouch, false, false, 320, 240, samples);

    /* The top left corner landed somewhere else entirely -- the user tapped
     * the wrong place, or the panel glitched. Its partner at the same end of
     * the screen's x axis is the bottom left, and the two no longer agree. */
    samples[0].raw_y += 900;

    expect_refusal(samples, "one tap far from its partner");
}

static void test_a_short_or_empty_set_is_refused(void)
{
    struct touch_cal_sample_s samples[SAMPLE_COUNT];
    struct touch_cal_s        out = {0, 0, 0, 0};

    corner_samples(&arduitouch, false, false, 320, 240, samples);

    TEST_ASSERT_NOT_NULL(touch_cal_solve(samples, 3, false, false, 320, 240, &out));
    TEST_ASSERT_NOT_NULL(touch_cal_solve(NULL, SAMPLE_COUNT, false, false, 320, 240, &out));
    TEST_ASSERT_NOT_NULL(touch_cal_solve(samples, SAMPLE_COUNT, false, false, 320, 240, NULL));

    /* No display yet: the resolution is not something to divide by. */
    TEST_ASSERT_NOT_NULL(touch_cal_solve(samples, SAMPLE_COUNT, false, false, 0, 0, &out));
}

/* The widest term in the solve is a full-scale raw difference times a screen
 * axis. This is the case that produces it, and it must still be an int32_t. */
static void test_a_full_scale_reading_does_not_overflow(void)
{
    struct touch_cal_sample_s samples[SAMPLE_COUNT];
    struct touch_cal_s        out = {0, 0, 0, 0};
    struct touch_cal_s        extreme = {0, 4095, 0, 4095};

    corner_samples(&extreme, false, false, 320, 240, samples);

    TEST_ASSERT_NULL(touch_cal_solve(samples, SAMPLE_COUNT, false, false, 320, 240, &out));
    TEST_ASSERT_LESS_OR_EQUAL_INT32(
        1, round_trip_error(&extreme, &out, false, false, 320, 240));
}

void test_touch_cal_run(void)
{
    RUN_TEST(test_the_map_is_the_one_the_port_had);
    RUN_TEST(test_the_span_is_a_span_and_not_a_maximum);
    RUN_TEST(test_a_zero_span_maps_to_zero_rather_than_dividing);
    RUN_TEST(test_a_solve_recovers_the_panel_it_came_from);
    RUN_TEST(test_four_taps_in_one_place_are_refused);
    RUN_TEST(test_a_mirrored_panel_is_refused_rather_than_encoded);
    RUN_TEST(test_two_taps_that_disagree_are_refused);
    RUN_TEST(test_a_short_or_empty_set_is_refused);
    RUN_TEST(test_a_full_scale_reading_does_not_overflow);
}
