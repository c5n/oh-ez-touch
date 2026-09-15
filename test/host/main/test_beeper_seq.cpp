/* Unit tests for the sequencer arithmetic in main/control/beeper_seq.c.
 *
 * Same argument as test_beeper_mixer.cpp, and the same limits: the host target
 * has no buzzer, the Lanbon has no buzzer, and "does it sound right" is not a
 * question anything here can answer. What it can answer is whether the numbers
 * handed to the pin are the numbers that were meant -- and this engine has more
 * ways to get that wrong than the mixer did, because it multiplies.
 *
 * The four that would actually bite, and that each have a test by name:
 * a falling sweep that wraps unsigned; an LFO fast enough to alias against
 * BEEPER_SEQ_STEP_MS or to overflow its own multiply; a tremolo that pushes a
 * note above the peak its envelope asked for; and a zero-length note that
 * reaches the modulo in beeper_seq_frame().
 */

#include <unity.h>

#include "control/beeper_seq.h"
#include "test_suites.hpp"

/* f_start, f_end, duration, pause, volume, env, fx, repeat */
#define N(fs, fe, d, p, v, e, x, r) {fs, fe, d, p, v, e, x, r}

/* --------------------------------------------------------------- envelope */

static void test_the_envelope_starts_and_ends_where_it_says(void)
{
    TEST_ASSERT_EQUAL_UINT8(255, beeper_seq_envelope(BEEPER_SEQ_ENV_FLAT, 0, 100));
    TEST_ASSERT_EQUAL_UINT8(255, beeper_seq_envelope(BEEPER_SEQ_ENV_FLAT, 50, 100));
    TEST_ASSERT_EQUAL_UINT8(255, beeper_seq_envelope(BEEPER_SEQ_ENV_FLAT, 99, 100));

    TEST_ASSERT_EQUAL_UINT8(255, beeper_seq_envelope(BEEPER_SEQ_ENV_PLUCK, 0, 100));
    TEST_ASSERT_TRUE(beeper_seq_envelope(BEEPER_SEQ_ENV_PLUCK, 99, 100) < 10);

    TEST_ASSERT_EQUAL_UINT8(0, beeper_seq_envelope(BEEPER_SEQ_ENV_PAD, 0, 100));

    /* A true 255 here, where the mixer's PAD peaks at 254 because its two
     * halves meet at t == 128. The mixer's 254 is pinned on purpose; this is
     * the same shape built out of stages rather than halves, and the two are
     * allowed to differ by one part in 255 rather than one being bent to match
     * the other. */
    TEST_ASSERT_EQUAL_UINT8(255, beeper_seq_envelope(BEEPER_SEQ_ENV_PAD, 50, 100));
}

static void test_the_pluck_only_ever_falls(void)
{
    uint8_t previous = 255;

    for (uint32_t t = 0; t <= 100; t++)
    {
        uint8_t level = beeper_seq_envelope(BEEPER_SEQ_ENV_PLUCK, t, 100);

        TEST_ASSERT_TRUE(level <= previous);
        previous = level;
    }
}

static void test_the_envelope_survives_its_edges(void)
{
    /* No duration to divide by: the peak, not a fault. */
    TEST_ASSERT_EQUAL_UINT8(255, beeper_seq_envelope(BEEPER_SEQ_ENV_PAD, 0, 0));
    TEST_ASSERT_EQUAL_UINT8(255, beeper_seq_envelope(BEEPER_SEQ_ENV_PLUCK, 99, 0));

    /* At or past the end is silence rather than whatever it was holding, and
     * rather than an underflow. */
    TEST_ASSERT_EQUAL_UINT8(0, beeper_seq_envelope(BEEPER_SEQ_ENV_FLAT, 100, 100));
    TEST_ASSERT_EQUAL_UINT8(0, beeper_seq_envelope(BEEPER_SEQ_ENV_FLAT, 5000, 100));

    /* An index nobody defined is row zero, not a read off the end. */
    TEST_ASSERT_EQUAL_UINT8(255, beeper_seq_envelope(200, 50, 100));
}

static void test_the_sustain_plateau_is_actually_flat(void)
{
    /* STAB holds at its sustain between the decay and the release. Whatever the
     * row's numbers, the middle of a long note must not drift. */
    uint8_t a = beeper_seq_envelope(BEEPER_SEQ_ENV_STAB, 200, 400);
    uint8_t b = beeper_seq_envelope(BEEPER_SEQ_ENV_STAB, 250, 400);

    TEST_ASSERT_EQUAL_UINT8(a, b);
    TEST_ASSERT_EQUAL_UINT8(beeper_seq_env_preset(BEEPER_SEQ_ENV_STAB)->sustain, a);
}

static void test_one_envelope_row_fits_a_tick_and_a_boot_note(void)
{
    /* The whole reason the stages are percentages: the same row has to describe
     * a twelve-millisecond contact tick and a four-hundred-millisecond boot
     * note and leave them sounding like the same instrument.
     *
     * BLOOM's attack is 25 % with a 60 ms cap. On the short note the percentage
     * decides and the shape is fractional; on the long one the cap binds, and
     * that is the half of the pair that stops a 25 % attack becoming a
     * hundred-millisecond fade-in. */
    const struct beeper_seq_env_s *e = beeper_seq_env_preset(BEEPER_SEQ_ENV_BLOOM);

    TEST_ASSERT_EQUAL_UINT8(25, e->attack_pct);
    TEST_ASSERT_EQUAL_UINT16(60, e->attack_max_ms);

    /* A quarter of the way into the attack of each, the fractional level is the
     * same -- which it would not be if the stage were written in milliseconds. */
    uint8_t shortish = beeper_seq_envelope(BEEPER_SEQ_ENV_BLOOM, 1, 12);  /* a = 3  */
    uint8_t longish  = beeper_seq_envelope(BEEPER_SEQ_ENV_BLOOM, 20, 240); /* a = 60 */

    TEST_ASSERT_UINT8_WITHIN(6, shortish, longish);

    /* And the cap binds where it should: at 240 ms, 25 % would be 60 -- exactly
     * the cap -- while at 400 ms it would be 100 and is held to 60. So the peak
     * arrives at 60 ms on the long note, not at 100. */
    TEST_ASSERT_EQUAL_UINT8(255, beeper_seq_envelope(BEEPER_SEQ_ENV_BLOOM, 60, 400));
    TEST_ASSERT_TRUE(beeper_seq_envelope(BEEPER_SEQ_ENV_BLOOM, 59, 400) < 255);
}

/* ------------------------------------------------------------------ sweep */

static void test_a_rising_sweep_walks_from_end_to_end(void)
{
    struct beeper_seq_note_s n = N(1000, 2000, 100, 0, 50, BEEPER_SEQ_ENV_FLAT,
                                   BEEPER_SEQ_FX_NONE, 1);

    TEST_ASSERT_EQUAL_UINT16(1000, beeper_seq_pitch(&n, 0));
    TEST_ASSERT_EQUAL_UINT16(1500, beeper_seq_pitch(&n, 50));
    TEST_ASSERT_EQUAL_UINT16(2000, beeper_seq_pitch(&n, 100));

    /* Past the end is clamped, not extrapolated. */
    TEST_ASSERT_EQUAL_UINT16(2000, beeper_seq_pitch(&n, 5000));
}

static void test_a_falling_sweep_does_not_wrap(void)
{
    /* The one that would be a 65 kHz screech if the span were computed
     * unsigned. Same bug and same fix as the mixer's. */
    struct beeper_seq_note_s n = N(2800, 1200, 80, 0, 50, BEEPER_SEQ_ENV_FLAT,
                                   BEEPER_SEQ_FX_NONE, 1);

    for (uint32_t t = 0; t <= 80; t++)
    {
        uint16_t f = beeper_seq_pitch(&n, t);

        TEST_ASSERT_TRUE(f <= 2800);
        TEST_ASSERT_TRUE(f >= 1200);
    }

    TEST_ASSERT_EQUAL_UINT16(2000, beeper_seq_pitch(&n, 40));
}

static void test_a_glide_is_monotone_and_hits_both_ends(void)
{
    struct beeper_seq_note_s n = N(1200, 2800, 100, 0, 50, BEEPER_SEQ_ENV_FLAT,
                                   BEEPER_SEQ_FX_GLIDE, 1);
    uint16_t previous = 0;

    TEST_ASSERT_UINT16_WITHIN(2, 1200, beeper_seq_pitch(&n, 0));
    TEST_ASSERT_UINT16_WITHIN(2, 2800, beeper_seq_pitch(&n, 100));

    for (uint32_t t = 0; t <= 100; t++)
    {
        uint16_t f = beeper_seq_pitch(&n, t);

        TEST_ASSERT_TRUE(f >= previous);
        previous = f;
    }
}

static void test_a_glide_lags_the_linear_sweep_in_the_middle(void)
{
    /* This is what "geometric" means here, and it is the whole point of the
     * glide: a linear sweep in hertz crosses its first octave in a third of the
     * note and then crawls. Interpolating the period spends equal time per
     * octave, so it sits below the straight line everywhere between the ends. */
    struct beeper_seq_note_s lin = N(1200, 2800, 100, 0, 50, BEEPER_SEQ_ENV_FLAT,
                                     BEEPER_SEQ_FX_NONE, 1);
    struct beeper_seq_note_s gl  = N(1200, 2800, 100, 0, 50, BEEPER_SEQ_ENV_FLAT,
                                     BEEPER_SEQ_FX_GLIDE, 1);

    for (uint32_t t = 1; t < 100; t++)
        TEST_ASSERT_TRUE(beeper_seq_pitch(&gl, t) < beeper_seq_pitch(&lin, t));
}

static void test_a_glide_with_a_zero_end_falls_back_rather_than_dividing(void)
{
    struct beeper_seq_note_s n = N(0, 2000, 100, 0, 50, BEEPER_SEQ_ENV_FLAT,
                                   BEEPER_SEQ_FX_GLIDE, 1);

    TEST_ASSERT_EQUAL_UINT16(1000, beeper_seq_pitch(&n, 50));

    struct beeper_seq_note_s m = N(2000, 0, 100, 0, 50, BEEPER_SEQ_ENV_FLAT,
                                   BEEPER_SEQ_FX_GLIDE, 1);

    TEST_ASSERT_EQUAL_UINT16(1000, beeper_seq_pitch(&m, 50));
}

/* -------------------------------------------------------------------- LFO */

static void test_the_lfo_is_a_triangle_that_starts_at_zero(void)
{
    /* 1000 cHz is 10 Hz, so one period is 100 ms. */
    TEST_ASSERT_EQUAL_INT8(0, beeper_seq_lfo(0, 1000));
    TEST_ASSERT_EQUAL_INT8(127, beeper_seq_lfo(25, 1000));
    TEST_ASSERT_EQUAL_INT8(0, beeper_seq_lfo(50, 1000));
    TEST_ASSERT_EQUAL_INT8(-127, beeper_seq_lfo(75, 1000));
    TEST_ASSERT_EQUAL_INT8(0, beeper_seq_lfo(100, 1000));

    /* And it repeats, rather than running away. */
    TEST_ASSERT_EQUAL_INT8(127, beeper_seq_lfo(925, 1000));
}

static void test_the_lfo_is_bounded_and_off_at_zero_rate(void)
{
    for (uint32_t t = 0; t < 4000; t++)
    {
        int8_t v = beeper_seq_lfo(t, BEEPER_SEQ_LFO_MAX_CHZ);

        TEST_ASSERT_TRUE(v >= -127);
        TEST_ASSERT_TRUE(v <= 127);

        TEST_ASSERT_EQUAL_INT8(0, beeper_seq_lfo(t, 0));
    }
}

/* --------------------------------------------------------- the modulators */

static void test_a_vibrato_stays_within_its_depth_of_the_carrier(void)
{
    /* SIREN is the deepest row shipped: 120 per mille either side. */
    struct beeper_seq_note_s n = N(2000, 2000, 400, 0, 50, BEEPER_SEQ_ENV_FLAT,
                                   BEEPER_SEQ_FX_SIREN, 1);
    uint32_t swing = (2000u * 120u) / 1000u;
    bool     high  = false;
    bool     low   = false;

    for (uint32_t t = 0; t <= 400; t++)
    {
        uint16_t f = beeper_seq_pitch(&n, t);

        TEST_ASSERT_TRUE(f <= 2000 + swing);
        TEST_ASSERT_TRUE(f >= 2000 - swing);

        if (f > 2000 + swing - 4)
            high = true;
        if (f < 2000 - swing + 4)
            low = true;
    }

    /* It has to actually reach both ends, or the depth is a lie. */
    TEST_ASSERT_TRUE(high);
    TEST_ASSERT_TRUE(low);
}

static void test_the_deepest_vibrato_at_the_top_of_the_band_does_not_overflow(void)
{
    /* The multiply that would wrap if the depth were applied before the divide.
     * 3900 Hz is the highest note any family writes. */
    struct beeper_seq_note_s n = N(3900, 3900, 400, 0, 100, BEEPER_SEQ_ENV_FLAT,
                                   BEEPER_SEQ_FX_SIREN, 1);

    for (uint32_t t = 0; t <= 400; t++)
    {
        uint16_t f = beeper_seq_pitch(&n, t);

        TEST_ASSERT_TRUE(f > 3000);
        TEST_ASSERT_TRUE(f < 4500);
    }
}

static void test_a_vibrato_never_silences_a_sounding_note(void)
{
    /* port_beeper_tone() reads a frequency of 0 as silence, so an ornament must
     * not be able to produce one however low the carrier is. */
    struct beeper_seq_note_s n = N(3, 3, 400, 0, 50, BEEPER_SEQ_ENV_FLAT,
                                   BEEPER_SEQ_FX_SIREN, 1);

    for (uint32_t t = 0; t <= 400; t++)
        TEST_ASSERT_TRUE(beeper_seq_pitch(&n, t) >= 1);
}

static void test_a_tremolo_only_ever_goes_down(void)
{
    /* PULSE is the deepest tremolo shipped. The peak a note reaches with one
     * must never exceed the peak it reaches without. */
    struct beeper_seq_note_s plain = N(2000, 2000, 300, 0, 50, BEEPER_SEQ_ENV_FLAT,
                                       BEEPER_SEQ_FX_NONE, 1);
    struct beeper_seq_note_s mod   = N(2000, 2000, 300, 0, 50, BEEPER_SEQ_ENV_FLAT,
                                       BEEPER_SEQ_FX_PULSE, 1);
    uint8_t ceiling = beeper_seq_amplitude(&plain, 0);
    bool    dipped  = false;

    for (uint32_t t = 0; t <= 300; t++)
    {
        uint8_t a = beeper_seq_amplitude(&mod, t);

        TEST_ASSERT_TRUE(a <= ceiling);

        if (a < ceiling)
            dipped = true;
    }

    TEST_ASSERT_TRUE(dipped);
}

/* ----------------------------------------------------------------- frames */

static void test_a_frame_reads_the_note_it_is_inside(void)
{
    static const struct beeper_seq_note_s notes[] = {
        N(2000, 2000, 20, 10, 50, BEEPER_SEQ_ENV_FLAT, BEEPER_SEQ_FX_NONE, 1),
        N(2500, 2500, 20, 0, 50, BEEPER_SEQ_ENV_FLAT, BEEPER_SEQ_FX_NONE, 1)};
    struct beeper_seq_s  seq = {notes, 2};
    struct beeper_slot_s slot;

    TEST_ASSERT_EQUAL_UINT16(BEEPER_SEQ_STEP_MS,
                             beeper_seq_frame(&seq, 0, 100, &slot));
    TEST_ASSERT_EQUAL_UINT16(2000, slot.freq);
    TEST_ASSERT_TRUE(slot.level > 0);

    TEST_ASSERT_EQUAL_UINT16(BEEPER_SEQ_STEP_MS,
                             beeper_seq_frame(&seq, 35, 100, &slot));
    TEST_ASSERT_EQUAL_UINT16(2500, slot.freq);
}

static void test_a_pause_is_a_rest_and_not_the_end(void)
{
    static const struct beeper_seq_note_s notes[] = {
        N(2000, 2000, 20, 20, 50, BEEPER_SEQ_ENV_FLAT, BEEPER_SEQ_FX_NONE, 1),
        N(2500, 2500, 20, 0, 50, BEEPER_SEQ_ENV_FLAT, BEEPER_SEQ_FX_NONE, 1)};
    struct beeper_seq_s  seq = {notes, 2};
    struct beeper_slot_s slot;

    /* Inside the first note's pause: a step long, silent, and not zero. */
    TEST_ASSERT_EQUAL_UINT16(BEEPER_SEQ_STEP_MS,
                             beeper_seq_frame(&seq, 25, 100, &slot));
    TEST_ASSERT_EQUAL_UINT16(0, slot.level);
}

static void test_a_frame_past_the_end_is_zero(void)
{
    static const struct beeper_seq_note_s notes[] = {
        N(2000, 2000, 20, 0, 50, BEEPER_SEQ_ENV_FLAT, BEEPER_SEQ_FX_NONE, 1)};
    struct beeper_seq_s  seq = {notes, 1};
    struct beeper_slot_s slot;

    TEST_ASSERT_EQUAL_UINT16(0, beeper_seq_frame(&seq, 20, 100, &slot));
    TEST_ASSERT_EQUAL_UINT16(0, slot.freq);
    TEST_ASSERT_EQUAL_UINT16(0, slot.level);

    struct beeper_seq_s empty = {NULL, 0};

    TEST_ASSERT_EQUAL_UINT16(0, beeper_seq_frame(&empty, 0, 100, &slot));
    TEST_ASSERT_EQUAL_UINT16(0, beeper_seq_frame(NULL, 0, 100, &slot));
}

static void test_a_zero_length_note_is_stepped_over_and_not_divided_by(void)
{
    /* A note with neither duration nor pause has a span of zero. The `>=` in
     * the frame walk is what keeps it away from the modulo; a `>` there would
     * be a division by zero on the first frame. */
    static const struct beeper_seq_note_s notes[] = {
        N(2000, 2000, 0, 0, 50, BEEPER_SEQ_ENV_FLAT, BEEPER_SEQ_FX_NONE, 1),
        N(2500, 2500, 20, 0, 50, BEEPER_SEQ_ENV_FLAT, BEEPER_SEQ_FX_NONE, 1)};
    struct beeper_seq_s  seq = {notes, 2};
    struct beeper_slot_s slot;

    TEST_ASSERT_EQUAL_UINT16(BEEPER_SEQ_STEP_MS,
                             beeper_seq_frame(&seq, 0, 100, &slot));
    TEST_ASSERT_EQUAL_UINT16(2500, slot.freq);
}

static void test_the_frame_is_pure(void)
{
    /* Called out of order on purpose. The simulator evaluates a tune at times
     * of its own choosing from an audio callback, so a hidden static here would
     * make it quietly wrong rather than loudly broken. */
    static const struct beeper_seq_note_s notes[] = {
        N(2000, 2600, 40, 5, 50, BEEPER_SEQ_ENV_PLUCK, BEEPER_SEQ_FX_SHIMMER, 1),
        N(2600, 2600, 40, 0, 50, BEEPER_SEQ_ENV_PAD, BEEPER_SEQ_FX_NONE, 1)};
    struct beeper_seq_s  seq = {notes, 2};
    struct beeper_slot_s a;
    struct beeper_slot_s b;

    beeper_seq_frame(&seq, 30, 25, &a);

    for (uint32_t t = 0; t < 90; t += 7)
    {
        struct beeper_slot_s scratch;

        beeper_seq_frame(&seq, t, 25, &scratch);
    }

    beeper_seq_frame(&seq, 30, 25, &b);

    TEST_ASSERT_EQUAL_UINT16(a.freq, b.freq);
    TEST_ASSERT_EQUAL_UINT16(a.level, b.level);
}

static void test_walking_a_sequence_terminates_at_its_stated_length(void)
{
    /* Non-termination here would hang the beeper task *and* the audio
     * callback, so this is worth asserting rather than assuming. */
    static const struct beeper_seq_note_s notes[] = {
        N(2000, 2000, 20, 5, 50, BEEPER_SEQ_ENV_PLUCK, BEEPER_SEQ_FX_NONE, 3),
        N(2500, 2500, 30, 0, 50, BEEPER_SEQ_ENV_BELL, BEEPER_SEQ_FX_BREATHE, 1)};
    struct beeper_seq_s  seq = {notes, 2};
    struct beeper_slot_s slot;
    uint32_t             t     = 0;
    uint32_t             steps = 0;

    while (beeper_seq_frame(&seq, t, 25, &slot) != 0)
    {
        t += BEEPER_SEQ_STEP_MS;

        TEST_ASSERT_TRUE(++steps < 1000);
    }

    TEST_ASSERT_UINT32_WITHIN(BEEPER_SEQ_STEP_MS, beeper_seq_duration_ms(&seq), t);
}

/* ---------------------------------------------------------------- repeats */

static void test_a_repeat_of_zero_and_of_one_both_play_once(void)
{
    /* Load-bearing. A note left half-written by a table that forgot a field has
     * repeat 0, and it must be audible: silent is the failure mode nobody
     * notices. */
    static const struct beeper_seq_note_s once[] = {
        N(2000, 2000, 20, 0, 50, BEEPER_SEQ_ENV_FLAT, BEEPER_SEQ_FX_NONE, 1)};
    static const struct beeper_seq_note_s zero[] = {
        N(2000, 2000, 20, 0, 50, BEEPER_SEQ_ENV_FLAT, BEEPER_SEQ_FX_NONE, 0)};
    struct beeper_seq_s a = {once, 1};
    struct beeper_seq_s b = {zero, 1};

    TEST_ASSERT_EQUAL_UINT32(20, beeper_seq_duration_ms(&a));
    TEST_ASSERT_EQUAL_UINT32(20, beeper_seq_duration_ms(&b));
    TEST_ASSERT_EQUAL_UINT32(1, beeper_seq_note_count(&b));
}

static void test_a_repeat_restarts_the_envelope_on_every_pass(void)
{
    /* A trill is three strikes, not one long note with gaps cut into it. */
    static const struct beeper_seq_note_s notes[] = {
        N(2000, 2000, 20, 10, 50, BEEPER_SEQ_ENV_PLUCK, BEEPER_SEQ_FX_NONE, 3)};
    struct beeper_seq_s  seq = {notes, 1};
    struct beeper_slot_s first;
    struct beeper_slot_s second;
    struct beeper_slot_s third;

    beeper_seq_frame(&seq, 0, 100, &first);
    beeper_seq_frame(&seq, 30, 100, &second);
    beeper_seq_frame(&seq, 60, 100, &third);

    TEST_ASSERT_EQUAL_UINT16(first.level, second.level);
    TEST_ASSERT_EQUAL_UINT16(first.level, third.level);
    TEST_ASSERT_TRUE(first.level > 0);

    TEST_ASSERT_EQUAL_UINT32(90, beeper_seq_duration_ms(&seq));
    TEST_ASSERT_EQUAL_UINT32(3, beeper_seq_note_count(&seq));

    /* And the pass boundary really is a boundary: late in a pass the pluck has
     * decayed, and the next frame after it is back at the top. */
    struct beeper_slot_s late;

    beeper_seq_frame(&seq, 19, 100, &late);

    TEST_ASSERT_TRUE(late.level < first.level);
}

/* ----------------------------------------------------- presets and levels */

static void test_no_preset_asks_for_a_faster_lfo_than_the_step_can_carry(void)
{
    /* Two invariants in one assertion: above the cap the wobble aliases against
     * BEEPER_SEQ_STEP_MS -- so the simulator and the panel would disagree --
     * and beeper_seq_lfo()'s multiply loses its overflow bound. */
    for (uint8_t i = 0; i < BEEPER_SEQ_FX_COUNT; i++)
    {
        const struct beeper_seq_fx_s *fx = beeper_seq_fx_preset(i);

        TEST_ASSERT_TRUE(fx->vib_rate_chz <= BEEPER_SEQ_LFO_MAX_CHZ);
        TEST_ASSERT_TRUE(fx->trem_rate_chz <= BEEPER_SEQ_LFO_MAX_CHZ);
    }
}

static void test_every_preset_row_is_distinct_and_well_formed(void)
{
    for (uint8_t i = 0; i < BEEPER_SEQ_FX_COUNT; i++)
    {
        const struct beeper_seq_fx_s *a = beeper_seq_fx_preset(i);

        TEST_ASSERT_EQUAL_UINT8(0, a->reserved);
        TEST_ASSERT_TRUE(a->sweep < BEEPER_SEQ_SWEEP_COUNT);

        for (uint8_t j = (uint8_t)(i + 1); j < BEEPER_SEQ_FX_COUNT; j++)
        {
            const struct beeper_seq_fx_s *b = beeper_seq_fx_preset(j);

            /* A duplicate row is dead flash and a name that means nothing. */
            TEST_ASSERT_FALSE(a->vib_rate_chz == b->vib_rate_chz &&
                              a->trem_rate_chz == b->trem_rate_chz &&
                              a->vib_depth == b->vib_depth &&
                              a->trem_depth == b->trem_depth &&
                              a->sweep == b->sweep);
        }
    }

    for (uint8_t i = 0; i < BEEPER_SEQ_ENV_COUNT; i++)
    {
        const struct beeper_seq_env_s *a = beeper_seq_env_preset(i);

        TEST_ASSERT_TRUE(a->attack_pct <= 100);
        TEST_ASSERT_TRUE(a->decay_pct <= 100);
        TEST_ASSERT_TRUE(a->release_pct <= 100);

        for (uint8_t j = (uint8_t)(i + 1); j < BEEPER_SEQ_ENV_COUNT; j++)
        {
            const struct beeper_seq_env_s *b = beeper_seq_env_preset(j);

            TEST_ASSERT_FALSE(a->attack_pct == b->attack_pct &&
                              a->decay_pct == b->decay_pct &&
                              a->release_pct == b->release_pct &&
                              a->sustain == b->sustain &&
                              a->attack_max_ms == b->attack_max_ms &&
                              a->release_max_ms == b->release_max_ms);
        }
    }

    /* An index past the end is row zero rather than a read off the end. */
    TEST_ASSERT_EQUAL_PTR(beeper_seq_fx_preset(0), beeper_seq_fx_preset(200));
    TEST_ASSERT_EQUAL_PTR(beeper_seq_env_preset(0), beeper_seq_env_preset(200));
}

static void test_the_shipped_defaults_land_on_the_duty_they_always_did(void)
{
    /* The same promise test_beeper_mixer.cpp pins, re-asserted through this
     * engine: a foreground note at the shipped master volume of 25 must reach
     * the pin at the level it always has. Swapping engines changes what the
     * panel says; it must not change how loud it says it. */
    struct beeper_seq_note_s n = N(2093, 2093, 40, 0, 50, BEEPER_SEQ_ENV_FLAT,
                                   BEEPER_SEQ_FX_NONE, 1);
    uint8_t level = beeper_seq_amplitude(&n, 0);

    TEST_ASSERT_EQUAL_UINT8(127, level);
    TEST_ASSERT_EQUAL_UINT16(124, beeper_level_permille(level, 25));
    TEST_ASSERT_EQUAL_UINT32(63, (124u * 512u) / 1000u);
}

static void test_no_frame_ever_exceeds_the_level_ceiling(void)
{
    static const struct beeper_seq_note_s notes[] = {
        N(2000, 2600, 60, 0, 100, BEEPER_SEQ_ENV_FLAT, BEEPER_SEQ_FX_PULSE, 2)};
    struct beeper_seq_s  seq = {notes, 1};
    struct beeper_slot_s slot;

    for (uint32_t t = 0; t < 200; t += 1)
    {
        beeper_seq_frame(&seq, t, 100, &slot);

        TEST_ASSERT_TRUE(slot.level <= BEEPER_LEVEL_MAX);
    }
}

/* --------------------------------------------------------- the two ranges */

static void test_duration_counts_pauses_and_repeats(void)
{
    static const struct beeper_seq_note_s notes[] = {
        N(2000, 2000, 20, 5, 50, BEEPER_SEQ_ENV_FLAT, BEEPER_SEQ_FX_NONE, 3),
        N(2500, 2500, 40, 10, 50, BEEPER_SEQ_ENV_FLAT, BEEPER_SEQ_FX_NONE, 1)};
    struct beeper_seq_s seq = {notes, 2};

    TEST_ASSERT_EQUAL_UINT32(3 * 25 + 50, beeper_seq_duration_ms(&seq));
    TEST_ASSERT_EQUAL_UINT32(4, beeper_seq_note_count(&seq));
    TEST_ASSERT_EQUAL_UINT32(0, beeper_seq_duration_ms(NULL));
}

static void test_the_frequency_range_includes_the_vibrato_excursion(void)
{
    static const struct beeper_seq_note_s plain[] = {
        N(1200, 2800, 60, 0, 50, BEEPER_SEQ_ENV_FLAT, BEEPER_SEQ_FX_NONE, 1)};
    static const struct beeper_seq_note_s wobbly[] = {
        N(2000, 2000, 60, 0, 50, BEEPER_SEQ_ENV_FLAT, BEEPER_SEQ_FX_SIREN, 1)};
    struct beeper_seq_s a = {plain, 1};
    struct beeper_seq_s b = {wobbly, 1};
    uint16_t            lo;
    uint16_t            hi;

    beeper_seq_freq_range(&a, &lo, &hi);
    TEST_ASSERT_EQUAL_UINT16(1200, lo);
    TEST_ASSERT_EQUAL_UINT16(2800, hi);

    /* SIREN is 120 per mille either side of 2000, so the table test that checks
     * the piezo band sees 1760..2240 rather than a tidy 2000. That is the whole
     * point: a note written inside the band can still be swung out of it. */
    beeper_seq_freq_range(&b, &lo, &hi);
    TEST_ASSERT_EQUAL_UINT16(1760, lo);
    TEST_ASSERT_EQUAL_UINT16(2240, hi);

    beeper_seq_freq_range(NULL, &lo, &hi);
    TEST_ASSERT_EQUAL_UINT16(0, lo);
    TEST_ASSERT_EQUAL_UINT16(0, hi);
}

void test_beeper_seq_run(void)
{
    RUN_TEST(test_the_envelope_starts_and_ends_where_it_says);
    RUN_TEST(test_the_pluck_only_ever_falls);
    RUN_TEST(test_the_envelope_survives_its_edges);
    RUN_TEST(test_the_sustain_plateau_is_actually_flat);
    RUN_TEST(test_one_envelope_row_fits_a_tick_and_a_boot_note);

    RUN_TEST(test_a_rising_sweep_walks_from_end_to_end);
    RUN_TEST(test_a_falling_sweep_does_not_wrap);
    RUN_TEST(test_a_glide_is_monotone_and_hits_both_ends);
    RUN_TEST(test_a_glide_lags_the_linear_sweep_in_the_middle);
    RUN_TEST(test_a_glide_with_a_zero_end_falls_back_rather_than_dividing);

    RUN_TEST(test_the_lfo_is_a_triangle_that_starts_at_zero);
    RUN_TEST(test_the_lfo_is_bounded_and_off_at_zero_rate);

    RUN_TEST(test_a_vibrato_stays_within_its_depth_of_the_carrier);
    RUN_TEST(test_the_deepest_vibrato_at_the_top_of_the_band_does_not_overflow);
    RUN_TEST(test_a_vibrato_never_silences_a_sounding_note);
    RUN_TEST(test_a_tremolo_only_ever_goes_down);

    RUN_TEST(test_a_frame_reads_the_note_it_is_inside);
    RUN_TEST(test_a_pause_is_a_rest_and_not_the_end);
    RUN_TEST(test_a_frame_past_the_end_is_zero);
    RUN_TEST(test_a_zero_length_note_is_stepped_over_and_not_divided_by);
    RUN_TEST(test_the_frame_is_pure);
    RUN_TEST(test_walking_a_sequence_terminates_at_its_stated_length);

    RUN_TEST(test_a_repeat_of_zero_and_of_one_both_play_once);
    RUN_TEST(test_a_repeat_restarts_the_envelope_on_every_pass);

    RUN_TEST(test_no_preset_asks_for_a_faster_lfo_than_the_step_can_carry);
    RUN_TEST(test_every_preset_row_is_distinct_and_well_formed);
    RUN_TEST(test_the_shipped_defaults_land_on_the_duty_they_always_did);
    RUN_TEST(test_no_frame_ever_exceeds_the_level_ceiling);

    RUN_TEST(test_duration_counts_pauses_and_repeats);
    RUN_TEST(test_the_frequency_range_includes_the_vibrato_excursion);
}
