/* Unit tests for the tune tables in main/ui/ui_beep_tables_seq.cpp.
 *
 * The same job test_ui_beep_chimes.cpp does for the other engine, and for the
 * same reason: three families and seventeen sounds is fifty-one tunes written
 * out by hand, and every target that can run a test is silent. A tune that is
 * missing, or backwards, or four times too long, is invisible until somebody
 * flashes a panel.
 *
 * Both suites are in one binary on purpose -- a panel ships one engine, but the
 * repository ships both, and the set that is not selected is exactly the one
 * nobody would notice going stale. That is what the two sets of symbol names in
 * ui_beep.hpp are for.
 *
 * The first test here is the boring one and the one that will actually fire: a
 * ui_tune_set_s built from sixteen entries instead of seventeen compiles
 * without a warning and zero-fills the rest, and beeper_play_seq() returns
 * early on exactly that shape -- so the symptom is one gesture in the interface
 * silently making no sound.
 *
 * Two of the constraints differ from the chime suite's, and both follow from
 * there being one voice:
 *
 *   - "nothing below a kilohertz is stacked" has no meaning here, and is gone.
 *
 *   - what replaces it is the vibrato excursion. A note written at the top of
 *     the band with a deep enough effect row swings out of it on its own, and
 *     that failure would otherwise only show up on a panel.
 */

#include <string.h>

#include <unity.h>

#include "control/beeper_seq.h"
#include "test_suites.hpp"
#include "test_ui_beep_policy.hpp"
#include "ui/ui_beep.hpp"

/* A tune longer than this is a melody somebody lost control of. The duration
 * test catches a runaway `repeat` by itself, but this catches one that is long
 * in the table rather than long in a multiplier. */
#define TUNE_NOTES_MAX 12

/* ------------------------------------------------------------ completeness */

static void test_every_family_has_every_sound(void)
{
    /* The one that matters. Sixteen entries where seventeen were meant is a
     * zero-filled {NULL, 0}, which plays nothing at all. */
    for (int f = 0; f < UI_THEME_FAMILY_COUNT; f++)
    {
        for (int s = 0; s < UI_SOUND_COUNT; s++)
        {
            const struct beeper_seq_s *tune = &ui_tune_sets[f]->chime[s];
            const char                *name = where(f, (enum ui_sound_e)s);

            TEST_ASSERT_NOT_NULL_MESSAGE(tune->notes, name);
            TEST_ASSERT_TRUE_MESSAGE(tune->count > 0, name);
            TEST_ASSERT_TRUE_MESSAGE(beeper_seq_duration_ms(tune) > 0, name);
        }
    }
}

static void test_the_families_do_not_share_a_set(void)
{
    /* A copy-paste that left two families pointing at one table would pass
     * every other test in this file. */
    for (int s = 0; s < UI_SOUND_COUNT; s++)
    {
        const struct beeper_seq_note_s *a = ui_tune_default.chime[s].notes;
        const struct beeper_seq_note_s *b = ui_tune_lcars.chime[s].notes;
        const struct beeper_seq_note_s *c = ui_tune_jarvis.chime[s].notes;

        TEST_ASSERT_TRUE(a != b);
        TEST_ASSERT_TRUE(b != c);
        TEST_ASSERT_TRUE(a != c);
    }
}

static void test_every_sound_has_a_name(void)
{
    for (int s = 0; s < UI_SOUND_COUNT; s++)
    {
        TEST_ASSERT_NOT_NULL(ui_sound_names[s]);
        TEST_ASSERT_TRUE(strlen(ui_sound_names[s]) > 0);

        for (int t = s + 1; t < UI_SOUND_COUNT; t++)
            TEST_ASSERT_TRUE(strcmp(ui_sound_names[s], ui_sound_names[t]) != 0);
    }
}

/* -------------------------------------------------------------- the band */

static void test_every_tune_stays_in_the_piezos_band(void)
{
    for (int f = 0; f < UI_THEME_FAMILY_COUNT; f++)
    {
        for (int s = 0; s < UI_SOUND_COUNT; s++)
        {
            uint16_t lo = 0;
            uint16_t hi = 0;

            if (may_go_low((enum ui_sound_e)s) == true)
                continue;

            beeper_seq_freq_range(&ui_tune_sets[f]->chime[s], &lo, &hi);

            TEST_ASSERT_TRUE_MESSAGE(lo >= BEEPER_BAND_LO_HZ, where(f, (enum ui_sound_e)s));
            TEST_ASSERT_TRUE_MESSAGE(hi <= BEEPER_BAND_HI_HZ, where(f, (enum ui_sound_e)s));
        }
    }
}

static void test_an_exempt_sound_really_does_go_low(void)
{
    /* The converse, so the exemption cannot go stale. If no family's error
     * sound dips below the band any more, the exemption is a lie and should be
     * deleted rather than left as a licence nobody is using. */
    bool went_low = false;

    for (int f = 0; f < UI_THEME_FAMILY_COUNT; f++)
    {
        uint16_t lo = 0;
        uint16_t hi = 0;

        beeper_seq_freq_range(&ui_tune_sets[f]->chime[UI_SOUND_ERROR], &lo, &hi);

        if (lo < BEEPER_BAND_LO_HZ)
            went_low = true;
    }

    TEST_ASSERT_TRUE(went_low);
}

static void test_no_vibrato_swings_a_note_out_of_the_band(void)
{
    /* This engine's version of the polyphony floor, and the reason
     * beeper_seq_freq_range() counts the excursion rather than the written
     * frequency. A 3.9 kHz note carrying FX_SIREN reaches 4.37 kHz, which is
     * outside the band even though every number in the table is inside it.
     *
     * The band test above already covers this. It is asserted separately
     * because what it is protecting against is not obvious from that one, and
     * the first person to add a vibrato to a high note should find out why
     * rather than be puzzled. */
    for (int f = 0; f < UI_THEME_FAMILY_COUNT; f++)
    {
        for (int s = 0; s < UI_SOUND_COUNT; s++)
        {
            const struct beeper_seq_s *tune = &ui_tune_sets[f]->chime[s];
            uint16_t                   lo   = 0;
            uint16_t                   hi   = 0;
            uint16_t                   flat_hi = 0;

            if (may_go_low((enum ui_sound_e)s) == true)
                continue;

            beeper_seq_freq_range(tune, &lo, &hi);

            for (uint8_t i = 0; i < tune->count; i++)
            {
                if (tune->notes[i].f_start > flat_hi)
                    flat_hi = tune->notes[i].f_start;

                if (tune->notes[i].f_end > flat_hi)
                    flat_hi = tune->notes[i].f_end;
            }

            /* Whatever the written notes are, what the panel actually emits is
             * what has to fit. */
            TEST_ASSERT_TRUE_MESSAGE(hi >= flat_hi, where(f, (enum ui_sound_e)s));
            TEST_ASSERT_TRUE_MESSAGE(hi <= BEEPER_BAND_HI_HZ, where(f, (enum ui_sound_e)s));
        }
    }
}

/* ----------------------------------------------------------------- length */

static void test_no_tune_outstays_its_gesture(void)
{
    /* Also the test that catches a runaway repeat: the duration is
     * (duration + pause) * repeat summed, so a note struck two hundred times
     * fails here rather than on a panel. */
    for (int f = 0; f < UI_THEME_FAMILY_COUNT; f++)
    {
        for (int s = 0; s < UI_SOUND_COUNT; s++)
        {
            uint32_t ms    = beeper_seq_duration_ms(&ui_tune_sets[f]->chime[s]);
            uint32_t limit = is_feedback((enum ui_sound_e)s) ? FEEDBACK_MAX_MS
                                                             : CHIME_MAX_MS;

            TEST_ASSERT_TRUE_MESSAGE(ms <= limit, where(f, (enum ui_sound_e)s));
        }
    }
}

static void test_no_tune_has_more_notes_than_anybody_meant(void)
{
    for (int f = 0; f < UI_THEME_FAMILY_COUNT; f++)
    {
        for (int s = 0; s < UI_SOUND_COUNT; s++)
        {
            const struct beeper_seq_s *tune = &ui_tune_sets[f]->chime[s];
            const char                *name = where(f, (enum ui_sound_e)s);

            TEST_ASSERT_TRUE_MESSAGE(tune->count <= BEEPER_SEQ_NOTES_MAX, name);
            TEST_ASSERT_TRUE_MESSAGE(beeper_seq_note_count(tune) <= TUNE_NOTES_MAX,
                                     name);
        }
    }
}

static void test_every_note_is_at_least_one_step_long(void)
{
    /* A note shorter than one step gets a single envelope sample, which makes
     * the shape it asked for a lie -- and a duration of zero is a click. */
    for (int f = 0; f < UI_THEME_FAMILY_COUNT; f++)
    {
        for (int s = 0; s < UI_SOUND_COUNT; s++)
        {
            const struct beeper_seq_s *tune = &ui_tune_sets[f]->chime[s];

            for (uint8_t i = 0; i < tune->count; i++)
                TEST_ASSERT_TRUE_MESSAGE(
                    tune->notes[i].duration_ms >= BEEPER_SEQ_STEP_MS,
                    where(f, (enum ui_sound_e)s));
        }
    }
}

/* ----------------------------------------------------- levels and presets */

static void test_every_note_names_a_preset_that_exists(void)
{
    /* The zero-filled-note trap, from the other side: a note left half-written
     * has env 0 and fx 0, which are both real rows, so this cannot catch that
     * one -- what it catches is a table that outlived a preset. */
    for (int f = 0; f < UI_THEME_FAMILY_COUNT; f++)
    {
        for (int s = 0; s < UI_SOUND_COUNT; s++)
        {
            const struct beeper_seq_s *tune = &ui_tune_sets[f]->chime[s];
            const char                *name = where(f, (enum ui_sound_e)s);

            for (uint8_t i = 0; i < tune->count; i++)
            {
                const struct beeper_seq_note_s *n = &tune->notes[i];

                TEST_ASSERT_TRUE_MESSAGE(n->env < BEEPER_SEQ_ENV_COUNT, name);
                TEST_ASSERT_TRUE_MESSAGE(n->fx < BEEPER_SEQ_FX_COUNT, name);
                TEST_ASSERT_TRUE_MESSAGE(n->volume > 0, name);
                TEST_ASSERT_TRUE_MESSAGE(n->volume <= 100, name);
                TEST_ASSERT_TRUE_MESSAGE(n->f_start > 0, name);
                TEST_ASSERT_TRUE_MESSAGE(n->f_end > 0, name);
            }
        }
    }
}

static void test_the_contact_layer_is_quieter_than_the_rest(void)
{
    /* Layer one says "the glass felt you" and nothing else -- see the policy in
     * ui_beep.hpp. A press at the same level as an outcome is the single
     * easiest way to make this panel unpleasant. */
    for (int f = 0; f < UI_THEME_FAMILY_COUNT; f++)
    {
        const struct beeper_seq_s *press  = &ui_tune_sets[f]->chime[UI_SOUND_PRESS];
        const struct beeper_seq_s *accept = &ui_tune_sets[f]->chime[UI_SOUND_ACCEPT];

        TEST_ASSERT_TRUE_MESSAGE(press->notes[0].volume < accept->notes[0].volume,
                                 where(f, UI_SOUND_PRESS));
    }
}

static void test_every_preset_is_used_by_somebody(void)
{
    /* The converse of "every note names a preset that exists", and the reason
     * to have both: a row nobody names is dead flash and a word in the
     * vocabulary that means nothing. It should be deleted rather than left as
     * an option nobody took -- or, better, a sound that wanted it should be
     * found. CLICK was unused until this test asked, and the two warnings
     * turned out to want exactly what it is.
     *
     * FX_NONE is exempt: it is what a note says when it says nothing. ENV_FLAT
     * is not, because "no envelope at all" is a real editorial choice and the
     * alert sounds make it. */
    bool env_used[BEEPER_SEQ_ENV_COUNT] = {false};
    bool fx_used[BEEPER_SEQ_FX_COUNT]   = {false};

    fx_used[BEEPER_SEQ_FX_NONE] = true;

    for (int f = 0; f < UI_THEME_FAMILY_COUNT; f++)
    {
        for (int s = 0; s < UI_SOUND_COUNT; s++)
        {
            const struct beeper_seq_s *tune = &ui_tune_sets[f]->chime[s];

            for (uint8_t i = 0; i < tune->count; i++)
            {
                if (tune->notes[i].env < BEEPER_SEQ_ENV_COUNT)
                    env_used[tune->notes[i].env] = true;

                if (tune->notes[i].fx < BEEPER_SEQ_FX_COUNT)
                    fx_used[tune->notes[i].fx] = true;
            }
        }
    }

    for (uint8_t i = 0; i < BEEPER_SEQ_ENV_COUNT; i++)
        TEST_ASSERT_TRUE_MESSAGE(env_used[i], "an envelope preset no tune names");

    for (uint8_t i = 0; i < BEEPER_SEQ_FX_COUNT; i++)
        TEST_ASSERT_TRUE_MESSAGE(fx_used[i], "an effect preset no tune names");
}

static void test_each_family_sounds_like_itself(void)
{
    /* Three families exist so that a panel can be told apart from another
     * panel, and with one voice the only thing left to tell them apart *with*
     * is the vocabulary each draws on. So: no two families may agree about
     * every sound's envelope, which is what would happen if somebody
     * transcribed one family over another.
     *
     * Deliberately weak -- it is a smoke test for a copy-paste, not an
     * aesthetic judgement, and nothing here can make an aesthetic judgement. */
    for (int a = 0; a < UI_THEME_FAMILY_COUNT; a++)
    {
        for (int b = a + 1; b < UI_THEME_FAMILY_COUNT; b++)
        {
            bool differs = false;

            for (int s = 0; s < UI_SOUND_COUNT && differs == false; s++)
            {
                const struct beeper_seq_s *x = &ui_tune_sets[a]->chime[s];
                const struct beeper_seq_s *y = &ui_tune_sets[b]->chime[s];

                if (x->count != y->count)
                    differs = true;
                else
                    for (uint8_t i = 0; i < x->count; i++)
                        if (x->notes[i].env != y->notes[i].env ||
                            x->notes[i].fx != y->notes[i].fx)
                            differs = true;
            }

            TEST_ASSERT_TRUE_MESSAGE(differs, "two families share a vocabulary");
        }
    }
}

void test_ui_beep_tunes_run(void)
{
    RUN_TEST(test_every_family_has_every_sound);
    RUN_TEST(test_the_families_do_not_share_a_set);
    RUN_TEST(test_every_sound_has_a_name);

    RUN_TEST(test_every_tune_stays_in_the_piezos_band);
    RUN_TEST(test_an_exempt_sound_really_does_go_low);
    RUN_TEST(test_no_vibrato_swings_a_note_out_of_the_band);

    RUN_TEST(test_no_tune_outstays_its_gesture);
    RUN_TEST(test_no_tune_has_more_notes_than_anybody_meant);
    RUN_TEST(test_every_note_is_at_least_one_step_long);

    RUN_TEST(test_every_note_names_a_preset_that_exists);
    RUN_TEST(test_the_contact_layer_is_quieter_than_the_rest);
    RUN_TEST(test_every_preset_is_used_by_somebody);
    RUN_TEST(test_each_family_sounds_like_itself);
}
