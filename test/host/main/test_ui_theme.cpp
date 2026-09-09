/* Unit tests for the theme name lookups in ui_theme.hpp.
 *
 * These four functions are the only funnel between a theme's name and its
 * enum, and four unrelated callers pass through them: the config file, the web
 * form's POST, the simulator's environment variables, and the compiled-in
 * defaults. Every one of them can supply a name that does not exist -- a typo
 * in a hand-edited config.json, a name written by newer firmware, or the NULL
 * that getenv() returns for an unset variable -- and none of them checks the
 * result. So the fallback is load-bearing, and these tests pin it down.
 *
 * Host-only; ui_theme.hpp is header-only and depends on nothing, so nothing
 * from main/ needs to be linked. Run with:
 *   cd test/host && idf.py build && ./build/oh-ez-touch-host-test.elf
 */

#include <unity.h>

#include "ui/ui_theme.hpp"
#include "test_suites.hpp"

static void test_every_theme_name_round_trips(void)
{
    for (int i = 0; i < UI_THEME_FAMILY_COUNT; i++)
    {
        enum ui_theme_family_e family = (enum ui_theme_family_e)i;

        TEST_ASSERT_EQUAL_INT(family, ui_theme_from_name(ui_theme_name(family)));
    }
}

static void test_every_night_mode_name_round_trips(void)
{
    for (int i = 0; i < UI_NIGHT_MODE_COUNT; i++)
    {
        enum ui_night_mode_e mode = (enum ui_night_mode_e)i;

        TEST_ASSERT_EQUAL_INT(mode, ui_night_mode_from_name(ui_night_mode_name(mode)));
    }
}

/* The web form submits a select element's option text, which webui.cpp looks
 * up with strcasecmp(), so the config file has to accept the same spellings
 * the form does. */
static void test_name_lookup_ignores_case(void)
{
    TEST_ASSERT_EQUAL_INT(UI_THEME_LCARS, ui_theme_from_name("lcars"));
    TEST_ASSERT_EQUAL_INT(UI_THEME_LCARS, ui_theme_from_name("LcArS"));
    TEST_ASSERT_EQUAL_INT(UI_THEME_JARVIS, ui_theme_from_name("jarvis"));
    TEST_ASSERT_EQUAL_INT(UI_THEME_DEFAULT, ui_theme_from_name("DEFAULT"));

    TEST_ASSERT_EQUAL_INT(UI_NIGHT_AUTO, ui_night_mode_from_name("AUTO"));
    TEST_ASSERT_EQUAL_INT(UI_NIGHT_ON, ui_night_mode_from_name("On"));
}

static void test_unknown_names_fall_back_to_the_first_entry(void)
{
    TEST_ASSERT_EQUAL_INT(UI_THEME_DEFAULT, ui_theme_from_name("Klingon"));
    TEST_ASSERT_EQUAL_INT(UI_THEME_DEFAULT, ui_theme_from_name(""));
    TEST_ASSERT_EQUAL_INT(UI_THEME_DEFAULT, ui_theme_from_name("LCARS2"));
    /* A prefix must not match either: strcasecmp(), not strncasecmp(). */
    TEST_ASSERT_EQUAL_INT(UI_THEME_DEFAULT, ui_theme_from_name("LCAR"));

    TEST_ASSERT_EQUAL_INT(UI_NIGHT_OFF, ui_night_mode_from_name("maybe"));
    TEST_ASSERT_EQUAL_INT(UI_NIGHT_OFF, ui_night_mode_from_name(""));
}

/* getenv() returns NULL for an unset variable and the simulator passes that
 * straight in, so NULL has to mean "the default" rather than crash. */
static void test_null_name_falls_back_to_the_first_entry(void)
{
    TEST_ASSERT_EQUAL_INT(UI_THEME_DEFAULT, ui_theme_from_name(NULL));
    TEST_ASSERT_EQUAL_INT(UI_NIGHT_OFF, ui_night_mode_from_name(NULL));
}

/* Config is a global, so a loadConfig() that returns early leaves item
 * zero-initialised. That has to name a valid theme, which it only does while
 * the two enums start at their default. */
static void test_zero_is_the_default_variant(void)
{
    TEST_ASSERT_EQUAL_INT(0, UI_THEME_DEFAULT);
    TEST_ASSERT_EQUAL_INT(0, UI_NIGHT_OFF);
}

/* An out-of-range enum -- a value cast in from a stale config or a newer
 * firmware -- must still name something printable rather than read off the end
 * of the table. */
static void test_out_of_range_ids_name_the_default(void)
{
    TEST_ASSERT_EQUAL_STRING(UI_THEME_NAME_DEFAULT,
                             ui_theme_name((enum ui_theme_family_e)UI_THEME_FAMILY_COUNT));
    TEST_ASSERT_EQUAL_STRING(UI_THEME_NAME_DEFAULT,
                             ui_theme_name((enum ui_theme_family_e)99));
    TEST_ASSERT_EQUAL_STRING(UI_NIGHT_NAME_OFF,
                             ui_night_mode_name((enum ui_night_mode_e)UI_NIGHT_MODE_COUNT));
}

void test_ui_theme_run(void)
{
    /* Unity records the file from the UNITY_BEGIN() call site, which is the
     * runner, while RUN_TEST records the line from here -- so without this a
     * failure would be reported against the wrong file. */
    Unity.TestFile = __FILE__;

    RUN_TEST(test_every_theme_name_round_trips);
    RUN_TEST(test_every_night_mode_name_round_trips);
    RUN_TEST(test_name_lookup_ignores_case);
    RUN_TEST(test_unknown_names_fall_back_to_the_first_entry);
    RUN_TEST(test_null_name_falls_back_to_the_first_entry);
    RUN_TEST(test_zero_is_the_default_variant);
    RUN_TEST(test_out_of_range_ids_name_the_default);
}
