/* Unit tests for the beacon parsers in main/ble/ble_beacon.cpp.
 *
 * This is the part of the BLE work that most needs them. The three layouts are
 * specified by Apple and by Google and cannot be checked by reading the code
 * that consumes them; the bytes arrive from devices nobody here wrote, so
 * malformed input is the normal case rather than the exception; and the one
 * place any of it can be exercised is a test, because the device firmware has
 * never been run on hardware and a desktop has no Bluetooth to hear a real
 * advertisement with.
 *
 * Which is why ble_beacon.cpp has no radio, no LVGL and no Config in it: the
 * split in port_ble.h -- the port yields raw advertisement bytes, the parsing
 * happens above it -- exists so that these tests can be written at all.
 *
 * The fixtures below are the same shape as the ones in
 * main/port/linux/port_ble.c, deliberately written out again rather than shared
 * with them: a test that imports the fixture it is checking against proves only
 * that two copies of the same mistake agree.
 *
 * Run with:
 *   cd test/host && idf.py build && ./build/oh-ez-touch-host-test.elf
 */

#include <unity.h>

#include <string.h>

#include "ble/ble_beacon.hpp"
#include "test_suites.hpp"

/* An AD structure is `length, type, data...`, and the length counts the type
 * byte. Every advertisement starts with the flags structure. */
#define AD_FLAGS 0x02, 0x01, 0x06

static struct ble_beacon_s beacon;

/* ---------------------------------------------------------------- iBeacon */

static const uint8_t ibeacon[] = {
    AD_FLAGS,
    0x1A, 0xFF, 0x4C, 0x00, 0x02, 0x15,
    0xF7, 0x82, 0x6D, 0xA6, 0x4F, 0xA2, 0x4E, 0x98,
    0x80, 0x24, 0xBC, 0x5B, 0x71, 0xE0, 0x89, 0x3E,
    0x03, 0xE8, /* major 1000 */
    0x00, 0x2A, /* minor 42   */
    0xC5        /* -59 dBm at one metre */
};

static void test_ibeacon(void)
{
    ble_beacon_parse(ibeacon, sizeof(ibeacon), &beacon);

    TEST_ASSERT_EQUAL_INT(BLE_BEACON_IBEACON, beacon.kind);
    TEST_ASSERT_EQUAL_STRING("f7826da6-4fa2-4e98-8024-bc5b71e0893e-1000-42", beacon.id);
    TEST_ASSERT_EQUAL_INT(-59, beacon.ref_power);
    /* No AD type 0x0A in this advertisement, and the iBeacon power must not be
     * mistaken for one: only ref_power can produce a distance. */
    TEST_ASSERT_EQUAL_INT(BLE_BEACON_POWER_UNKNOWN, beacon.tx_power);
    TEST_ASSERT_EQUAL_STRING("", beacon.name);
    TEST_ASSERT_FALSE(beacon.have_telemetry);
}

/* The identity buffer is sized for the longest this format can produce, and
 * "longest" means both counters at 65535. */
static void test_ibeacon_longest_identity_fits(void)
{
    uint8_t adv[sizeof(ibeacon)];

    memcpy(adv, ibeacon, sizeof(adv));

    /* major and minor to 0xFFFF, and the UUID to all ff. */
    memset(&adv[9], 0xFF, 16 + 4);

    ble_beacon_parse(adv, sizeof(adv), &beacon);

    TEST_ASSERT_EQUAL_STRING("ffffffff-ffff-ffff-ffff-ffffffffffff-65535-65535", beacon.id);
    TEST_ASSERT_EQUAL_UINT(strlen(beacon.id) + 1, 49);
    TEST_ASSERT_TRUE(strlen(beacon.id) + 1 <= BLE_BEACON_ID_SIZE);
}

/* Another company's manufacturer data is not an iBeacon however much it looks
 * like one -- the company identifier is the only thing that says which. */
static void test_manufacturer_data_from_another_company(void)
{
    uint8_t adv[sizeof(ibeacon)];

    memcpy(adv, ibeacon, sizeof(adv));
    adv[5] = 0x59; /* Nordic, little-endian 0x0059 */
    adv[6] = 0x00;

    ble_beacon_parse(adv, sizeof(adv), &beacon);

    TEST_ASSERT_EQUAL_INT(BLE_BEACON_NONE, beacon.kind);
    TEST_ASSERT_EQUAL_STRING("", beacon.id);
}

/* A structure whose subtype or subtype length is not Apple's proximity pair. */
static void test_apple_data_that_is_not_a_beacon(void)
{
    uint8_t adv[sizeof(ibeacon)];

    memcpy(adv, ibeacon, sizeof(adv));
    adv[7] = 0x0C; /* the "handoff" subtype, which many Apple devices send */

    ble_beacon_parse(adv, sizeof(adv), &beacon);

    TEST_ASSERT_EQUAL_INT(BLE_BEACON_NONE, beacon.kind);
}

/* -------------------------------------------------------------- Eddystone */

static const uint8_t eddystone_uid[] = {
    AD_FLAGS,
    0x03, 0x03, 0xAA, 0xFE,
    0x17, 0x16, 0xAA, 0xFE, 0x00,
    0xEB, /* -21 dBm at zero metres */
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
    0x00, 0x00
};

static void test_eddystone_uid(void)
{
    ble_beacon_parse(eddystone_uid, sizeof(eddystone_uid), &beacon);

    TEST_ASSERT_EQUAL_INT(BLE_BEACON_EDDYSTONE_UID, beacon.kind);
    TEST_ASSERT_EQUAL_STRING("0102030405060708090a-000000000001", beacon.id);
    /* Eddystone states the power at zero metres; the specification's own
     * conversion to one metre is 41 dB, applied on the way in so that
     * ref_power means one thing whichever format it came from. */
    TEST_ASSERT_EQUAL_INT(-21 - 41, beacon.ref_power);
}

/* Real beacons sometimes leave the two reserved bytes off the end. */
static void test_eddystone_uid_without_the_reserved_bytes(void)
{
    uint8_t adv[sizeof(eddystone_uid) - 2];

    memcpy(adv, eddystone_uid, sizeof(adv));
    adv[7] = 0x15; /* the service data structure is two bytes shorter */

    ble_beacon_parse(adv, sizeof(adv), &beacon);

    TEST_ASSERT_EQUAL_INT(BLE_BEACON_EDDYSTONE_UID, beacon.kind);
    TEST_ASSERT_EQUAL_STRING("0102030405060708090a-000000000001", beacon.id);
}

static void test_eddystone_url(void)
{
    /* scheme 0x00 is "http://www.", and 0x07 inside the URL is ".com". */
    const uint8_t adv[] = {
        AD_FLAGS,
        0x0E, 0x16, 0xAA, 0xFE, 0x10,
        0xEE, /* -18 dBm at zero metres */
        0x00, 'e', 'x', 'a', 'm', 'p', 'l', 'e', 0x07
    };

    ble_beacon_parse(adv, sizeof(adv), &beacon);

    TEST_ASSERT_EQUAL_INT(BLE_BEACON_EDDYSTONE_URL, beacon.kind);
    TEST_ASSERT_EQUAL_STRING("http://www.example.com", beacon.id);
    TEST_ASSERT_EQUAL_INT(-18 - 41, beacon.ref_power);
}

/* A reserved expansion code is dropped rather than guessed at: a plausible
 * wrong URL is worse than a short one, because it resolves somewhere. */
static void test_eddystone_url_skips_reserved_codes(void)
{
    const uint8_t adv[] = {
        AD_FLAGS,
        0x09, 0x16, 0xAA, 0xFE, 0x10, 0xEE,
        0x03, 'a', 0x0E, 'b' /* 0x0E is the first reserved code */
    };

    ble_beacon_parse(adv, sizeof(adv), &beacon);

    TEST_ASSERT_EQUAL_INT(BLE_BEACON_EDDYSTONE_URL, beacon.kind);
    TEST_ASSERT_EQUAL_STRING("https://ab", beacon.id);
}

static void test_eddystone_url_with_an_unknown_scheme(void)
{
    const uint8_t adv[] = {
        AD_FLAGS,
        0x07, 0x16, 0xAA, 0xFE, 0x10, 0xEE,
        0x09, 'a', 'b' /* only 0x00 to 0x03 are schemes */
    };

    ble_beacon_parse(adv, sizeof(adv), &beacon);

    TEST_ASSERT_EQUAL_INT(BLE_BEACON_NONE, beacon.kind);
    TEST_ASSERT_EQUAL_STRING("", beacon.id);
}

static void test_eddystone_telemetry(void)
{
    const uint8_t adv[] = {
        AD_FLAGS,
        0x03, 0x03, 0xAA, 0xFE,
        0x11, 0x16, 0xAA, 0xFE, 0x20,
        0x00,       /* TLM version 0 */
        0x0B, 0xB8, /* 3000 mV */
        0x15, 0x80, /* 21.5 C, 8.8 fixed point */
        0x00, 0x00, 0x0A, 0xBC,
        0x00, 0x00, 0x27, 0x10
    };

    ble_beacon_parse(adv, sizeof(adv), &beacon);

    TEST_ASSERT_TRUE(beacon.have_telemetry);
    TEST_ASSERT_EQUAL_UINT16(3000, beacon.battery_mv);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 21.5f, beacon.temperature_c);

    /* And no identity: a TLM frame carries none, so it must not claim to be a
     * beacon. That is what lets ble_scan.cpp merge one into an entry whose
     * identity came from a different advertisement without demoting it. */
    TEST_ASSERT_EQUAL_INT(BLE_BEACON_NONE, beacon.kind);
    TEST_ASSERT_EQUAL_STRING("", beacon.id);
}

/* A negative temperature, which the 8.8 fixed point encodes as two's
 * complement across both bytes. */
static void test_eddystone_telemetry_below_zero(void)
{
    const uint8_t adv[] = {
        AD_FLAGS,
        0x11, 0x16, 0xAA, 0xFE, 0x20, 0x00,
        0x0B, 0xB8,
        0xFB, 0x80, /* -4.5 C */
        0x00, 0x00, 0x00, 0x01,
        0x00, 0x00, 0x00, 0x01
    };

    ble_beacon_parse(adv, sizeof(adv), &beacon);

    TEST_ASSERT_TRUE(beacon.have_telemetry);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, -4.5f, beacon.temperature_c);
}

/* 0x8000 is the specified "not supported" value, and -128.0 is what reading it
 * as a temperature would give -- a number a dashboard would happily plot. */
static void test_eddystone_telemetry_without_a_thermometer(void)
{
    const uint8_t adv[] = {
        AD_FLAGS,
        0x11, 0x16, 0xAA, 0xFE, 0x20, 0x00,
        0x0B, 0xB8,
        0x80, 0x00,
        0x00, 0x00, 0x00, 0x01,
        0x00, 0x00, 0x00, 0x01
    };

    ble_beacon_parse(adv, sizeof(adv), &beacon);

    TEST_ASSERT_TRUE(beacon.have_telemetry);
    TEST_ASSERT_EQUAL_UINT16(3000, beacon.battery_mv);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, beacon.temperature_c);
}

/* Version 1 is the encrypted variant and its fields are not these ones. */
static void test_eddystone_telemetry_of_an_unknown_version(void)
{
    const uint8_t adv[] = {
        AD_FLAGS,
        0x11, 0x16, 0xAA, 0xFE, 0x20, 0x01,
        0x0B, 0xB8, 0x15, 0x80,
        0x00, 0x00, 0x00, 0x01,
        0x00, 0x00, 0x00, 0x01
    };

    ble_beacon_parse(adv, sizeof(adv), &beacon);

    TEST_ASSERT_FALSE(beacon.have_telemetry);
}

/* Service data under some other UUID is not Eddystone. */
static void test_service_data_under_another_uuid(void)
{
    uint8_t adv[sizeof(eddystone_uid)];

    memcpy(adv, eddystone_uid, sizeof(adv));
    adv[9] = 0x0D; /* 0x180D, the heart rate service */
    adv[10] = 0x18;

    ble_beacon_parse(adv, sizeof(adv), &beacon);

    TEST_ASSERT_EQUAL_INT(BLE_BEACON_NONE, beacon.kind);
}

/* ---------------------------------------------------------- plain devices */

static void test_a_named_device_is_not_a_beacon(void)
{
    const uint8_t adv[] = {
        AD_FLAGS,
        0x09, 0x09, 'L', 'i', 'v', 'i', 'n', 'g', 'T', 'V',
        0x02, 0x0A, 0xF4 /* -12 dBm */
    };

    ble_beacon_parse(adv, sizeof(adv), &beacon);

    TEST_ASSERT_EQUAL_INT(BLE_BEACON_NONE, beacon.kind);
    TEST_ASSERT_EQUAL_STRING("LivingTV", beacon.name);
    TEST_ASSERT_EQUAL_STRING("", beacon.id);
    TEST_ASSERT_EQUAL_INT(-12, beacon.tx_power);
    /* The whole point of the two fields: a transmit power is not a calibrated
     * one, so no distance can be derived from this. */
    TEST_ASSERT_EQUAL_INT(BLE_BEACON_POWER_UNKNOWN, beacon.ref_power);
}

/* An advertiser may send both a short and a complete name, in either order.
 * The complete one wins whichever arrives first. */
static void test_the_complete_name_wins(void)
{
    const uint8_t short_first[] = {
        AD_FLAGS,
        0x04, 0x08, 'A', 'b', 'c',
        0x07, 0x09, 'A', 'b', 'c', 'd', 'e', 'f'
    };
    const uint8_t complete_first[] = {
        AD_FLAGS,
        0x07, 0x09, 'A', 'b', 'c', 'd', 'e', 'f',
        0x04, 0x08, 'A', 'b', 'c'
    };

    ble_beacon_parse(short_first, sizeof(short_first), &beacon);
    TEST_ASSERT_EQUAL_STRING("Abcdef", beacon.name);

    ble_beacon_parse(complete_first, sizeof(complete_first), &beacon);
    TEST_ASSERT_EQUAL_STRING("Abcdef", beacon.name);
}

/* -------------------------------------------------------- malformed input */

static void test_out_is_assigned_even_for_nothing_at_all(void)
{
    /* Not "does not crash": the caller reads every field afterwards, so a
     * parse that returns without assigning would hand back whatever the last
     * advertisement left in the struct. */
    ble_beacon_parse(ibeacon, sizeof(ibeacon), &beacon);
    TEST_ASSERT_EQUAL_INT(BLE_BEACON_IBEACON, beacon.kind);

    ble_beacon_parse(NULL, 0, &beacon);
    TEST_ASSERT_EQUAL_INT(BLE_BEACON_NONE, beacon.kind);
    TEST_ASSERT_EQUAL_STRING("", beacon.id);
    TEST_ASSERT_EQUAL_STRING("", beacon.name);
    TEST_ASSERT_EQUAL_INT(BLE_BEACON_POWER_UNKNOWN, beacon.ref_power);
    TEST_ASSERT_EQUAL_INT(BLE_BEACON_POWER_UNKNOWN, beacon.tx_power);
    TEST_ASSERT_FALSE(beacon.have_telemetry);

    ble_beacon_parse(ibeacon, 0, &beacon);
    TEST_ASSERT_EQUAL_INT(BLE_BEACON_NONE, beacon.kind);
}

/* A length field that runs past the end of the payload. This is the one that
 * matters: believing it reads other people's memory. */
static void test_a_length_past_the_end_is_refused(void)
{
    const uint8_t adv[] = {AD_FLAGS, 0x1F, 0x09, 'A', 'b'};

    ble_beacon_parse(adv, sizeof(adv), &beacon);

    TEST_ASSERT_EQUAL_STRING("", beacon.name);
}

/* Truncating a valid advertisement at every possible point must not produce a
 * beacon out of thin air, and must not read past the length it was given --
 * which the sanitizers in a debug build are watching for. */
static void test_every_truncation_of_a_valid_advertisement(void)
{
    for (size_t len = 0; len < sizeof(ibeacon); len++)
    {
        ble_beacon_parse(ibeacon, len, &beacon);
        TEST_ASSERT_EQUAL_INT(BLE_BEACON_NONE, beacon.kind);
    }

    for (size_t len = 0; len < sizeof(eddystone_uid); len++)
    {
        ble_beacon_parse(eddystone_uid, len, &beacon);

        if (beacon.kind != BLE_BEACON_NONE)
        {
            /* The frame becomes parseable before the reserved bytes arrive,
             * which is deliberate -- see the test above -- so anything from
             * there on is allowed to be a UID and nothing else is. */
            TEST_ASSERT_EQUAL_INT(BLE_BEACON_EDDYSTONE_UID, beacon.kind);
            TEST_ASSERT_EQUAL_STRING("0102030405060708090a-000000000001", beacon.id);
        }
    }
}

/* A zero length is the specified terminator for the padding at the end of a
 * 31 byte payload, and everything after it is padding rather than data. */
static void test_a_zero_length_ends_the_walk(void)
{
    uint8_t adv[31] = {AD_FLAGS, 0x00};

    /* What would be a name, if the terminator above were ignored. */
    adv[4] = 0x04;
    adv[5] = 0x09;
    adv[6] = 'A';
    adv[7] = 'b';
    adv[8] = 'c';

    ble_beacon_parse(adv, sizeof(adv), &beacon);

    TEST_ASSERT_EQUAL_STRING("", beacon.name);
}

/* A name longer than the field it is copied into. */
static void test_an_over_long_name_truncates_and_terminates(void)
{
    uint8_t adv[3 + 2 + 29];
    size_t  i = 0;

    adv[i++] = 0x02;
    adv[i++] = 0x01;
    adv[i++] = 0x06;
    adv[i++] = 0x1E; /* 1 type + 29 characters */
    adv[i++] = 0x09;

    for (int c = 0; c < 29; c++)
        adv[i++] = (uint8_t)('a' + (c % 26));

    ble_beacon_parse(adv, sizeof(adv), &beacon);

    TEST_ASSERT_EQUAL_UINT(BLE_BEACON_NAME_SIZE - 1, strlen(beacon.name));
    TEST_ASSERT_EQUAL_CHAR('\0', beacon.name[BLE_BEACON_NAME_SIZE - 1]);
}

/* ------------------------------------------------------------- the ranging */

static void test_distance_at_the_reference_power_is_one_metre(void)
{
    /* The definition of the reference power, so this is the one point on the
     * curve that is not a model but an identity. */
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.0f, ble_beacon_distance(-59, -59));
}

static void test_distance_follows_the_path_loss_model(void)
{
    /* 20 dB below the reference is ten metres, and 20 above is a tenth. */
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 10.0f, ble_beacon_distance(-79, -59));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.1f, ble_beacon_distance(-39, -59));
}

static void test_no_reference_power_means_no_distance(void)
{
    /* Negative rather than a guess at a typical beacon: a fabricated distance
     * is indistinguishable from a measured one. */
    TEST_ASSERT_TRUE(ble_beacon_distance(-70, BLE_BEACON_POWER_UNKNOWN) < 0.0f);
    TEST_ASSERT_TRUE(ble_beacon_distance(127, -59) < 0.0f);
}

static void test_kind_names_are_all_present(void)
{
    for (int i = 0; i < BLE_BEACON_KIND_COUNT; i++)
    {
        const char *name = ble_beacon_kind_name((enum ble_beacon_kind_e)i);

        TEST_ASSERT_NOT_NULL(name);
        TEST_ASSERT_TRUE(name[0] != '\0');
    }

    /* These strings are published as a topic value, so they are part of the
     * interface and not just a log line. */
    TEST_ASSERT_EQUAL_STRING("device", ble_beacon_kind_name(BLE_BEACON_NONE));
    TEST_ASSERT_EQUAL_STRING("iBeacon", ble_beacon_kind_name(BLE_BEACON_IBEACON));
    TEST_ASSERT_EQUAL_STRING("Eddystone-UID", ble_beacon_kind_name(BLE_BEACON_EDDYSTONE_UID));
    TEST_ASSERT_EQUAL_STRING("Eddystone-URL", ble_beacon_kind_name(BLE_BEACON_EDDYSTONE_URL));

    /* Out of range names the fallback rather than reading past the table. */
    TEST_ASSERT_EQUAL_STRING("device", ble_beacon_kind_name((enum ble_beacon_kind_e)99));
}

void test_ble_beacon_run(void)
{
    RUN_TEST(test_ibeacon);
    RUN_TEST(test_ibeacon_longest_identity_fits);
    RUN_TEST(test_manufacturer_data_from_another_company);
    RUN_TEST(test_apple_data_that_is_not_a_beacon);
    RUN_TEST(test_eddystone_uid);
    RUN_TEST(test_eddystone_uid_without_the_reserved_bytes);
    RUN_TEST(test_eddystone_url);
    RUN_TEST(test_eddystone_url_skips_reserved_codes);
    RUN_TEST(test_eddystone_url_with_an_unknown_scheme);
    RUN_TEST(test_eddystone_telemetry);
    RUN_TEST(test_eddystone_telemetry_below_zero);
    RUN_TEST(test_eddystone_telemetry_without_a_thermometer);
    RUN_TEST(test_eddystone_telemetry_of_an_unknown_version);
    RUN_TEST(test_service_data_under_another_uuid);
    RUN_TEST(test_a_named_device_is_not_a_beacon);
    RUN_TEST(test_the_complete_name_wins);
    RUN_TEST(test_out_is_assigned_even_for_nothing_at_all);
    RUN_TEST(test_a_length_past_the_end_is_refused);
    RUN_TEST(test_every_truncation_of_a_valid_advertisement);
    RUN_TEST(test_a_zero_length_ends_the_walk);
    RUN_TEST(test_an_over_long_name_truncates_and_terminates);
    RUN_TEST(test_distance_at_the_reference_power_is_one_metre);
    RUN_TEST(test_distance_follows_the_path_loss_model);
    RUN_TEST(test_no_reference_power_means_no_distance);
    RUN_TEST(test_kind_names_are_all_present);
}
