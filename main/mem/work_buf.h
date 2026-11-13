/**
 * @file work_buf.h
 *
 * One block of work memory that several modules take turns on.
 *
 * The panel's heap is small and fragments: after the first minute an
 * ArduiTouch with every feature on has 13 to 19 KB free with a largest block
 * of 6 to 13 KB, and every module that asks for a few kilobytes in one piece
 * competes for it. Most of those requests are short -- one HTTP body, one
 * glyph, one decode -- and they do not happen at the same moment. This block
 * is allocated once, at boot, and handed to one owner at a time.
 *
 * Two ways to take it:
 *
 * - work_buf_try_claim() never waits. If the block is in use, or a
 *   work_buf_claim() is waiting for it, the caller uses the heap as before.
 *   This is the only way for code that must not stall, such as the renderer.
 * - work_buf_claim() waits until the block is free. A waiter is served before
 *   any later try_claim(), so a stream of short claims cannot starve it.
 *
 * Rules for every owner:
 *
 * - Hold the block briefly, and never across a wait for something another
 *   owner may hold: two tasks that each wait for what the other holds stop
 *   both.
 * - Give it back on every path, with work_buf_release() or by handing it over.
 * - Only work_buf_claim() may wait. The renderer uses work_buf_try_claim().
 *
 * Owners in use:
 *
 * - WORK_BUF_OPENHAB_READ: the openHAB client reads a page, sitemap list or
 *   server icon into it, and hands it over to WORK_BUF_OPENHAB_RESULT.
 * - WORK_BUF_OPENHAB_RESULT: that result, while it waits in the queue and
 *   while the UI parses or decodes it; released by
 *   openhab_client_result_release().
 * - WORK_BUF_GLYPH: LVGL's glyph buffers, for one label draw (see
 *   ui/ui_glyph_buf.h).
 *
 * Pure -- libc and C11 atomics only -- so the host tests can reach it.
 */
#ifndef WORK_BUF_H
#define WORK_BUF_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum work_buf_owner_e
{
    WORK_BUF_FREE = 0,
    WORK_BUF_OPENHAB_READ,
    WORK_BUF_OPENHAB_RESULT,
    WORK_BUF_GLYPH,
    WORK_BUF_OWNER_COUNT
};

struct work_buf_stats_s
{
    uint32_t claims;      /* times the block was taken, both ways      */
    uint32_t busy;        /* work_buf_try_claim() calls that got NULL  */
    uint32_t max_hold_ms; /* longest time one owner held the block     */
    enum work_buf_owner_e max_hold_owner;
};

/**
 * Allocate the block. Once; a second call returns false and changes nothing.
 *
 * @param size    bytes
 * @param now_ms  a monotonic millisecond clock, for the hold times
 * @param yield   what work_buf_claim() calls while it waits; on the device one
 *                tick of vTaskDelay()
 * @return        false if the block could not be allocated
 */
bool work_buf_init(size_t size, uint32_t (*now_ms)(void), void (*yield)(void));

/** The block's size, 0 before work_buf_init(). */
size_t work_buf_size(void);

/** The block if it was free and nobody waits for it, else NULL. */
void *work_buf_try_claim(enum work_buf_owner_e owner);

/** The block, after waiting for it to be free. NULL only before work_buf_init(). */
void *work_buf_claim(enum work_buf_owner_e owner);

/**
 * Pass the block from one owner to the next without letting it go free in
 * between. False, and nothing changes, if `from` does not hold it.
 */
bool work_buf_handover(enum work_buf_owner_e from, enum work_buf_owner_e to);

/** Give the block back. False, and nothing changes, if `owner` does not hold it. */
bool work_buf_release(enum work_buf_owner_e owner);

/** Who holds the block now. */
enum work_buf_owner_e work_buf_owner(void);

/** Whether `p` is the start of the block. */
bool work_buf_is(const void *p);

void work_buf_stats(struct work_buf_stats_s *out);

/** For the host tests: free the block and the counters, so the next test
 *  starts from scratch. Not for use while anyone holds it. */
void work_buf_reset(void);

#ifdef __cplusplus
}
#endif

#endif /* WORK_BUF_H */
