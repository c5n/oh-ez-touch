/* Unit tests for the TZ string the NTP settings build.
 *
 * The DST flag used to add a fixed hour, which on a CET panel was right from
 * March to October and wrong the rest of the year, and the only days the
 * difference shows are two Sundays a year that nobody can reproduce by
 * waiting. The flag carries the EU rule now, and the rule is data: a string
 * whose every character is decided by two settings, and whose mistakes are
 * invisible except on the day they fire.
 *
 * So these call the same port_ntp_setup() the firmware does and read the
 * string back from the environment -- setenv() is how the setting becomes
 * live -- and then ask the C library what it makes of either side of the
 * 2026 boundaries, which is the question the panel will put to the same
 * code on the day. The device and the host build the string from the same
 * source, so what holds here holds there.
 *
 * The host's clock is not touched: port_localtime() reads it, but nothing
 * here reads port_localtime().
 */
#include "port/port_ntp.h"

#include <unity.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "test_suites.hpp"

/* The call the settings would make: the offset in hours, and the EU flag as
 * the saving it asks for. No server, because the host has a clock already. */
static void apply(int gmt_offset_h, int dst_s)
{
    port_ntp_setup(NULL, gmt_offset_h * 3600, dst_s);
}

/* ---------------------------------------------------------------- strings */

/* The default setting's string, whole and with nothing derived: what a
 * regression in the format should be read against, rather than as a panel
 * an hour out half the year. */
static void test_the_default_setting_builds_the_eu_rule(void)
{
    apply(1, 3600);

    TEST_ASSERT_EQUAL_STRING(
        "OHEZ-1:00:00OHEZDST-2:00:00,M3.5.0/2,M10.5.0/3",
        getenv("TZ"));
}

/* Off is the offset alone: no saving name, and no rule to select it. */
static void test_without_the_flag_there_is_no_rule(void)
{
    apply(1, 0);

    TEST_ASSERT_EQUAL_STRING("OHEZ-1:00:00", getenv("TZ"));
}

/* The EU's two transitions happen at 01:00 UTC, which is a different local
 * hour per zone -- the rule has to move with the GMT offset, or a British
 * and a Finnish panel would both switch away from their own clocks. */
static void test_the_hours_move_with_the_offset(void)
{
    apply(0, 3600);

    TEST_ASSERT_EQUAL_STRING(
        "OHEZ+0:00:00OHEZDST-1:00:00,M3.5.0/1,M10.5.0/2",
        getenv("TZ"));

    apply(2, 3600);

    TEST_ASSERT_EQUAL_STRING(
        "OHEZ-2:00:00OHEZDST-3:00:00,M3.5.0/3,M10.5.0/4",
        getenv("TZ"));
}

/* A GMT offset west of UTC has no EU rule to ask for; the hours are clamped
 * rather than let go negative, because a rule tzset() cannot parse takes the
 * whole timezone with it and the clock falls back to UTC. */
static void test_a_western_offset_stays_well_formed(void)
{
    apply(-5, 3600);

    TEST_ASSERT_EQUAL_STRING(
        "OHEZ+5:00:00OHEZDST+4:00:00,M3.5.0/0,M10.5.0/0",
        getenv("TZ"));
}

/* ------------------------------------------------- what localtime answers */

/* One moment UTC, as a time_t. timegm() rather than mktime(), because the
 * boundaries are defined in UTC and mktime() would fold them back through
 * the very offset under test. */
static time_t utc(int year, int month, int day, int hour, int minute)
{
    struct tm t;

    memset(&t, 0, sizeof(t));
    t.tm_year = year - 1900;
    t.tm_mon  = month - 1;
    t.tm_mday = day;
    t.tm_hour = hour;
    t.tm_min  = minute;

    return timegm(&t);
}

/* The local hour and the saving flag the panel would read at that moment.
 * Both are asserted, because they are the two things the rule decides. */
static void check_local(int year, int month, int day, int hour, int minute,
                        int want_hour, int want_isdst)
{
    time_t   at = utc(year, month, day, hour, minute);
    struct tm t;

    TEST_ASSERT_NOT_NULL(localtime_r(&at, &t));
    TEST_ASSERT_EQUAL_INT(want_hour, t.tm_hour);
    TEST_ASSERT_EQUAL_INT(want_isdst, t.tm_isdst > 0);
}

/* 2026's two Sundays are the 29th of March and the 25th of October. Half
 * an hour either side of each 01:00 UTC is what proves the switch fires on
 * the day and at the hour the rule names: the March gap is the hour that
 * does not exist, and the October pair is the hour that comes round twice.
 * Nothing short of these two moments distinguishes a right rule from a
 * near miss. */
static void test_the_rule_springs_forward_in_march(void)
{
    apply(1, 3600);

    check_local(2026, 3, 29, 0, 30, 1, 0); /* 01:30, still standard */
    check_local(2026, 3, 29, 1, 30, 3, 1); /* 03:30, and 02:30 never was */
}

static void test_the_rule_falls_back_in_october(void)
{
    apply(1, 3600);

    check_local(2026, 10, 25, 0, 30, 2, 1); /* 02:30, still saving */
    check_local(2026, 10, 25, 1, 30, 2, 0); /* 02:30 again, standard */
}

/* Deep inside the two halves of the year, where every panel spends all but
 * two nights: the saving is in force in July and not in January. */
static void test_the_saving_is_on_only_between_the_sundays(void)
{
    apply(1, 3600);

    check_local(2026, 1, 15, 12, 0, 13, 0);
    check_local(2026, 7, 1, 12, 0, 14, 1);
}

void test_port_ntp_run(void)
{
    RUN_TEST(test_the_default_setting_builds_the_eu_rule);
    RUN_TEST(test_without_the_flag_there_is_no_rule);
    RUN_TEST(test_the_hours_move_with_the_offset);
    RUN_TEST(test_a_western_offset_stays_well_formed);
    RUN_TEST(test_the_rule_springs_forward_in_march);
    RUN_TEST(test_the_rule_falls_back_in_october);
    RUN_TEST(test_the_saving_is_on_only_between_the_sundays);

    /* Back to nothing, so the suites that run after this one see the process
     * they started with: TZ is environment-wide and the runner is one. */
    unsetenv("TZ");
    tzset();
}
