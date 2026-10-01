/* Unit tests for BacklightControl: when the panel dims, and how it gets
 * there and back.
 *
 * The class has always been shared by both targets -- port_backlight is the
 * only thing it drives, and the simulator's is a no-op -- but nothing ran it
 * against a clock somebody controls. The blank transition made that worth
 * having: it is eight phases on deadlines, and the clock screen and the
 * return to the home page both key on exactly when they change.
 *
 * port_millis() and port_backlight_*() are doubles here: a clock the test
 * moves by hand, and a record of the last fade asked for.
 */

#include <unity.h>

#include "control/backlight_control.hpp"
#include "port/port_backlight.h"
#include "port/port_sys.h"
#include "test_suites.hpp"

static uint64_t now_ms;
static uint8_t  fade_percent;
static uint16_t fade_ms;
static unsigned fade_count;

extern "C" uint64_t port_millis(void)
{
    return now_ms;
}

extern "C" void port_backlight_init(void)
{
}

extern "C" void port_backlight_set(uint8_t percent)
{
    port_backlight_fade(percent, 0);
}

extern "C" void port_backlight_fade(uint8_t percent, uint16_t ms)
{
    fade_percent = percent;
    fade_ms = ms;
    fade_count++;
}

/* Run the loop over `ms` of time, a millisecond a step, the way the main loop
 * would at its busiest. */
static void run_for(BacklightControl &bl, uint64_t ms)
{
    for (uint64_t i = 0; i < ms; i++)
    {
        now_ms++;
        bl.loop();
    }
}

/* A panel that dims to 40 % after 10 s, set up at t = 1000. */
static void make(BacklightControl &bl, bool blank)
{
    now_ms = 1000;
    fade_count = 0;

    bl.setDimTimeout(10);
    bl.setNormalBrightness(100);
    bl.setDimBrightness(40);
    bl.setBlankTransition(blank);
    bl.setup();
}

/* Without the blank transition nothing has changed: one fade down at the
 * timeout, dimmed at once, one fade up on the wake. */
static void test_the_plain_dim_is_unchanged(void)
{
    BacklightControl bl;

    make(bl, false);

    run_for(bl, 9999);
    TEST_ASSERT_FALSE(bl.isDimmed());
    TEST_ASSERT_FALSE(bl.isInactive());
    TEST_ASSERT_EQUAL_UINT8(100, bl.currentBrightness());

    run_for(bl, 1);
    TEST_ASSERT_TRUE(bl.isDimmed());
    TEST_ASSERT_TRUE(bl.isInactive());
    TEST_ASSERT_EQUAL_UINT8(40, fade_percent);
    TEST_ASSERT_EQUAL_UINT16(600, fade_ms);

    TEST_ASSERT_TRUE(bl.resetDimTimeout());
    TEST_ASSERT_FALSE(bl.isDimmed());
    TEST_ASSERT_FALSE(bl.isInactive());
    TEST_ASSERT_EQUAL_UINT8(100, fade_percent);
    TEST_ASSERT_EQUAL_UINT16(180, fade_ms);
    TEST_ASSERT_EQUAL(BacklightControl::PHASE_AWAKE, bl.currentPhase());
}

/* With both levels equal there is nothing to see, so the waking tap is not
 * swallowed -- and the panel still counts as dimmed in between. */
static void test_equal_levels_do_not_swallow_the_tap(void)
{
    BacklightControl bl;

    make(bl, false);
    bl.setDimBrightness(100);

    run_for(bl, 10000);
    TEST_ASSERT_TRUE(bl.isDimmed());
    TEST_ASSERT_FALSE(bl.resetDimTimeout());
    TEST_ASSERT_FALSE(bl.isDimmed());
}

/* The way to sleep through black: inactive at the timeout, dark -- and only
 * then dimmed -- after the slow fade, lit to the dim level after the hold and
 * the fade back in. */
static void test_sleep_goes_through_black(void)
{
    BacklightControl bl;

    make(bl, true);

    run_for(bl, 10000);
    TEST_ASSERT_TRUE(bl.isInactive());
    TEST_ASSERT_FALSE(bl.isDimmed());
    TEST_ASSERT_EQUAL(BacklightControl::PHASE_SLEEP_OUT, bl.currentPhase());
    TEST_ASSERT_EQUAL_UINT8(0, fade_percent);
    TEST_ASSERT_EQUAL_UINT16(1500, fade_ms);

    run_for(bl, 1499);
    TEST_ASSERT_FALSE(bl.isDimmed());

    run_for(bl, 1);
    TEST_ASSERT_TRUE(bl.isDimmed());
    TEST_ASSERT_EQUAL(BacklightControl::PHASE_SLEEP_DARK, bl.currentPhase());
    TEST_ASSERT_EQUAL_UINT8(0, bl.currentBrightness());

    run_for(bl, 60);
    TEST_ASSERT_EQUAL(BacklightControl::PHASE_SLEEP_IN, bl.currentPhase());
    TEST_ASSERT_EQUAL_UINT8(40, fade_percent);
    TEST_ASSERT_EQUAL_UINT16(800, fade_ms);

    run_for(bl, 800);
    TEST_ASSERT_EQUAL(BacklightControl::PHASE_ASLEEP, bl.currentPhase());
    TEST_ASSERT_TRUE(bl.isDimmed());
}

/* And back: the tap is swallowed, the panel dips to black, the dim state
 * clears in the dark, and the light comes up to the normal level. */
static void test_wake_goes_through_black(void)
{
    BacklightControl bl;

    make(bl, true);
    run_for(bl, 10000 + 1500 + 60 + 800);
    TEST_ASSERT_EQUAL(BacklightControl::PHASE_ASLEEP, bl.currentPhase());

    TEST_ASSERT_TRUE(bl.resetDimTimeout());
    TEST_ASSERT_FALSE(bl.isInactive());
    TEST_ASSERT_TRUE(bl.isDimmed());
    TEST_ASSERT_EQUAL(BacklightControl::PHASE_WAKE_OUT, bl.currentPhase());
    TEST_ASSERT_EQUAL_UINT8(0, fade_percent);
    TEST_ASSERT_EQUAL_UINT16(100, fade_ms);

    /* A second finger inside the wake is part of it. */
    TEST_ASSERT_TRUE(bl.resetDimTimeout());

    run_for(bl, 100);
    TEST_ASSERT_FALSE(bl.isDimmed());
    TEST_ASSERT_EQUAL(BacklightControl::PHASE_WAKE_DARK, bl.currentPhase());

    run_for(bl, 40);
    TEST_ASSERT_EQUAL(BacklightControl::PHASE_WAKE_IN, bl.currentPhase());
    TEST_ASSERT_EQUAL_UINT8(100, fade_percent);
    TEST_ASSERT_EQUAL_UINT16(180, fade_ms);

    run_for(bl, 180);
    TEST_ASSERT_EQUAL(BacklightControl::PHASE_AWAKE, bl.currentPhase());

    /* And the timeout was re-armed by the tap, not by the end of the fade. */
    TEST_ASSERT_FALSE(bl.isInactive());
    run_for(bl, 10000 - 320 - 1);
    TEST_ASSERT_FALSE(bl.isInactive());
    run_for(bl, 1);
    TEST_ASSERT_TRUE(bl.isInactive());
}

/* Caught on the way down: back up at once, and no screen ever swapped -- the
 * dim state never set. */
static void test_a_tap_during_the_fade_out_turns_it_back(void)
{
    BacklightControl bl;

    make(bl, true);
    run_for(bl, 10000 + 700);
    TEST_ASSERT_EQUAL(BacklightControl::PHASE_SLEEP_OUT, bl.currentPhase());

    TEST_ASSERT_TRUE(bl.resetDimTimeout());
    TEST_ASSERT_FALSE(bl.isDimmed());
    TEST_ASSERT_FALSE(bl.isInactive());
    TEST_ASSERT_EQUAL(BacklightControl::PHASE_AWAKE, bl.currentPhase());
    TEST_ASSERT_EQUAL_UINT8(100, fade_percent);

    run_for(bl, 2000);
    TEST_ASSERT_FALSE(bl.isDimmed());
}

/* A brightness setting moved mid-transition does not cut into it; the
 * transition ends at the level the setting now says. */
static void test_settings_do_not_cut_into_a_transition(void)
{
    BacklightControl bl;

    make(bl, true);
    run_for(bl, 10000 + 100);

    unsigned before = fade_count;

    bl.setNormalBrightness(80);
    bl.setDimBrightness(20);
    TEST_ASSERT_EQUAL_UINT(before, fade_count);

    run_for(bl, 1400 + 60);
    TEST_ASSERT_EQUAL_UINT8(20, fade_percent);

    run_for(bl, 800);
    bl.resetDimTimeout();
    run_for(bl, 100 + 40);
    TEST_ASSERT_EQUAL_UINT8(80, fade_percent);
}

/* A timeout of 0 never dims, either way. */
static void test_no_timeout_never_dims(void)
{
    BacklightControl bl;

    make(bl, true);
    bl.setDimTimeout(0);
    bl.resetDimTimeout();

    run_for(bl, 100000);
    TEST_ASSERT_FALSE(bl.isInactive());
    TEST_ASSERT_EQUAL(BacklightControl::PHASE_AWAKE, bl.currentPhase());
}

void test_backlight_control_run(void)
{
    RUN_TEST(test_the_plain_dim_is_unchanged);
    RUN_TEST(test_equal_levels_do_not_swallow_the_tap);
    RUN_TEST(test_sleep_goes_through_black);
    RUN_TEST(test_wake_goes_through_black);
    RUN_TEST(test_a_tap_during_the_fade_out_turns_it_back);
    RUN_TEST(test_settings_do_not_cut_into_a_transition);
    RUN_TEST(test_no_timeout_never_dims);
}
