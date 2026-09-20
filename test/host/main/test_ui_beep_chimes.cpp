/* Unit tests for the chime tables in main/ui/ui_beep_tables.cpp.
 *
 * There are three families and eighteen sounds, which is fifty-four chimes
 * written out by hand -- and every target that can run a test is silent. The
 * host has no buzzer, the Lanbon has no buzzer, and the ArduiTouch that does
 * has never had this firmware on it. So a chime that is missing, or backwards,
 * or four times too long, is invisible until somebody flashes a panel.
 *
 * The first test here is the boring one, and it is the one that will actually
 * fire. A ui_chime_set_s built from seventeen CHIME() entries instead of eighteen
 * compiles without a warning and zero-fills the rest -- and beeper_play()
 * returns early on exactly that shape, so the symptom is one gesture in the
 * interface silently making no sound. (The X-macro in ui_beep.hpp is meant to
 * make that impossible; this is what checks that it did.)
 *
 * The rest are the constraints that are real but that nobody has in mind while
 * writing a table of frequencies: a small piezo is loud between about one and
 * four kilohertz and quiet outside it, a chime that outlasts the gesture it
 * answers reads as lag rather than as feedback, and -- the one that is
 * genuinely easy to get wrong -- a voice cannot be stacked below about a
 * kilohertz, because a two-millisecond slot down there is less than two cycles
 * and the ear hears the slot rate instead of the note.
 *
 * It links ui_beep_tables.cpp, which is why that file exists separately from
 * ui_beep.cpp: the tables need nothing but stdint, while ui_beep_play() needs
 * ui_style.hpp and therefore <lvgl.h>.
 *
 * The constraints themselves are in test_ui_beep_policy.hpp, shared with the
 * other engine's suite: which gestures may be unpleasant and how long each kind
 * may last are decisions about the interface, not about the arithmetic.
 */

#include <string.h>

#include <unity.h>

#include "control/beeper_mixer.h"
#include "test_suites.hpp"
#include "test_ui_beep_policy.hpp"
#include "ui/ui_beep.hpp"

static void test_every_family_has_every_sound(void)
{
    for (int f = 0; f < UI_THEME_FAMILY_COUNT; f++)
    {
        const struct ui_chime_set_s *set = ui_chime_sets[f];

        TEST_ASSERT_NOT_NULL(set);

        for (int s = 0; s < UI_SOUND_COUNT; s++)
        {
            const struct beeper_chime_s *chime = &set->chime[s];
            const char                  *name  = where(f, (enum ui_sound_e)s);

            TEST_ASSERT_NOT_NULL_MESSAGE(chime->voices, name);
            TEST_ASSERT_NOT_EQUAL_MESSAGE(0, chime->count, name);
            TEST_ASSERT_NOT_EQUAL_MESSAGE(0, beeper_chime_duration_ms(chime), name);
        }
    }
}

/* The only thing that can catch a copy-paste in a table of three near
 * identical lines. */
static void test_the_families_do_not_share_a_set(void)
{
    TEST_ASSERT_EQUAL_PTR(&ui_chime_material, ui_chime_sets[UI_THEME_MATERIAL]);
    TEST_ASSERT_EQUAL_PTR(&ui_chime_lcars, ui_chime_sets[UI_THEME_LCARS]);
    TEST_ASSERT_EQUAL_PTR(&ui_chime_jarvis, ui_chime_sets[UI_THEME_JARVIS]);
    TEST_ASSERT_EQUAL_PTR(&ui_chime_classic, ui_chime_sets[UI_THEME_CLASSIC]);

    /* Every pair, rather than the neighbours: with four families the chain of
     * three comparisons stopped covering all six. */
    for (int a = 0; a < UI_THEME_FAMILY_COUNT; a++)
        for (int b = a + 1; b < UI_THEME_FAMILY_COUNT; b++)
            TEST_ASSERT_TRUE(ui_chime_sets[a] != ui_chime_sets[b]);
}

static void test_the_sound_names_are_present_and_distinct(void)
{
    for (int s = 0; s < UI_SOUND_COUNT; s++)
    {
        TEST_ASSERT_NOT_NULL(ui_sound_names[s]);
        TEST_ASSERT_TRUE(ui_sound_names[s][0] != '\0');

        for (const char *c = ui_sound_names[s]; *c != '\0'; c++)
            TEST_ASSERT_TRUE((*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9') ||
                             *c == '_');

        for (int other = 0; other < s; other++)
            TEST_ASSERT_TRUE(strcmp(ui_sound_names[s], ui_sound_names[other]) != 0);
    }
}

/* The names are also a wire format now: `sound/set` carries one as its payload
 * and ui_sound_from_name() turns it back into a sound -- see ui_beep.hpp. Both
 * ends of that are worth pinning down, because the failure is a log line on a
 * panel nobody is watching rather than anything visible here.
 *
 * In this suite rather than the tune one because the character-set test above
 * is the other half of the same question, and the lookup is engine-blind. */
static void test_every_name_is_accepted_back(void)
{
    for (int s = 0; s < UI_SOUND_COUNT; s++)
        TEST_ASSERT_EQUAL_MESSAGE(s, (int)ui_sound_from_name(ui_sound_names[s]),
                                  ui_sound_names[s]);

    /* Typed by a person into a broker rule, so the case it arrives in is not
     * something to have an opinion about. */
    TEST_ASSERT_EQUAL(UI_SOUND_DOOR_CHIME, ui_sound_from_name("DOOR_CHIME"));
    TEST_ASSERT_EQUAL(UI_SOUND_ACCEPT, ui_sound_from_name("Accept"));

    /* And everything else is refused rather than silently landing on sound
     * zero, which is the contact tick and would be the quietest possible way
     * to get this wrong. */
    TEST_ASSERT_EQUAL(UI_SOUND_COUNT, ui_sound_from_name(""));
    TEST_ASSERT_EQUAL(UI_SOUND_COUNT, ui_sound_from_name("door chime"));
    TEST_ASSERT_EQUAL(UI_SOUND_COUNT, ui_sound_from_name("door_chimes"));
    TEST_ASSERT_EQUAL(UI_SOUND_COUNT, ui_sound_from_name(NULL));
}

/* The door chime has no call site, by design -- an installation knows somebody
 * is at the door and a touchscreen does not, so it is reachable over MQTT and
 * nowhere else. That makes it the one sound in the vocabulary a person cannot
 * find by using the panel, and therefore the one a table could quietly leave
 * as a copy of its neighbour without anybody noticing.
 *
 * The completeness test above already refuses an empty entry. This refuses a
 * lazy one. */
static void test_the_door_chime_is_its_own_sound(void)
{
    for (int f = 0; f < UI_THEME_FAMILY_COUNT; f++)
    {
        const struct beeper_chime_s *door = &ui_chime_sets[f]->chime[UI_SOUND_DOOR_CHIME];
        const char                  *name = where(f, UI_SOUND_DOOR_CHIME);

        for (int s = 0; s < UI_SOUND_COUNT; s++)
        {
            if (s == UI_SOUND_DOOR_CHIME)
                continue;

            TEST_ASSERT_TRUE_MESSAGE(door->voices != ui_chime_sets[f]->chime[s].voices,
                                     name);
        }
    }
}

static void test_frequencies_stay_inside_the_piezo_band(void)
{
    for (int f = 0; f < UI_THEME_FAMILY_COUNT; f++)
    {
        for (int s = 0; s < UI_SOUND_COUNT; s++)
        {
            if (may_go_low((enum ui_sound_e)s) == true)
                continue;

            uint16_t lo = 0;
            uint16_t hi = 0;

            beeper_chime_freq_range(&ui_chime_sets[f]->chime[s], &lo, &hi);

            TEST_ASSERT_TRUE_MESSAGE(lo >= BEEPER_BAND_LO_HZ, where(f, (enum ui_sound_e)s));
            TEST_ASSERT_TRUE_MESSAGE(hi <= BEEPER_BAND_HI_HZ, where(f, (enum ui_sound_e)s));
        }
    }
}

/* The converse, so the exemption above cannot go stale. If no family's error
 * sound dips below the band any more, the exemption is a lie and should be
 * deleted rather than left as a licence nobody is using. */
static void test_an_exempt_sound_really_does_go_low(void)
{
    bool any = false;

    for (int f = 0; f < UI_THEME_FAMILY_COUNT; f++)
    {
        uint16_t lo = 0;
        uint16_t hi = 0;

        beeper_chime_freq_range(&ui_chime_sets[f]->chime[UI_SOUND_ERROR], &lo, &hi);

        if (lo < BEEPER_BAND_LO_HZ)
            any = true;
    }

    TEST_ASSERT_TRUE_MESSAGE(any, "no error sound uses its low-frequency exemption");
}

/* Rule 1 out of beeper_mixer.h, and the only thing that makes it a rule rather
 * than a comment: a slot is two milliseconds, which is four cycles at 2 kHz
 * and fewer than two below one. Stack a voice down there and the chord is
 * heard as the slot rate. */
static void test_nothing_below_the_polyphony_floor_is_stacked(void)
{
    for (int f = 0; f < UI_THEME_FAMILY_COUNT; f++)
    {
        for (int s = 0; s < UI_SOUND_COUNT; s++)
        {
            const struct beeper_chime_s *chime = &ui_chime_sets[f]->chime[s];

            if (beeper_chime_peak_voices(chime) < 2)
                continue;

            uint16_t lo = 0;
            uint16_t hi = 0;

            beeper_chime_freq_range(chime, &lo, &hi);

            TEST_ASSERT_TRUE_MESSAGE(lo >= BEEPER_POLY_MIN_HZ,
                                     where(f, (enum ui_sound_e)s));
        }
    }
}

static void test_no_chime_outstays_its_gesture(void)
{
    for (int f = 0; f < UI_THEME_FAMILY_COUNT; f++)
    {
        for (int s = 0; s < UI_SOUND_COUNT; s++)
        {
            uint32_t ms    = beeper_chime_duration_ms(&ui_chime_sets[f]->chime[s]);
            uint32_t limit = is_feedback((enum ui_sound_e)s) ? FEEDBACK_MAX_MS
                                                             : CHIME_MAX_MS;

            TEST_ASSERT_TRUE_MESSAGE(ms <= limit, where(f, (enum ui_sound_e)s));
        }
    }
}

static void test_no_chime_asks_for_more_voices_than_the_mixer_has(void)
{
    for (int f = 0; f < UI_THEME_FAMILY_COUNT; f++)
    {
        for (int s = 0; s < UI_SOUND_COUNT; s++)
        {
            uint8_t peak = beeper_chime_peak_voices(&ui_chime_sets[f]->chime[s]);
            const char *name = where(f, (enum ui_sound_e)s);

            TEST_ASSERT_TRUE_MESSAGE(peak >= 1, name);
            TEST_ASSERT_TRUE_MESSAGE(peak <= BEEPER_VOICES_MAX, name);
        }
    }
}

/* A note shorter than one step gets a single envelope sample, which makes the
 * shape it asked for a lie -- and a duration of zero is a click. */
static void test_every_note_is_at_least_one_step_long(void)
{
    for (int f = 0; f < UI_THEME_FAMILY_COUNT; f++)
    {
        for (int s = 0; s < UI_SOUND_COUNT; s++)
        {
            const struct beeper_chime_s *chime = &ui_chime_sets[f]->chime[s];
            const char                  *name  = where(f, (enum ui_sound_e)s);

            for (uint8_t v = 0; v < chime->count; v++)
            {
                const struct beeper_voice_s *voice = &chime->voices[v];

                TEST_ASSERT_NOT_NULL_MESSAGE(voice->notes, name);
                TEST_ASSERT_NOT_EQUAL_MESSAGE(0, voice->count, name);

                for (uint8_t n = 0; n < voice->count; n++)
                    TEST_ASSERT_TRUE_MESSAGE(
                        voice->notes[n].duration_ms >= BEEPER_STEP_MS, name);
            }
        }
    }
}

static void test_every_note_asks_for_a_level_and_a_shape_that_exist(void)
{
    for (int f = 0; f < UI_THEME_FAMILY_COUNT; f++)
    {
        for (int s = 0; s < UI_SOUND_COUNT; s++)
        {
            const struct beeper_chime_s *chime = &ui_chime_sets[f]->chime[s];
            const char                  *name  = where(f, (enum ui_sound_e)s);

            for (uint8_t v = 0; v < chime->count; v++)
            {
                const struct beeper_voice_s *voice = &chime->voices[v];

                for (uint8_t n = 0; n < voice->count; n++)
                {
                    TEST_ASSERT_TRUE_MESSAGE(voice->notes[n].volume > 0, name);
                    TEST_ASSERT_TRUE_MESSAGE(voice->notes[n].volume <= 100, name);
                    TEST_ASSERT_TRUE_MESSAGE(voice->notes[n].shape < BEEPER_SHAPE_COUNT,
                                             name);
                }
            }
        }
    }
}

/* The contact layer has to sit under the outcome layer or the two stop reading
 * as one gesture -- see the policy in ui_beep.hpp. This is the only place that
 * relationship is written down as a number. */
static void test_the_contact_layer_is_quieter_than_the_rest(void)
{
    for (int f = 0; f < UI_THEME_FAMILY_COUNT; f++)
    {
        const struct beeper_chime_s *press  = &ui_chime_sets[f]->chime[UI_SOUND_PRESS];
        const struct beeper_chime_s *change = &ui_chime_sets[f]->chime[UI_SOUND_CHANGE];

        uint8_t quiet = press->voices[0].notes[0].volume;
        uint8_t loud  = change->voices[0].notes[0].volume;

        TEST_ASSERT_TRUE_MESSAGE(quiet * 2 < loud, where(f, UI_SOUND_PRESS));
    }
}

void test_ui_beep_chimes_run(void)
{
    RUN_TEST(test_every_family_has_every_sound);
    RUN_TEST(test_the_families_do_not_share_a_set);
    RUN_TEST(test_the_sound_names_are_present_and_distinct);
    RUN_TEST(test_every_name_is_accepted_back);
    RUN_TEST(test_the_door_chime_is_its_own_sound);
    RUN_TEST(test_frequencies_stay_inside_the_piezo_band);
    RUN_TEST(test_an_exempt_sound_really_does_go_low);
    RUN_TEST(test_nothing_below_the_polyphony_floor_is_stacked);
    RUN_TEST(test_no_chime_outstays_its_gesture);
    RUN_TEST(test_no_chime_asks_for_more_voices_than_the_mixer_has);
    RUN_TEST(test_every_note_is_at_least_one_step_long);
    RUN_TEST(test_every_note_asks_for_a_level_and_a_shape_that_exist);
    RUN_TEST(test_the_contact_layer_is_quieter_than_the_rest);
}
