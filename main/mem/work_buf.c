#include "work_buf.h"

#include <stdatomic.h>
#include <stdlib.h>

static uint8_t *block;
static size_t   block_size;
static uint32_t (*clock_ms)(void);
static void     (*wait_tick)(void);

/* Who holds the block. Every change is one compare-and-swap, so two tasks
 * that try at the same moment cannot both win. */
static _Atomic int owner = WORK_BUF_FREE;

/* work_buf_claim() calls waiting now. While it is not zero, try_claim() steps
 * aside, so a waiter is served before the next short claim. */
static atomic_uint waiters;

/* Written by the holder only, so one claim at a time touches them. */
static uint32_t claimed_at;
static uint32_t max_hold_ms;
static int      max_hold_owner = WORK_BUF_FREE;

static atomic_uint claims;
static atomic_uint busy;

static void hold_start(void)
{
    claimed_at = (clock_ms != NULL) ? clock_ms() : 0;
}

static void hold_end(enum work_buf_owner_e who)
{
    if (clock_ms == NULL)
        return;

    uint32_t held = clock_ms() - claimed_at;

    if (held > max_hold_ms)
    {
        max_hold_ms = held;
        max_hold_owner = who;
    }
}

bool work_buf_init(size_t size, uint32_t (*now_ms)(void), void (*yield)(void))
{
    if (block != NULL || size == 0)
        return false;

    block = (uint8_t *)malloc(size);

    if (block == NULL)
        return false;

    block_size = size;
    clock_ms = now_ms;
    wait_tick = yield;

    return true;
}

size_t work_buf_size(void)
{
    return block_size;
}

void *work_buf_try_claim(enum work_buf_owner_e who)
{
    if (block == NULL || who == WORK_BUF_FREE)
        return NULL;

    int expected = WORK_BUF_FREE;

    if (atomic_load(&waiters) != 0
        || atomic_compare_exchange_strong(&owner, &expected, who) == false)
    {
        atomic_fetch_add(&busy, 1);
        return NULL;
    }

    atomic_fetch_add(&claims, 1);
    hold_start();

    return block;
}

void *work_buf_claim(enum work_buf_owner_e who)
{
    if (block == NULL || who == WORK_BUF_FREE)
        return NULL;

    atomic_fetch_add(&waiters, 1);

    for (;;)
    {
        int expected = WORK_BUF_FREE;

        if (atomic_compare_exchange_strong(&owner, &expected, who) == true)
            break;

        if (wait_tick != NULL)
            wait_tick();
    }

    atomic_fetch_sub(&waiters, 1);
    atomic_fetch_add(&claims, 1);
    hold_start();

    return block;
}

bool work_buf_handover(enum work_buf_owner_e from, enum work_buf_owner_e to)
{
    if (from == WORK_BUF_FREE || to == WORK_BUF_FREE || atomic_load(&owner) != from)
        return false;

    /* The hold times are per owner: the result's wait in the queue is its
     * own entry, not the read's. */
    hold_end(from);

    int expected = from;

    if (atomic_compare_exchange_strong(&owner, &expected, to) == false)
        return false;

    hold_start();

    return true;
}

bool work_buf_release(enum work_buf_owner_e who)
{
    if (who == WORK_BUF_FREE || atomic_load(&owner) != who)
        return false;

    hold_end(who);

    int expected = who;

    return atomic_compare_exchange_strong(&owner, &expected, WORK_BUF_FREE);
}

enum work_buf_owner_e work_buf_owner(void)
{
    return (enum work_buf_owner_e)atomic_load(&owner);
}

bool work_buf_is(const void *p)
{
    return block != NULL && p == block;
}

void work_buf_stats(struct work_buf_stats_s *out)
{
    out->claims = atomic_load(&claims);
    out->busy = atomic_load(&busy);
    out->max_hold_ms = max_hold_ms;
    out->max_hold_owner = (enum work_buf_owner_e)max_hold_owner;
}

void work_buf_reset(void)
{
    free(block);
    block = NULL;
    block_size = 0;
    clock_ms = NULL;
    wait_tick = NULL;
    atomic_store(&owner, WORK_BUF_FREE);
    atomic_store(&waiters, 0);
    atomic_store(&claims, 0);
    atomic_store(&busy, 0);
    claimed_at = 0;
    max_hold_ms = 0;
    max_hold_owner = WORK_BUF_FREE;
}
