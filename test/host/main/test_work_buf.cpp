/* Unit tests for mem/work_buf.c, the block that several modules take turns on.
 *
 * What has to hold: one owner at a time, whatever the order of calls; a
 * try_claim() that never waits and steps aside for a waiting claim(); a
 * handover that never lets the block go free in between; and a release or
 * handover by anyone but the holder that changes nothing.
 */

#include <unity.h>

#include "mem/work_buf.h"
#include "test_suites.hpp"

#include <atomic>
#include <chrono>
#include <thread>

static uint32_t fake_now;

static uint32_t fake_clock(void)
{
    return fake_now;
}

static void thread_yield(void)
{
    std::this_thread::yield();
}

static void fresh(size_t size = 1024)
{
    work_buf_reset();
    fake_now = 0;
    TEST_ASSERT_TRUE(work_buf_init(size, fake_clock, thread_yield));
}

static void test_nothing_before_init(void)
{
    work_buf_reset();

    TEST_ASSERT_EQUAL_UINT(0, work_buf_size());
    TEST_ASSERT_NULL(work_buf_try_claim(WORK_BUF_GLYPH));
    TEST_ASSERT_NULL(work_buf_claim(WORK_BUF_OPENHAB_READ));
    TEST_ASSERT_FALSE(work_buf_release(WORK_BUF_GLYPH));
}

static void test_init_once(void)
{
    fresh(4096);

    TEST_ASSERT_EQUAL_UINT(4096, work_buf_size());
    TEST_ASSERT_FALSE(work_buf_init(8192, fake_clock, thread_yield));
    TEST_ASSERT_EQUAL_UINT(4096, work_buf_size());
}

static void test_one_owner_at_a_time(void)
{
    fresh();

    void *a = work_buf_try_claim(WORK_BUF_GLYPH);

    TEST_ASSERT_NOT_NULL(a);
    TEST_ASSERT_TRUE(work_buf_is(a));
    TEST_ASSERT_EQUAL(WORK_BUF_GLYPH, work_buf_owner());
    TEST_ASSERT_NULL(work_buf_try_claim(WORK_BUF_OPENHAB_READ));
    TEST_ASSERT_NULL(work_buf_try_claim(WORK_BUF_GLYPH));

    TEST_ASSERT_TRUE(work_buf_release(WORK_BUF_GLYPH));
    TEST_ASSERT_EQUAL(WORK_BUF_FREE, work_buf_owner());
    TEST_ASSERT_EQUAL_PTR(a, work_buf_try_claim(WORK_BUF_OPENHAB_READ));
}

/* A release by anyone but the holder must not free the block under it. */
static void test_only_the_holder_releases(void)
{
    fresh();

    TEST_ASSERT_NOT_NULL(work_buf_try_claim(WORK_BUF_OPENHAB_READ));
    TEST_ASSERT_FALSE(work_buf_release(WORK_BUF_GLYPH));
    TEST_ASSERT_FALSE(work_buf_release(WORK_BUF_FREE));
    TEST_ASSERT_EQUAL(WORK_BUF_OPENHAB_READ, work_buf_owner());

    TEST_ASSERT_TRUE(work_buf_release(WORK_BUF_OPENHAB_READ));
    TEST_ASSERT_FALSE(work_buf_release(WORK_BUF_OPENHAB_READ));
}

/* The read becomes the result without the block going free in between, and
 * only the holder can hand it on. */
static void test_handover(void)
{
    fresh();

    TEST_ASSERT_NOT_NULL(work_buf_claim(WORK_BUF_OPENHAB_READ));
    TEST_ASSERT_FALSE(work_buf_handover(WORK_BUF_GLYPH, WORK_BUF_OPENHAB_RESULT));
    TEST_ASSERT_FALSE(work_buf_handover(WORK_BUF_OPENHAB_READ, WORK_BUF_FREE));
    TEST_ASSERT_TRUE(work_buf_handover(WORK_BUF_OPENHAB_READ, WORK_BUF_OPENHAB_RESULT));
    TEST_ASSERT_EQUAL(WORK_BUF_OPENHAB_RESULT, work_buf_owner());

    TEST_ASSERT_NULL(work_buf_try_claim(WORK_BUF_GLYPH));
    TEST_ASSERT_FALSE(work_buf_release(WORK_BUF_OPENHAB_READ));
    TEST_ASSERT_TRUE(work_buf_release(WORK_BUF_OPENHAB_RESULT));
    TEST_ASSERT_EQUAL(WORK_BUF_FREE, work_buf_owner());
}

/* claim() waits for the holder, and while it waits, try_claim() steps aside:
 * the glyph draw must not keep a page read out for a whole stream of frames.
 * No Unity assert inside the thread: a failure there would longjmp across
 * threads. */
static void test_claim_waits_and_wins_over_try_claim(void)
{
    fresh();

    TEST_ASSERT_NOT_NULL(work_buf_try_claim(WORK_BUF_GLYPH));

    std::atomic<void *> got{nullptr};
    std::thread reader([&got] { got = work_buf_claim(WORK_BUF_OPENHAB_READ); });

    /* Long enough for the reader to be counted as waiting. */
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    TEST_ASSERT_NULL(got.load());
    TEST_ASSERT_EQUAL(WORK_BUF_GLYPH, work_buf_owner());
    TEST_ASSERT_TRUE(work_buf_release(WORK_BUF_GLYPH));

    /* The waiter is served first: a try_claim() now finds it waiting or
     * already holding the block, never the block free. */
    void *stolen = work_buf_try_claim(WORK_BUF_GLYPH);

    reader.join();

    TEST_ASSERT_NULL(stolen);
    TEST_ASSERT_NOT_NULL(got.load());
    TEST_ASSERT_EQUAL(WORK_BUF_OPENHAB_READ, work_buf_owner());
    TEST_ASSERT_TRUE(work_buf_release(WORK_BUF_OPENHAB_READ));
}

static void test_stats(void)
{
    fresh();

    fake_now = 100;
    TEST_ASSERT_NOT_NULL(work_buf_try_claim(WORK_BUF_OPENHAB_READ));
    fake_now = 130;
    TEST_ASSERT_TRUE(work_buf_handover(WORK_BUF_OPENHAB_READ, WORK_BUF_OPENHAB_RESULT));
    TEST_ASSERT_NULL(work_buf_try_claim(WORK_BUF_GLYPH));
    fake_now = 210;
    TEST_ASSERT_TRUE(work_buf_release(WORK_BUF_OPENHAB_RESULT));

    fake_now = 300;
    TEST_ASSERT_NOT_NULL(work_buf_try_claim(WORK_BUF_GLYPH));
    fake_now = 305;
    TEST_ASSERT_TRUE(work_buf_release(WORK_BUF_GLYPH));

    struct work_buf_stats_s st;

    work_buf_stats(&st);

    /* A handover is not a second claim. */
    TEST_ASSERT_EQUAL_UINT32(2, st.claims);
    TEST_ASSERT_EQUAL_UINT32(1, st.busy);
    TEST_ASSERT_EQUAL_UINT32(80, st.max_hold_ms);
    TEST_ASSERT_EQUAL(WORK_BUF_OPENHAB_RESULT, st.max_hold_owner);
}

void test_work_buf_run(void)
{
    RUN_TEST(test_nothing_before_init);
    RUN_TEST(test_init_once);
    RUN_TEST(test_one_owner_at_a_time);
    RUN_TEST(test_only_the_holder_releases);
    RUN_TEST(test_handover);
    RUN_TEST(test_claim_waits_and_wins_over_try_claim);
    RUN_TEST(test_stats);

    work_buf_reset();
}
