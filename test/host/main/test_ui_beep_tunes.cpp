/* Unit tests for the tune tables in main/ui/ui_beep_tables_seq.cpp.
 *
 * The same job test_ui_beep_chimes.cpp does for the other engine, and for the
 * same reason: three families and eighteen sounds is fifty-four tunes written
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
 * ui_tune_set_s built from seventeen entries instead of eighteen compiles
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
 *
 * The demonstration tune in control/beeper_song.c is here too, at the foot.
 * It is a tune table that only this engine has, this is the suite compiled with
 * this engine selected, and it is held to every rule the families are held to
 * except the two that are about being a *chime* -- its whole purpose is to be
 * half a minute long.
 */

#include <string.h>

#include <unity.h>

#include "control/beeper_seq.h"
#include "control/beeper_song.h"
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
    /* The one that matters. Seventeen entries where eighteen were meant is a
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
     * every other test in this file.
     *
     * Walked off ui_tune_sets[] rather than off the four objects by name: with
     * four families the named version was three comparisons short of every
     * pair, and it would have gone on compiling. */
    TEST_ASSERT_EQUAL_PTR(&ui_tune_material, ui_tune_sets[UI_THEME_MATERIAL]);
    TEST_ASSERT_EQUAL_PTR(&ui_tune_lcars, ui_tune_sets[UI_THEME_LCARS]);
    TEST_ASSERT_EQUAL_PTR(&ui_tune_jarvis, ui_tune_sets[UI_THEME_JARVIS]);
    TEST_ASSERT_EQUAL_PTR(&ui_tune_classic, ui_tune_sets[UI_THEME_CLASSIC]);

    for (int s = 0; s < UI_SOUND_COUNT; s++)
        for (int a = 0; a < UI_THEME_FAMILY_COUNT; a++)
            for (int b = a + 1; b < UI_THEME_FAMILY_COUNT; b++)
                TEST_ASSERT_TRUE_MESSAGE(
                    ui_tune_sets[a]->chime[s].notes != ui_tune_sets[b]->chime[s].notes,
                    where(a, (enum ui_sound_e)s));
}

/* The counterpart in the chime suite is test_the_door_chime_is_its_own_sound(),
 * and the reason for both is the same: the door chime has no call site, so it
 * is the one sound nobody can find by using the panel and therefore the one a
 * table could quietly leave as a copy of its neighbour. */
static void test_the_door_chime_is_its_own_tune(void)
{
    for (int f = 0; f < UI_THEME_FAMILY_COUNT; f++)
    {
        const struct beeper_seq_s *door = &ui_tune_sets[f]->chime[UI_SOUND_DOOR_CHIME];
        const char                *name = where(f, UI_SOUND_DOOR_CHIME);

        for (int s = 0; s < UI_SOUND_COUNT; s++)
        {
            if (s == UI_SOUND_DOOR_CHIME)
                continue;

            TEST_ASSERT_TRUE_MESSAGE(door->notes != ui_tune_sets[f]->chime[s].notes,
                                     name);
        }
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

static void test_no_note_carries_an_lfo_slower_than_itself(void)
{
    /* An LFO slower than the note carrying it is not an ornament, it is a
     * pitch bend or a fade: the note ends partway up the first rise and never
     * comes back down. That is a different sound from the one the preset's name
     * promises, and it is invisible in the table -- the note looks like it has
     * a vibrato and the effect row looks like a vibrato.
     *
     * It caught three rows the first time these tables were walked. SHIMMER was
     * 5.5 Hz, a lovely violin vibrato, getting a third of a cycle into a
     * sixty-millisecond arpeggio note; SIREN was 2 Hz against a note an eighth
     * of that period long. Both are faster now, and this is what keeps them
     * honest against a table that shortens a note later.
     *
     * One period, not two. Two would be the better sound and would rule out
     * half the places these are used; one is the floor below which the name on
     * the row is simply false. */
    for (int f = 0; f < UI_THEME_FAMILY_COUNT; f++)
    {
        for (int s = 0; s < UI_SOUND_COUNT; s++)
        {
            const struct beeper_seq_s *tune = &ui_tune_sets[f]->chime[s];

            for (uint8_t i = 0; i < tune->count; i++)
            {
                const struct beeper_seq_note_s *n  = &tune->notes[i];
                const struct beeper_seq_fx_s   *fx = beeper_seq_fx_preset(n->fx);
                uint16_t                        rate;

                rate = (fx->vib_rate_chz > fx->trem_rate_chz) ? fx->vib_rate_chz
                                                              : fx->trem_rate_chz;

                if (rate == 0)
                    continue;

                /* One period in milliseconds is 100000 / rate_chz. */
                TEST_ASSERT_TRUE_MESSAGE(
                    (uint32_t)n->duration_ms * rate >= 100000u,
                    where(f, (enum ui_sound_e)s));
            }
        }
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

/* ------------------------------------------------- the demonstration tune */

/* The piece is thirty seconds, and its own movement headings say how that is
 * spent. Nothing else in the firmware would notice if a later edit made it
 * twenty or fifty -- there is no ceiling on it the way there is on a chime, the
 * settings button would still work, and the comments would simply be wrong.
 *
 * Two seconds of slack either side, so this is a guard against a movement being
 * doubled or dropped rather than a re-statement of the table. */
#define SONG_TARGET_MS 30000
#define SONG_SLACK_MS  2000

static void test_the_demo_song_is_about_half_a_minute(void)
{
    uint32_t ms = beeper_seq_duration_ms(beeper_song());

    TEST_ASSERT_TRUE_MESSAGE(ms >= SONG_TARGET_MS - SONG_SLACK_MS, "demo song too short");
    TEST_ASSERT_TRUE_MESSAGE(ms <= SONG_TARGET_MS + SONG_SLACK_MS, "demo song too long");
}

static void test_the_demo_song_fits_one_queue_item(void)
{
    /* `count` is a uint8_t and beeper_play_seq() sends the whole tune by value,
     * so a piece that grew past 255 notes would not be truncated at the queue
     * -- it would be silently miscounted where the table is declared, and play
     * whatever the low eight bits came to. */
    size_t notes = (size_t)beeper_song()->count;

    TEST_ASSERT_TRUE_MESSAGE(notes > 0, "demo song is empty");
    TEST_ASSERT_TRUE_MESSAGE(notes <= 255, "demo song will not fit a uint8_t count");
    TEST_ASSERT_NOT_NULL(beeper_song()->notes);
}

static void test_the_demo_song_stays_in_the_piezos_band(void)
{
    /* Including what the vibratos swing it to, which is the whole reason this
     * uses beeper_seq_freq_range() rather than reading the table: the finale
     * runs up to B7, and B7 with a SHIMMER on it would be 2 Hz inside the
     * ceiling while B7 with a SIREN would be 400 Hz outside it. */
    uint16_t lo = 0;
    uint16_t hi = 0;

    beeper_seq_freq_range(beeper_song(), &lo, &hi);

    TEST_ASSERT_TRUE_MESSAGE(lo >= BEEPER_BAND_LO_HZ, "demo song goes below the band");
    TEST_ASSERT_TRUE_MESSAGE(hi <= BEEPER_BAND_HI_HZ, "demo song goes above the band");
}

static void test_no_demo_note_carries_an_lfo_slower_than_itself(void)
{
    /* The same rule the families are held to, and the one the piece is most
     * likely to break: the ornaments movement exists to let each effect be
     * heard, and an effect that does not complete a cycle inside its note is
     * heard as a bend instead -- which would make that movement demonstrate
     * the opposite of what it is labelled. */
    const struct beeper_seq_s *song = beeper_song();

    for (uint8_t i = 0; i < song->count; i++)
    {
        const struct beeper_seq_note_s *n  = &song->notes[i];
        const struct beeper_seq_fx_s   *fx = beeper_seq_fx_preset(n->fx);
        uint16_t                        rate;

        TEST_ASSERT_TRUE_MESSAGE(n->env < BEEPER_SEQ_ENV_COUNT, "demo song: bad envelope");
        TEST_ASSERT_TRUE_MESSAGE(n->fx < BEEPER_SEQ_FX_COUNT, "demo song: bad effect");
        TEST_ASSERT_TRUE_MESSAGE(n->volume > 0 && n->volume <= 100, "demo song: bad volume");
        TEST_ASSERT_TRUE_MESSAGE(n->duration_ms >= BEEPER_SEQ_STEP_MS,
                                 "demo song: note shorter than a step");

        rate = (fx->vib_rate_chz > fx->trem_rate_chz) ? fx->vib_rate_chz
                                                      : fx->trem_rate_chz;

        if (rate == 0)
            continue;

        TEST_ASSERT_TRUE_MESSAGE((uint32_t)n->duration_ms * rate >= 100000u,
                                 "demo song: an LFO slower than the note carrying it");
    }
}

static void test_the_demo_song_plays_every_preset(void)
{
    /* The point of the piece. The themed families between them leave rows
     * unheard -- that is what the "every preset is used by somebody" test above
     * is really saying, since one family using a row once is enough for it --
     * and this is the one table that is supposed to name all sixteen.
     *
     * So this is a stronger assertion than that one, and it is the assertion
     * that would fire if somebody added a ninth envelope and did not put it in
     * the demonstration, which is exactly the moment to be told. */
    const struct beeper_seq_s *song = beeper_song();

    bool env_used[BEEPER_SEQ_ENV_COUNT] = {false};
    bool fx_used[BEEPER_SEQ_FX_COUNT]   = {false};

    for (uint8_t i = 0; i < song->count; i++)
    {
        if (song->notes[i].env < BEEPER_SEQ_ENV_COUNT)
            env_used[song->notes[i].env] = true;

        if (song->notes[i].fx < BEEPER_SEQ_FX_COUNT)
            fx_used[song->notes[i].fx] = true;
    }

    for (uint8_t i = 0; i < BEEPER_SEQ_ENV_COUNT; i++)
        TEST_ASSERT_TRUE_MESSAGE(env_used[i], "the demo song never plays some envelope");

    for (uint8_t i = 0; i < BEEPER_SEQ_FX_COUNT; i++)
        TEST_ASSERT_TRUE_MESSAGE(fx_used[i], "the demo song never plays some effect");
}

static void test_the_demo_song_uses_repeat(void)
{
    /* `repeat` is a field of the note format that no themed family needs more
     * than twice, and the trills movement is where it is actually shown off. A
     * piece with every repeat flattened to 1 would still pass everything above
     * and would have stopped demonstrating one of the six things in the struct. */
    const struct beeper_seq_s *song = beeper_song();
    bool                       repeated = false;

    for (uint8_t i = 0; i < song->count; i++)
        if (song->notes[i].repeat > 1)
            repeated = true;

    TEST_ASSERT_TRUE_MESSAGE(repeated, "the demo song never repeats a note");

    /* And the struck count has to exceed the written one, which is the same
     * claim from the other side: it says the engine really does expand them. */
    TEST_ASSERT_TRUE(beeper_seq_note_count(song) > song->count);
}

void test_ui_beep_tunes_run(void)
{
    RUN_TEST(test_every_family_has_every_sound);
    RUN_TEST(test_the_families_do_not_share_a_set);
    RUN_TEST(test_every_sound_has_a_name);
    RUN_TEST(test_the_door_chime_is_its_own_tune);

    RUN_TEST(test_every_tune_stays_in_the_piezos_band);
    RUN_TEST(test_an_exempt_sound_really_does_go_low);
    RUN_TEST(test_no_vibrato_swings_a_note_out_of_the_band);

    RUN_TEST(test_no_tune_outstays_its_gesture);
    RUN_TEST(test_no_tune_has_more_notes_than_anybody_meant);
    RUN_TEST(test_every_note_is_at_least_one_step_long);

    RUN_TEST(test_every_note_names_a_preset_that_exists);
    RUN_TEST(test_the_contact_layer_is_quieter_than_the_rest);
    RUN_TEST(test_no_note_carries_an_lfo_slower_than_itself);
    RUN_TEST(test_every_preset_is_used_by_somebody);
    RUN_TEST(test_each_family_sounds_like_itself);

    RUN_TEST(test_the_demo_song_is_about_half_a_minute);
    RUN_TEST(test_the_demo_song_fits_one_queue_item);
    RUN_TEST(test_the_demo_song_stays_in_the_piezos_band);
    RUN_TEST(test_no_demo_note_carries_an_lfo_slower_than_itself);
    RUN_TEST(test_the_demo_song_plays_every_preset);
    RUN_TEST(test_the_demo_song_uses_repeat);
}
