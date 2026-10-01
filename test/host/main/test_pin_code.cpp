/* Unit tests for the PIN arithmetic in main/config/pin_code.c.
 *
 * Three things, each of which would fail silently on the panel. A SHA-256
 * that is wrong still produces 32 bytes, and a PIN stored with it still
 * opens -- until the day the code is replaced by a correct one and every
 * stored PIN stops matching. So the hash is checked against the FIPS 180-2
 * test vectors, including the one whose padding spills into a second block.
 *
 * The validation decides what the pad accepts, and the lockout decides how
 * long a run of guesses takes. Both are pinned at their edges: the lengths
 * either side of 4 and 8, the fifth wrong try and the tenth, and a lockout
 * that straddles the 32-bit millisecond clock's wrap.
 *
 * pin_store.c is not here: it is NVS, and NVS is not in this build. Run with:
 *   cd test/host && idf.py build && ./build/oh-ez-touch-host-test.elf
 */

#include <string.h>

#include <unity.h>

#include "config/pin_code.h"
#include "test_suites.hpp"

static void hex_of_sha256(const void *data, size_t len, char out[65])
{
    uint8_t hash[PIN_CODE_HASH_LEN];

    pin_code_sha256(data, len, hash);
    pin_code_to_hex(hash, sizeof(hash), out);
}

/* ------------------------------------------------------------- validation */

static void test_four_to_eight_digits_are_a_pin(void)
{
    TEST_ASSERT_TRUE(pin_code_valid("0000"));
    TEST_ASSERT_TRUE(pin_code_valid("12345"));
    TEST_ASSERT_TRUE(pin_code_valid("98765432"));

    TEST_ASSERT_FALSE(pin_code_valid(NULL));
    TEST_ASSERT_FALSE(pin_code_valid(""));
    TEST_ASSERT_FALSE(pin_code_valid("123"));
    TEST_ASSERT_FALSE(pin_code_valid("123456789"));
}

static void test_anything_but_a_digit_is_not(void)
{
    TEST_ASSERT_FALSE(pin_code_valid("12a4"));
    TEST_ASSERT_FALSE(pin_code_valid("12 4"));
    TEST_ASSERT_FALSE(pin_code_valid("-1234"));
    TEST_ASSERT_FALSE(pin_code_valid("1234\n"));
    /* The byte after '9' and the one before '0'. */
    TEST_ASSERT_FALSE(pin_code_valid("123:"));
    TEST_ASSERT_FALSE(pin_code_valid("123/"));
}

/* ----------------------------------------------------------------- SHA-256 */

static void test_sha256_matches_the_standard_vectors(void)
{
    char hex[65];

    hex_of_sha256("", 0, hex);
    TEST_ASSERT_EQUAL_STRING(
        "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", hex);

    hex_of_sha256("abc", 3, hex);
    TEST_ASSERT_EQUAL_STRING(
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", hex);

    /* 56 bytes: no room left in the first block for the length, so the
     * padding takes a second one. */
    const char *two = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";

    hex_of_sha256(two, strlen(two), hex);
    TEST_ASSERT_EQUAL_STRING(
        "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1", hex);
}

static void test_sha256_of_many_blocks(void)
{
    static char million[1000000];
    char        hex[65];

    memset(million, 'a', sizeof(million));
    hex_of_sha256(million, sizeof(million), hex);
    TEST_ASSERT_EQUAL_STRING(
        "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0", hex);
}

/* -------------------------------------------------------- the stored hash */

static void test_the_hash_is_the_salt_and_the_pin(void)
{
    uint8_t salt[PIN_CODE_SALT_LEN];
    uint8_t buf[PIN_CODE_SALT_LEN + 4];
    uint8_t a[PIN_CODE_HASH_LEN];
    uint8_t b[PIN_CODE_HASH_LEN];

    for (size_t i = 0; i < sizeof(salt); i++)
        salt[i] = (uint8_t)(i * 17 + 3);

    memcpy(buf, salt, sizeof(salt));
    memcpy(buf + sizeof(salt), "1234", 4);

    pin_code_hash("1234", salt, a);
    pin_code_sha256(buf, sizeof(buf), b);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(b, a, PIN_CODE_HASH_LEN);
}

static void test_a_different_salt_is_a_different_hash(void)
{
    uint8_t salt[PIN_CODE_SALT_LEN] = {0};
    uint8_t a[PIN_CODE_HASH_LEN];
    uint8_t b[PIN_CODE_HASH_LEN];

    pin_code_hash("1234", salt, a);
    pin_code_hash("1234", salt, b);
    TEST_ASSERT_TRUE(pin_code_equal(a, b, sizeof(a)));

    salt[15] = 1;
    pin_code_hash("1234", salt, b);
    TEST_ASSERT_FALSE(pin_code_equal(a, b, sizeof(a)));

    salt[15] = 0;
    pin_code_hash("1235", salt, b);
    TEST_ASSERT_FALSE(pin_code_equal(a, b, sizeof(a)));
}

static void test_hex_round_trips_and_refuses_junk(void)
{
    uint8_t in[4] = {0x00, 0x7f, 0xa5, 0xff};
    uint8_t out[4] = {0};
    char    hex[9];

    pin_code_to_hex(in, sizeof(in), hex);
    TEST_ASSERT_EQUAL_STRING("007fa5ff", hex);
    TEST_ASSERT_TRUE(pin_code_from_hex(hex, out, sizeof(out)));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(in, out, sizeof(in));

    TEST_ASSERT_TRUE(pin_code_from_hex("007FA5FF", out, sizeof(out)));
    TEST_ASSERT_FALSE(pin_code_from_hex("007fa5f", out, sizeof(out)));
    TEST_ASSERT_FALSE(pin_code_from_hex("007fa5ff0", out, sizeof(out)));
    TEST_ASSERT_FALSE(pin_code_from_hex("007fa5fg", out, sizeof(out)));
    TEST_ASSERT_FALSE(pin_code_from_hex(NULL, out, sizeof(out)));
}

/* ----------------------------------------------------------------- lockout */

static void test_five_wrong_tries_lock_for_thirty_seconds(void)
{
    struct pin_lockout_s lo;
    uint32_t             remaining = 0;

    memset(&lo, 0, sizeof(lo));

    for (int i = 0; i < PIN_LOCKOUT_TRIES - 1; i++)
    {
        pin_lockout_fail(&lo, 1000);
        TEST_ASSERT_FALSE(pin_lockout_active(&lo, 1000, &remaining));
    }

    pin_lockout_fail(&lo, 1000);
    TEST_ASSERT_TRUE(pin_lockout_active(&lo, 1000, &remaining));
    TEST_ASSERT_EQUAL_UINT32(PIN_LOCKOUT_MS, remaining);
    TEST_ASSERT_TRUE(pin_lockout_active(&lo, 1000 + PIN_LOCKOUT_MS - 1, &remaining));
    TEST_ASSERT_EQUAL_UINT32(1, remaining);
    TEST_ASSERT_FALSE(pin_lockout_active(&lo, 1000 + PIN_LOCKOUT_MS, &remaining));
    TEST_ASSERT_EQUAL_UINT32(0, remaining);
}

static void test_each_further_run_doubles_up_to_the_cap(void)
{
    struct pin_lockout_s lo;
    uint32_t             now = 0;
    uint32_t             remaining = 0;
    uint32_t             expect = PIN_LOCKOUT_MS;

    memset(&lo, 0, sizeof(lo));

    for (int run = 0; run < 8; run++)
    {
        for (int i = 0; i < PIN_LOCKOUT_TRIES; i++)
            pin_lockout_fail(&lo, now);

        TEST_ASSERT_TRUE(pin_lockout_active(&lo, now, &remaining));
        TEST_ASSERT_EQUAL_UINT32(expect, remaining);

        now += remaining;
        expect = (expect * 2 > PIN_LOCKOUT_MAX_MS) ? PIN_LOCKOUT_MAX_MS : expect * 2;
    }
}

static void test_a_correct_pin_forgets_the_failures(void)
{
    struct pin_lockout_s lo;

    memset(&lo, 0, sizeof(lo));

    for (int i = 0; i < PIN_LOCKOUT_TRIES - 1; i++)
        pin_lockout_fail(&lo, 0);

    pin_lockout_success(&lo);

    /* Four more, and still not locked: the count started again. */
    for (int i = 0; i < PIN_LOCKOUT_TRIES - 1; i++)
        pin_lockout_fail(&lo, 0);

    TEST_ASSERT_FALSE(pin_lockout_active(&lo, 0, NULL));
}

static void test_a_lockout_survives_the_clock_wrapping(void)
{
    struct pin_lockout_s lo;
    uint32_t             start = UINT32_MAX - 1000;
    uint32_t             remaining = 0;

    memset(&lo, 0, sizeof(lo));

    for (int i = 0; i < PIN_LOCKOUT_TRIES; i++)
        pin_lockout_fail(&lo, start);

    TEST_ASSERT_TRUE(pin_lockout_active(&lo, 5000, &remaining));
    TEST_ASSERT_EQUAL_UINT32(PIN_LOCKOUT_MS - 6001, remaining);
    TEST_ASSERT_FALSE(pin_lockout_active(&lo, start + PIN_LOCKOUT_MS, NULL));
}

void test_pin_code_run(void)
{
    RUN_TEST(test_four_to_eight_digits_are_a_pin);
    RUN_TEST(test_anything_but_a_digit_is_not);
    RUN_TEST(test_sha256_matches_the_standard_vectors);
    RUN_TEST(test_sha256_of_many_blocks);
    RUN_TEST(test_the_hash_is_the_salt_and_the_pin);
    RUN_TEST(test_a_different_salt_is_a_different_hash);
    RUN_TEST(test_hex_round_trips_and_refuses_junk);
    RUN_TEST(test_five_wrong_tries_lock_for_thirty_seconds);
    RUN_TEST(test_each_further_run_doubles_up_to_the_cap);
    RUN_TEST(test_a_correct_pin_forgets_the_failures);
    RUN_TEST(test_a_lockout_survives_the_clock_wrapping);
}
