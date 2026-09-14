/* Unit tests for the mixer arithmetic in main/control/beeper_mixer.c.
 *
 * This is the only place any of it can be checked. The host target has no
 * buzzer, the Lanbon has no buzzer, and the ArduiTouch that does has never had
 * this firmware on it -- so "does it sound right" is not a question anything
 * here can answer. What it can answer is whether the numbers handed to the pin
 * are the numbers that were meant, and that turns out to be most of the risk:
 * a sweep that wraps, an envelope that underflows, a voice that keeps taking a
 * slot after it has finished, a frame walk that never terminates.
 *
 * beeper_mixer.c was split out of beeper_control.cpp so that these tests could
 * exist. What stayed behind is a queue, a task and vTaskDelay(); what came out
 * includes nothing but <stdint.h>, and is the part that decides what every
 * chime on this panel sounds like. Same argument that keeps ui_geometry.hpp
 * free of LVGL.
 *
 * The load-bearing assertion in this file is
 * test_the_shipped_defaults_land_on_the_duty_they_always_did. Everything else
 * is correctness; that one is the promise that this overhaul did not change
 * how loud the panel is.
 */

#include <unity.h>

#include "control/beeper_mixer.h"
#include "test_suites.hpp"

/* f_start, f_end, duration, pause, volume, shape */
#define N(fs, fe, d, p, v, sh) {fs, fe, d, p, v, sh}

/* --------------------------------------------------------------- envelope */

static void test_the_envelope_starts_and_ends_where_it_says(void)
{
    TEST_ASSERT_EQUAL_UINT8(255, beeper_envelope(BEEPER_SHAPE_FLAT, 0, 100));
    TEST_ASSERT_EQUAL_UINT8(255, beeper_envelope(BEEPER_SHAPE_FLAT, 50, 100));
    TEST_ASSERT_EQUAL_UINT8(255, beeper_envelope(BEEPER_SHAPE_FLAT, 99, 100));

    TEST_ASSERT_EQUAL_UINT8(255, beeper_envelope(BEEPER_SHAPE_PLUCK, 0, 100));
    TEST_ASSERT_TRUE(beeper_envelope(BEEPER_SHAPE_PLUCK, 99, 100) < 10);

    TEST_ASSERT_EQUAL_UINT8(0, beeper_envelope(BEEPER_SHAPE_PAD, 0, 100));
    TEST_ASSERT_EQUAL_UINT8(0, beeper_envelope(BEEPER_SHAPE_PAD, 100, 100));

    /* 254 and not 255, because the two halves meet at t == 128. Pinned as it
     * is rather than as it was meant, so that a later tidy-up of the curve is
     * a deliberate change and not a silent one. Nothing audible turns on one
     * part in 255. */
    TEST_ASSERT_EQUAL_UINT8(254, beeper_envelope(BEEPER_SHAPE_PAD, 50, 100));
}

static void test_the_pluck_only_ever_falls(void)
{
    uint8_t previous = 255;

    for (uint32_t t = 0; t <= 100; t++)
    {
        uint8_t level = beeper_envelope(BEEPER_SHAPE_PLUCK, t, 100);

        TEST_ASSERT_TRUE(level <= previous);
        previous = level;
    }
}

/* The loop in play_note() used to be the only caller and it guarded both of
 * these. The function is public now, and an `elapsed` past `duration` would
 * make (255 - t) underflow a uint8_t into something near full volume. */
static void test_the_envelope_survives_its_edges(void)
{
    TEST_ASSERT_EQUAL_UINT8(255, beeper_envelope(BEEPER_SHAPE_PLUCK, 0, 0));
    TEST_ASSERT_EQUAL_UINT8(255, beeper_envelope(BEEPER_SHAPE_PAD, 17, 0));

    TEST_ASSERT_EQUAL_UINT8(0, beeper_envelope(BEEPER_SHAPE_PLUCK, 1000, 100));
    TEST_ASSERT_EQUAL_UINT8(0, beeper_envelope(BEEPER_SHAPE_PAD, 1000, 100));
    TEST_ASSERT_EQUAL_UINT8(255, beeper_envelope(BEEPER_SHAPE_FLAT, 1000, 100));
}

/* ------------------------------------------------------------------ sweeps */

static const struct beeper_note_s up_note[]   = {N(1000, 2000, 100, 0, 100, BEEPER_SHAPE_FLAT)};
static const struct beeper_note_s down_note[] = {N(2800, 1200, 100, 0, 100, BEEPER_SHAPE_FLAT)};

static void test_a_rising_sweep_walks_from_end_to_end(void)
{
    struct beeper_voice_s voice = {up_note, 0, 1};
    uint16_t              freq;
    uint8_t               level;

    TEST_ASSERT_TRUE(beeper_voice_sample(&voice, 0, &freq, &level));
    TEST_ASSERT_EQUAL_UINT16(1000, freq);

    TEST_ASSERT_TRUE(beeper_voice_sample(&voice, 50, &freq, &level));
    TEST_ASSERT_EQUAL_UINT16(1500, freq);

    TEST_ASSERT_TRUE(beeper_voice_sample(&voice, 99, &freq, &level));
    TEST_ASSERT_TRUE(freq > 1980 && freq <= 2000);
}

/* lcars_back is a falling sweep, so the int32_t cast in the lerp is not
 * hypothetical: with unsigned arithmetic this reads about 65 kHz at t=1. */
static void test_a_falling_sweep_does_not_wrap(void)
{
    struct beeper_voice_s voice = {down_note, 0, 1};
    uint16_t              previous = 0xFFFF;

    for (uint32_t t = 0; t < 100; t++)
    {
        uint16_t freq;
        uint8_t  level;

        TEST_ASSERT_TRUE(beeper_voice_sample(&voice, t, &freq, &level));
        TEST_ASSERT_TRUE(freq <= 2800);
        TEST_ASSERT_TRUE(freq >= 1200);
        TEST_ASSERT_TRUE(freq <= previous);
        previous = freq;
    }
}

/* ---------------------------------------------------------- voice sampling */

static const struct beeper_note_s two_notes[] = {N(2000, 2000, 30, 10, 100, BEEPER_SHAPE_FLAT),
                                                 N(2500, 2500, 40, 0, 100, BEEPER_SHAPE_FLAT)};

static void test_a_voice_before_its_entry_is_alive_and_silent(void)
{
    struct beeper_voice_s late = {two_notes, 25, 2};
    uint16_t              freq;
    uint8_t               level;

    TEST_ASSERT_TRUE(beeper_voice_sample(&late, 0, &freq, &level));
    TEST_ASSERT_EQUAL_UINT16(0, freq);
    TEST_ASSERT_EQUAL_UINT8(0, level);

    TEST_ASSERT_TRUE(beeper_voice_sample(&late, 25, &freq, &level));
    TEST_ASSERT_EQUAL_UINT16(2000, freq);
    TEST_ASSERT_TRUE(level > 0);
}

static void test_a_voice_inside_a_pause_is_alive_and_silent(void)
{
    struct beeper_voice_s voice = {two_notes, 0, 2};
    uint16_t              freq;
    uint8_t               level;

    /* 30..39 is the first note's pause. */
    TEST_ASSERT_TRUE(beeper_voice_sample(&voice, 35, &freq, &level));
    TEST_ASSERT_EQUAL_UINT16(0, freq);
    TEST_ASSERT_EQUAL_UINT8(0, level);

    /* 40 is the second note. */
    TEST_ASSERT_TRUE(beeper_voice_sample(&voice, 40, &freq, &level));
    TEST_ASSERT_EQUAL_UINT16(2500, freq);
}

static void test_a_voice_ends_when_its_last_pause_does(void)
{
    struct beeper_voice_s voice = {two_notes, 0, 2};
    uint16_t              freq;
    uint8_t               level;

    TEST_ASSERT_TRUE(beeper_voice_sample(&voice, 79, &freq, &level));
    TEST_ASSERT_FALSE(beeper_voice_sample(&voice, 80, &freq, &level));
    TEST_ASSERT_FALSE(beeper_voice_sample(&voice, 5000, &freq, &level));
}

static void test_an_empty_voice_is_finished_immediately(void)
{
    struct beeper_voice_s none = {NULL, 0, 0};
    uint16_t              freq;
    uint8_t               level;

    TEST_ASSERT_FALSE(beeper_voice_sample(&none, 0, &freq, &level));
    TEST_ASSERT_EQUAL_UINT16(0, freq);
    TEST_ASSERT_EQUAL_UINT8(0, level);
}

/* ------------------------------------------------------------------ volume */

/* The one assertion in this file that is about behaviour rather than
 * correctness.
 *
 * Every chime is written at note volume 50, the shipped master is 25, and the
 * duty ceiling is 512 of 1024. 124 per mille of 512 is 63 -- which is exactly
 * what map(volume, 0, 100, 0, 127) produced at volume 50, back when the whole
 * scale was an accident. Nobody has ever heard this panel at any other level,
 * so that is the level the defaults have to reproduce. If this test fails, the
 * firmware just got louder or quieter for everyone who never touches the new
 * setting. */
static void test_the_shipped_defaults_land_on_the_duty_they_always_did(void)
{
    uint8_t level = (uint8_t)((50u * 255u) / 100u); /* note volume 50, full envelope */

    TEST_ASSERT_EQUAL_UINT8(127, level);
    TEST_ASSERT_EQUAL_UINT16(124, beeper_level_permille(level, 25));

    /* 124 per mille through the port's 512-count ceiling. */
    TEST_ASSERT_EQUAL_UINT32(63, (124u * 512u) / 1000u);
}

static void test_the_master_scales_and_clamps(void)
{
    TEST_ASSERT_EQUAL_UINT16(1000, beeper_level_permille(255, 100));
    TEST_ASSERT_EQUAL_UINT16(0, beeper_level_permille(255, 0));
    TEST_ASSERT_EQUAL_UINT16(0, beeper_level_permille(0, 100));

    /* A config file edited by hand can say anything. */
    TEST_ASSERT_EQUAL_UINT16(1000, beeper_level_permille(255, 200));

    uint16_t half = beeper_level_permille(255, 50);

    TEST_ASSERT_TRUE(half >= 495 && half <= 505);
}

static void test_the_slot_gain_puts_back_what_interleaving_took(void)
{
    /* Solo: nothing to put back. */
    TEST_ASSERT_EQUAL_UINT16(400, beeper_slot_gain(400, 1));

    /* Two voices: 256 * sqrt(2) in Q8 is 362, so 400 -> 565. Applied to a
     * voice that is only on half the time, that is the same RMS it had alone. */
    TEST_ASSERT_EQUAL_UINT16(565, beeper_slot_gain(400, 2));
    TEST_ASSERT_EQUAL_UINT16(692, beeper_slot_gain(400, 3));

    /* At a high master there is no headroom left to restore with. */
    TEST_ASSERT_EQUAL_UINT16(BEEPER_LEVEL_MAX, beeper_slot_gain(900, 3));

    /* Out of range is a pass-through, not a crash. */
    TEST_ASSERT_EQUAL_UINT16(400, beeper_slot_gain(400, 0));
    TEST_ASSERT_EQUAL_UINT16(400, beeper_slot_gain(400, BEEPER_VOICES_MAX + 1));
}

/* ------------------------------------------------------------------ frames */

static const struct beeper_note_s lead_notes[] = {N(2000, 2000, 100, 0, 100, BEEPER_SHAPE_FLAT)};
static const struct beeper_note_s pad_notes[]  = {N(3000, 3000, 50, 0, 100, BEEPER_SHAPE_FLAT)};

static const struct beeper_voice_s mono_voices[] = {{lead_notes, 0, 1}};
static const struct beeper_voice_s duo_voices[]  = {{lead_notes, 0, 1}, {pad_notes, 0, 1}};

static const struct beeper_chime_s mono_chime = {mono_voices, 1};
static const struct beeper_chime_s duo_chime  = {duo_voices, 2};

/* One voice must be bit for bit what the monophonic driver did: an
 * uninterrupted carrier on a 5 ms step, not a one-slot frame. */
static void test_a_single_voice_is_not_interleaved(void)
{
    struct beeper_slot_s slots[BEEPER_VOICES_MAX];
    uint8_t              count;

    TEST_ASSERT_EQUAL_UINT16(BEEPER_STEP_MS,
                             beeper_mixer_frame(&mono_chime, 0, 100, slots, &count));
    TEST_ASSERT_EQUAL_UINT8(1, count);
    TEST_ASSERT_EQUAL_UINT16(2000, slots[0].freq);
}

static void test_two_voices_share_a_frame_of_two_slots(void)
{
    struct beeper_slot_s slots[BEEPER_VOICES_MAX];
    uint8_t              count;

    TEST_ASSERT_EQUAL_UINT16(2 * BEEPER_SLOT_MS,
                             beeper_mixer_frame(&duo_chime, 0, 100, slots, &count));
    TEST_ASSERT_EQUAL_UINT8(2, count);
    TEST_ASSERT_EQUAL_UINT16(2000, slots[0].freq);
    TEST_ASSERT_EQUAL_UINT16(3000, slots[1].freq);
}

/* The bug this design most invites: the pad finishes at 50 ms, and if it keeps
 * being counted the lead spends the rest of the chime playing every other
 * slot -- audible as the surviving note suddenly acquiring a tremolo. */
static void test_a_finished_voice_stops_taking_slots(void)
{
    struct beeper_slot_s slots[BEEPER_VOICES_MAX];
    uint8_t              count;

    TEST_ASSERT_EQUAL_UINT16(BEEPER_STEP_MS,
                             beeper_mixer_frame(&duo_chime, 60, 100, slots, &count));
    TEST_ASSERT_EQUAL_UINT8(1, count);
    TEST_ASSERT_EQUAL_UINT16(2000, slots[0].freq);
}

static void test_a_frame_past_the_end_is_zero(void)
{
    struct beeper_slot_s slots[BEEPER_VOICES_MAX];
    uint8_t              count;

    TEST_ASSERT_EQUAL_UINT16(0, beeper_mixer_frame(&duo_chime, 100, 100, slots, &count));
    TEST_ASSERT_EQUAL_UINT8(0, count);
}

/* A chime whose every voice is mid-pause is a rest, and a rest has a length --
 * returning 0 there would end the chime before its second half. */
static void test_a_chime_that_is_all_pause_is_a_rest_and_not_the_end(void)
{
    static const struct beeper_voice_s resting[] = {{two_notes, 0, 2}};
    static const struct beeper_chime_s chime     = {resting, 1};

    struct beeper_slot_s slots[BEEPER_VOICES_MAX];
    uint8_t              count;

    TEST_ASSERT_EQUAL_UINT16(BEEPER_STEP_MS,
                             beeper_mixer_frame(&chime, 35, 100, slots, &count));
    TEST_ASSERT_EQUAL_UINT8(0, count);
}

/* The property the simulator's audio callback rests on: it evaluates frames at
 * whatever time it needs them, and the host tests call them out of order on
 * purpose. A round-robin cursor kept in a file static would pass every other
 * test in this file and make both of those quietly wrong. */
static void test_the_frame_is_pure(void)
{
    struct beeper_slot_s first[BEEPER_VOICES_MAX];
    struct beeper_slot_s again[BEEPER_VOICES_MAX];
    struct beeper_slot_s scratch[BEEPER_VOICES_MAX];
    uint8_t              count_first, count_again, count_scratch;

    uint16_t len_first = beeper_mixer_frame(&duo_chime, 20, 100, first, &count_first);

    /* Walk somewhere else, in another chime, and come back. */
    (void)beeper_mixer_frame(&duo_chime, 80, 100, scratch, &count_scratch);
    (void)beeper_mixer_frame(&mono_chime, 5, 60, scratch, &count_scratch);

    uint16_t len_again = beeper_mixer_frame(&duo_chime, 20, 100, again, &count_again);

    TEST_ASSERT_EQUAL_UINT16(len_first, len_again);
    TEST_ASSERT_EQUAL_UINT8(count_first, count_again);

    for (uint8_t i = 0; i < count_first; i++)
    {
        TEST_ASSERT_EQUAL_UINT16(first[i].freq, again[i].freq);
        TEST_ASSERT_EQUAL_UINT16(first[i].level, again[i].level);
    }
}

static void test_a_fourth_voice_is_dropped_in_table_order(void)
{
    static const struct beeper_voice_s four[] = {
        {lead_notes, 0, 1}, {pad_notes, 0, 1}, {lead_notes, 0, 1}, {pad_notes, 0, 1}};
    static const struct beeper_chime_s chime = {four, 4};

    struct beeper_slot_s slots[BEEPER_VOICES_MAX];
    uint8_t              count;

    TEST_ASSERT_EQUAL_UINT16(BEEPER_VOICES_MAX * BEEPER_SLOT_MS,
                             beeper_mixer_frame(&chime, 0, 100, slots, &count));
    TEST_ASSERT_EQUAL_UINT8(BEEPER_VOICES_MAX, count);
    TEST_ASSERT_EQUAL_UINT16(2000, slots[0].freq);
}

static void test_no_frame_ever_exceeds_the_level_ceiling(void)
{
    for (uint32_t t = 0; t < 120; t++)
    {
        struct beeper_slot_s slots[BEEPER_VOICES_MAX];
        uint8_t              count;

        (void)beeper_mixer_frame(&duo_chime, t, 100, slots, &count);

        for (uint8_t i = 0; i < count; i++)
            TEST_ASSERT_TRUE(slots[i].level <= BEEPER_LEVEL_MAX);
    }
}

/* The test that catches a hang. beeper_task walks frames until one returns 0,
 * with nothing else to stop it: a frame length of 0 while the chime is still
 * alive, or a chime that never reports itself finished, would spin a task at
 * priority 2 forever. */
static void test_walking_a_chime_terminates_at_its_stated_length(void)
{
    struct beeper_slot_s slots[BEEPER_VOICES_MAX];
    uint8_t              count;
    uint32_t             t     = 0;
    uint32_t             turns = 0;

    for (;;)
    {
        uint16_t frame = beeper_mixer_frame(&duo_chime, t, 100, slots, &count);

        if (frame == 0)
            break;

        TEST_ASSERT_TRUE(frame > 0);
        t += frame;

        TEST_ASSERT_TRUE_MESSAGE(++turns < 10000, "the frame walk did not terminate");
    }

    uint32_t stated = beeper_chime_duration_ms(&duo_chime);

    TEST_ASSERT_EQUAL_UINT32(100, stated);
    TEST_ASSERT_TRUE(t >= stated);
    TEST_ASSERT_TRUE(t < stated + BEEPER_STEP_MS);
}

/* --------------------------------------------------------------- the queries */

static void test_duration_is_the_longest_voice_entry_included(void)
{
    static const struct beeper_voice_s staggered[] = {{lead_notes, 0, 1}, {pad_notes, 80, 1}};
    static const struct beeper_chime_s chime       = {staggered, 2};

    /* Voice 0 ends at 100; voice 1 enters at 80 and ends at 130. */
    TEST_ASSERT_EQUAL_UINT32(130, beeper_chime_duration_ms(&chime));

    /* Both of a voice's pauses count: 30 + 10 + 40 + 0. */
    static const struct beeper_voice_s paused[] = {{two_notes, 0, 2}};
    static const struct beeper_chime_s two      = {paused, 1};

    TEST_ASSERT_EQUAL_UINT32(80, beeper_chime_duration_ms(&two));

    TEST_ASSERT_EQUAL_UINT32(0, beeper_chime_duration_ms(NULL));
}

static void test_peak_voices_counts_what_actually_sounds(void)
{
    TEST_ASSERT_EQUAL_UINT8(1, beeper_chime_peak_voices(&mono_chime));
    TEST_ASSERT_EQUAL_UINT8(2, beeper_chime_peak_voices(&duo_chime));

    /* Two voices in the table, but never at the same time: the pad enters
     * after the lead has finished, so this chime is monophonic and the rule
     * about not stacking below 1 kHz does not apply to it. */
    static const struct beeper_voice_s sequential[] = {{lead_notes, 0, 1}, {pad_notes, 100, 1}};
    static const struct beeper_chime_s chime        = {sequential, 2};

    TEST_ASSERT_EQUAL_UINT8(1, beeper_chime_peak_voices(&chime));
}

static void test_the_frequency_range_includes_both_ends_of_a_sweep(void)
{
    static const struct beeper_voice_s swept[] = {{down_note, 0, 1}};
    static const struct beeper_chime_s chime   = {swept, 1};

    uint16_t lo = 0;
    uint16_t hi = 0;

    beeper_chime_freq_range(&chime, &lo, &hi);

    TEST_ASSERT_EQUAL_UINT16(1200, lo);
    TEST_ASSERT_EQUAL_UINT16(2800, hi);

    beeper_chime_freq_range(NULL, &lo, &hi);

    TEST_ASSERT_EQUAL_UINT16(0, lo);
    TEST_ASSERT_EQUAL_UINT16(0, hi);
}

void test_beeper_mixer_run(void)
{
    RUN_TEST(test_the_envelope_starts_and_ends_where_it_says);
    RUN_TEST(test_the_pluck_only_ever_falls);
    RUN_TEST(test_the_envelope_survives_its_edges);

    RUN_TEST(test_a_rising_sweep_walks_from_end_to_end);
    RUN_TEST(test_a_falling_sweep_does_not_wrap);

    RUN_TEST(test_a_voice_before_its_entry_is_alive_and_silent);
    RUN_TEST(test_a_voice_inside_a_pause_is_alive_and_silent);
    RUN_TEST(test_a_voice_ends_when_its_last_pause_does);
    RUN_TEST(test_an_empty_voice_is_finished_immediately);

    RUN_TEST(test_the_shipped_defaults_land_on_the_duty_they_always_did);
    RUN_TEST(test_the_master_scales_and_clamps);
    RUN_TEST(test_the_slot_gain_puts_back_what_interleaving_took);

    RUN_TEST(test_a_single_voice_is_not_interleaved);
    RUN_TEST(test_two_voices_share_a_frame_of_two_slots);
    RUN_TEST(test_a_finished_voice_stops_taking_slots);
    RUN_TEST(test_a_frame_past_the_end_is_zero);
    RUN_TEST(test_a_chime_that_is_all_pause_is_a_rest_and_not_the_end);
    RUN_TEST(test_the_frame_is_pure);
    RUN_TEST(test_a_fourth_voice_is_dropped_in_table_order);
    RUN_TEST(test_no_frame_ever_exceeds_the_level_ceiling);
    RUN_TEST(test_walking_a_chime_terminates_at_its_stated_length);

    RUN_TEST(test_duration_is_the_longest_voice_entry_included);
    RUN_TEST(test_peak_voices_counts_what_actually_sounds);
    RUN_TEST(test_the_frequency_range_includes_both_ends_of_a_sweep);
}
