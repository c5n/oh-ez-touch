/**
 * @file port_sys.h
 *
 * Time, restart, heap and wall-clock. This is what is left of hal/sdl2/Arduino.h
 * once String and Serial are gone.
 *
 * Note what is *not* here: strlcpy(). hal/sdl2/Arduino.h carried a fallback for
 * glibc < 2.38, and it is not needed on either target -- IDF's newlib provides
 * strlcpy on the device, and on the host IDF's own components/linux/linux_include/
 * string.h pulls in <bsd/string.h> (which is why libbsd-dev is a hard build
 * requirement, not a nicety).
 */
#ifndef PORT_SYS_H
#define PORT_SYS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Milliseconds since boot, monotonic.
 *
 * 64 bit on purpose. Arduino's millis() returned `unsigned long`, which is 32
 * bit on the device and 64 bit on the simulator host, so the deadline idiom
 * `(long)(millis() - deadline) >= 0` that src/ uses today means two different
 * things on the two targets: wrap-safe arithmetic on one, a plain comparison on
 * the other. With a 64-bit value the idiom becomes a plain `>=` everywhere and
 * the difference disappears -- 2^64 ms is 584 million years.
 *
 * This is also the uptime: nothing here resets it, which is why there is no
 * separate port_uptime(). It replaces the Uptime library, whose entire job was
 * tracking the 32-bit rollover this does not have.
 */
uint64_t port_millis(void);

/** Microseconds since boot, monotonic. Same contract as port_millis(). */
uint64_t port_micros(void);

/**
 * LVGL's tick source, narrowed to what lv_tick_get_cb_t returns.
 *
 * Kept next to port_millis() rather than in the display port because LVGL needs
 * it before any display exists, and because both targets answer it the same
 * way. Wraps every 49 days on both, identically.
 */
uint32_t port_tick_ms(void);

/** Reboot. Does not return. */
void port_restart(void) __attribute__((noreturn));

/**
 * Free heap in bytes, for the Info tab and the web status page.
 *
 * Not comparable between targets: on the device this is the real, bounded
 * figure that decides whether an icon decode succeeds, while on the host it is
 * whatever the allocator happens not to have handed back to the kernel. Display
 * it; do not make decisions on it.
 */
size_t port_free_heap(void);

/**
 * The largest single block the heap could still hand out, or 0 where that is
 * not knowable.
 *
 * Free heap on its own cannot tell a tired heap from a full one, and the
 * difference is the whole of what goes wrong on a panel that has been up for
 * days: a page fetch needs its buffer in one piece, and a heap with 60 KB free
 * in scraps refuses it while a heap with 20 KB free in one run gives it.
 * Fragmentation *is* the gap between these two numbers, so the pair is
 * reported wherever the single figure used to be.
 *
 * 0 means "this target cannot say", which is the host: glibc has no such
 * accessor. Display it as unknown rather than as none.
 */
size_t port_largest_free_block(void);

/**
 * The heap's shape, for the test interface's `heap` command.
 *
 * The pair free/largest is the same as the two accessors above; the block
 * counts are what tells a leak apart from fragmentation when the two move
 * together -- a leak loses free bytes with no new free blocks, fragmentation
 * keeps the bytes and multiplies the pieces. min_free_ever is the high-water
 * mark of harm done since boot, which a spot reading of `free` cannot see.
 *
 * Fields a target cannot answer are 0, which on the host is everything but
 * the first two.
 */
struct port_heap_info_s
{
    size_t   free;          /* bytes free now                                   */
    size_t   largest;       /* biggest single block still available, 0: unknown */
    size_t   min_free_ever; /* lowest `free` has ever been, 0: unknown          */
    unsigned alloc_blocks;  /* live allocated blocks, 0: unknown                */
    unsigned free_blocks;   /* separate free pieces, 0: unknown                 */
};

void port_heap_info(struct port_heap_info_s *out);

/**
 * Local wall-clock time, or false if it is not known yet.
 *
 * Replaces Arduino's getLocalTime(). The failure case is load-bearing:
 * openhab_ui.cpp keeps the theme variant in effect while this returns false,
 * rather than jumping into the night window because the clock still reads 1970.
 * So the device must report failure until NTP has actually synchronised, and
 * the host -- whose clock is set by the OS -- always succeeds.
 */
bool port_localtime(struct tm *out);

#ifdef __cplusplus
}
#endif

#endif /* PORT_SYS_H */
