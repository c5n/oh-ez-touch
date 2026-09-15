/**
 * @file beeper_common.h
 *
 * What both beeper engines and the port layer agree on, and nothing else.
 *
 * There are two engines -- see CONFIG_OHEZ_BEEPER_ENGINE -- and they disagree
 * about almost everything: what a note is, how many sound at once, what a frame
 * means. They agree about three things, and those three are exactly the ones
 * the port layer needs: what a level is, what the transducer's good band is,
 * and what a programmed tone looks like.
 *
 * Splitting them out is what lets port_beeper_tone() stop knowing which engine
 * is compiled in. Only port_beeper_render*() still does, and that is one
 * prototype per engine rather than a whole header.
 *
 * Same rule as beeper_mixer.h and beeper_seq.h: nothing but <stdint.h>, no
 * clock, no hardware. Every caller of those two walks this one.
 */
#ifndef BEEPER_COMMON_H
#define BEEPER_COMMON_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The level scale between an engine and the port layer, per mille rather than
 * per cent for a reason: the shipped master volume is 25, and on a 0..100 scale
 * that would leave a whole envelope twelve steps tall where a decay wants
 * fifty. */
#define BEEPER_LEVEL_MAX 1000

/* The band a small piezo is actually loud in. Advisory here -- the alert sounds
 * break it deliberately -- and enforced by the table tests, which know which
 * sounds are allowed to. */
#define BEEPER_BAND_LO_HZ 1000
#define BEEPER_BAND_HI_HZ 4000

/* What an engine hands the port layer: one tone, for a stated number of
 * milliseconds. Under the mixer this is one voice's turn in an interleaved
 * frame; under the sequencer it is the whole frame. */
struct beeper_slot_s
{
    uint16_t freq;
    uint16_t level; /* 0..BEEPER_LEVEL_MAX */
};

/* A note's 0..255 level scaled by the 0..100 master, in 0..BEEPER_LEVEL_MAX.
 *
 * Shared on purpose rather than duplicated per engine: this is the function
 * test_the_shipped_defaults_land_on_the_duty_they_always_did pins to sixty-three
 * counts of duty, and that promise is about the *panel*, not about one way of
 * arranging its notes. Swapping engines must not change how loud it is. */
uint16_t beeper_level_permille(uint8_t level, uint8_t master);

#ifdef __cplusplus
}
#endif

#endif /* BEEPER_COMMON_H */
