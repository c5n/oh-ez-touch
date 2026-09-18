/**
 * @file ui_frame_stats.c
 *
 * The averaging behind ui_frame_stats.h. Sums in, means out, once per window.
 *
 * Two properties are worth stating because they are the ones a test can pin
 * down. Time arithmetic is wrap-safe -- every comparison is on an unsigned
 * difference, never on two absolute stamps, because the caller's clock is the
 * 32-bit one LVGL uses and it rolls over every 49 days. And a window that
 * expires empty is dropped rather than published, so a panel nobody is looking
 * at keeps showing the last frame rate it actually had.
 */
#include "ui_frame_stats.h"

#include <string.h>

/* 64-bit sums against 32-bit terms: a full-screen frame is 76800 pixels, and
 * 64 of them overflow a uint32_t sum before the window is half over. The
 * durations cannot overflow at this scale, and are kept the same width for
 * want of a reason to mix. */
static struct
{
    uint32_t start_ms;
    uint32_t frames;
    uint64_t frame_us;
    uint32_t frame_us_max;
    uint64_t render_us;
    uint64_t wait_us;
    uint64_t pixels;
    uint32_t flushes;
} window;

static ui_frame_stats_t last;
static uint32_t         last_ms;

/* Rounded rather than truncated, and without a divide by zero: the means below
 * are read by a human off a status page, where 3 reported as 2 is worse than
 * the extra add costs. */
static uint32_t mean(uint64_t sum, uint32_t count)
{
    if (count == 0)
        return 0;

    return (uint32_t)((sum + count / 2) / count);
}

static void window_start(uint32_t now_ms)
{
    memset(&window, 0, sizeof(window));
    window.start_ms = now_ms;
}

/* Publishes the window as `last` if it holds anything, and starts a new one
 * either way. The elapsed time is floored at 1 ms so that a burst of frames
 * inside a single millisecond divides by something. */
static void window_close(uint32_t now_ms)
{
    if (window.frames > 0)
    {
        uint32_t elapsed = now_ms - window.start_ms;

        if (elapsed == 0)
            elapsed = 1;

        last.valid        = true;
        last.age_ms       = 0;
        last.window_ms    = elapsed;
        last.frames       = window.frames;
        last.fps_x10      = (uint32_t)(((uint64_t)window.frames * 10000u) / elapsed);
        last.frame_us     = mean(window.frame_us, window.frames);
        last.frame_us_max = window.frame_us_max;
        last.render_us    = mean(window.render_us, window.frames);
        last.wait_us      = mean(window.wait_us, window.frames);
        last.pixels       = mean(window.pixels, window.frames);
        last.flushes_x10  = mean((uint64_t)window.flushes * 10u, window.frames);

        last_ms = now_ms;
    }

    window_start(now_ms);
}

void ui_frame_stats_reset(uint32_t now_ms)
{
    memset(&last, 0, sizeof(last));
    last_ms = now_ms;
    window_start(now_ms);
}

void ui_frame_stats_tick(uint32_t now_ms)
{
    if ((uint32_t)(now_ms - window.start_ms) >= UI_FRAME_STATS_WINDOW_MS)
        window_close(now_ms);
}

void ui_frame_stats_frame(uint32_t now_ms, uint32_t frame_us, uint32_t render_us,
                          uint32_t wait_us, uint32_t pixels, uint32_t flushes)
{
    window.frames++;
    window.frame_us += frame_us;
    window.render_us += render_us;
    window.wait_us += wait_us;
    window.pixels += pixels;
    window.flushes += flushes;

    if (frame_us > window.frame_us_max)
        window.frame_us_max = frame_us;

    /* After the frame, not before it: a window sized 64 should contain 64. */
    if (window.frames >= UI_FRAME_STATS_WINDOW_FRAMES ||
        (uint32_t)(now_ms - window.start_ms) >= UI_FRAME_STATS_WINDOW_MS)
        window_close(now_ms);
}

void ui_frame_stats_get(uint32_t now_ms, ui_frame_stats_t *out)
{
    *out = last;

    if (out->valid)
        out->age_ms = now_ms - last_ms;
}
