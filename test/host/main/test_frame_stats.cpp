/* Unit tests for the frame-time accumulator in main/ui/ui_frame_stats.c.
 *
 * The accumulator is here rather than mocked for the reason the beeper tables
 * are: it includes nothing but libc, because the half that knows about LVGL --
 * ui_frame_probe.c, where the event callbacks live -- was split away from it
 * precisely so this half could be reached without a display.
 *
 * What is worth pinning down is not the averaging, which is a division. It is
 * the two decisions around it that a reader of the status page depends on and
 * that nothing else in the firmware writes down: a window that expires with no
 * frames in it is *discarded* rather than published, so a panel nobody is
 * touching keeps showing the last frame rate it really had; and every
 * comparison is on an unsigned difference, so the 32-bit millisecond clock
 * rolling over mid-window does not report a window 49 days long.
 */

#include <unity.h>

#include "test_suites.hpp"
#include "ui/ui_frame_stats.h"

/* One ordinary frame, so a test that is about the window rather than about the
 * numbers does not have to invent six of them each time. */
static void feed(uint32_t now_ms, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++)
        ui_frame_stats_frame(now_ms, 1000, 800, 200, 5000, 2);
}

static void test_nothing_is_reported_before_a_window_closes(void)
{
    ui_frame_stats_t s;

    ui_frame_stats_reset(0);
    feed(10, 5);

    ui_frame_stats_get(20, &s);

    TEST_ASSERT_FALSE(s.valid);
    TEST_ASSERT_EQUAL_UINT32(0, s.frames);
    TEST_ASSERT_EQUAL_UINT32(0, s.fps_x10);
}

static void test_a_full_window_closes_on_the_frame_count(void)
{
    ui_frame_stats_t s;

    ui_frame_stats_reset(0);

    /* Spread over two seconds, so the rate is a round number: 64 frames in
     * 2000 ms is 32.0 fps. */
    for (uint32_t i = 0; i < UI_FRAME_STATS_WINDOW_FRAMES; i++)
        ui_frame_stats_frame(1 + i * 2000 / UI_FRAME_STATS_WINDOW_FRAMES,
                             1000, 800, 200, 5000, 2);

    ui_frame_stats_get(2000, &s);

    TEST_ASSERT_TRUE(s.valid);
    TEST_ASSERT_EQUAL_UINT32(UI_FRAME_STATS_WINDOW_FRAMES, s.frames);
    TEST_ASSERT_EQUAL_UINT32(1000, s.frame_us);
    TEST_ASSERT_EQUAL_UINT32(800, s.render_us);
    TEST_ASSERT_EQUAL_UINT32(200, s.wait_us);
    TEST_ASSERT_EQUAL_UINT32(5000, s.pixels);
    TEST_ASSERT_EQUAL_UINT32(20, s.flushes_x10);
}

static void test_a_window_closes_on_time_with_frames_in_it(void)
{
    ui_frame_stats_t s;

    ui_frame_stats_reset(0);
    feed(100, 4);

    /* The tick is what an idle refresh cycle does, and this one is the first
     * past the cap. */
    ui_frame_stats_tick(UI_FRAME_STATS_WINDOW_MS);

    ui_frame_stats_get(UI_FRAME_STATS_WINDOW_MS, &s);

    TEST_ASSERT_TRUE(s.valid);
    TEST_ASSERT_EQUAL_UINT32(4, s.frames);
    TEST_ASSERT_EQUAL_UINT32(UI_FRAME_STATS_WINDOW_MS, s.window_ms);

    /* 4 frames over 10 s, in tenths. */
    TEST_ASSERT_EQUAL_UINT32(4, s.fps_x10);
}

static void test_an_empty_window_does_not_overwrite_the_last_one(void)
{
    ui_frame_stats_t s;

    ui_frame_stats_reset(0);
    feed(100, 4);
    ui_frame_stats_tick(UI_FRAME_STATS_WINDOW_MS);

    /* Three windows' worth of a still screen. Nothing is drawn, so nothing is
     * published, and the measurement above survives -- which is the whole
     * point: 0.0 fps reads like a frame rate, and a panel at rest does not
     * have one. */
    ui_frame_stats_tick(UI_FRAME_STATS_WINDOW_MS * 2);
    ui_frame_stats_tick(UI_FRAME_STATS_WINDOW_MS * 3);
    ui_frame_stats_tick(UI_FRAME_STATS_WINDOW_MS * 4);

    ui_frame_stats_get(UI_FRAME_STATS_WINDOW_MS * 4, &s);

    TEST_ASSERT_TRUE(s.valid);
    TEST_ASSERT_EQUAL_UINT32(4, s.frames);

    /* And says how stale it is, which is the only reason age_ms exists. */
    TEST_ASSERT_EQUAL_UINT32(UI_FRAME_STATS_WINDOW_MS * 3, s.age_ms);
}

static void test_the_worst_frame_is_kept_not_averaged(void)
{
    ui_frame_stats_t s;

    ui_frame_stats_reset(0);

    ui_frame_stats_frame(10, 1000, 500, 500, 100, 1);
    ui_frame_stats_frame(20, 31000, 4000, 27000, 76800, 10);
    ui_frame_stats_frame(30, 1000, 500, 500, 100, 1);

    ui_frame_stats_tick(UI_FRAME_STATS_WINDOW_MS);
    ui_frame_stats_get(UI_FRAME_STATS_WINDOW_MS, &s);

    TEST_ASSERT_EQUAL_UINT32(31000, s.frame_us_max);
    TEST_ASSERT_EQUAL_UINT32(11000, s.frame_us);
}

static void test_the_means_are_rounded_rather_than_truncated(void)
{
    ui_frame_stats_t s;

    ui_frame_stats_reset(0);

    /* Mean 2.5 us, which a truncating divide would report as 2. */
    ui_frame_stats_frame(10, 2, 2, 2, 2, 1);
    ui_frame_stats_frame(20, 3, 3, 3, 3, 2);

    ui_frame_stats_tick(UI_FRAME_STATS_WINDOW_MS);
    ui_frame_stats_get(UI_FRAME_STATS_WINDOW_MS, &s);

    TEST_ASSERT_EQUAL_UINT32(3, s.frame_us);
    TEST_ASSERT_EQUAL_UINT32(15, s.flushes_x10);
}

static void test_a_window_that_spans_the_clock_rollover(void)
{
    ui_frame_stats_t s;

    /* Starting 100 ms before the 32-bit millisecond clock wraps. Every
     * comparison in the accumulator is on now - start, so the window is 10 s
     * long here as everywhere else; on absolute stamps it would be either
     * 49 days or immediate, depending on which way the comparison went. */
    const uint32_t start = 0xFFFFFF9Bu;

    ui_frame_stats_reset(start);

    feed(start + 50, 2);
    feed(start + 150, 2);   /* past the wrap */

    ui_frame_stats_tick(start + UI_FRAME_STATS_WINDOW_MS - 1);
    ui_frame_stats_get(start + UI_FRAME_STATS_WINDOW_MS - 1, &s);
    TEST_ASSERT_FALSE(s.valid);

    ui_frame_stats_tick(start + UI_FRAME_STATS_WINDOW_MS);
    ui_frame_stats_get(start + UI_FRAME_STATS_WINDOW_MS, &s);

    TEST_ASSERT_TRUE(s.valid);
    TEST_ASSERT_EQUAL_UINT32(4, s.frames);
    TEST_ASSERT_EQUAL_UINT32(UI_FRAME_STATS_WINDOW_MS, s.window_ms);
    TEST_ASSERT_EQUAL_UINT32(0, s.age_ms);
}

static void test_a_burst_inside_one_millisecond_does_not_divide_by_zero(void)
{
    ui_frame_stats_t s;

    ui_frame_stats_reset(0);
    feed(0, UI_FRAME_STATS_WINDOW_FRAMES);

    ui_frame_stats_get(0, &s);

    TEST_ASSERT_TRUE(s.valid);
    TEST_ASSERT_EQUAL_UINT32(1, s.window_ms);
    TEST_ASSERT_EQUAL_UINT32(UI_FRAME_STATS_WINDOW_FRAMES * 10000u, s.fps_x10);
}

static void test_reset_forgets_the_last_window(void)
{
    ui_frame_stats_t s;

    ui_frame_stats_reset(0);
    feed(100, 4);
    ui_frame_stats_tick(UI_FRAME_STATS_WINDOW_MS);

    ui_frame_stats_reset(0);
    ui_frame_stats_get(500, &s);

    TEST_ASSERT_FALSE(s.valid);
    TEST_ASSERT_EQUAL_UINT32(0, s.frames);
    TEST_ASSERT_EQUAL_UINT32(0, s.age_ms);
}

void test_frame_stats_run(void)
{
    RUN_TEST(test_nothing_is_reported_before_a_window_closes);
    RUN_TEST(test_a_full_window_closes_on_the_frame_count);
    RUN_TEST(test_a_window_closes_on_time_with_frames_in_it);
    RUN_TEST(test_an_empty_window_does_not_overwrite_the_last_one);
    RUN_TEST(test_the_worst_frame_is_kept_not_averaged);
    RUN_TEST(test_the_means_are_rounded_rather_than_truncated);
    RUN_TEST(test_a_window_that_spans_the_clock_rollover);
    RUN_TEST(test_a_burst_inside_one_millisecond_does_not_divide_by_zero);
    RUN_TEST(test_reset_forgets_the_last_window);
}
