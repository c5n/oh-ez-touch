/**
 * @file beeper_seq.h
 *
 * What a tune is, and the arithmetic that turns one into a tone on a pin.
 *
 * The default engine -- see CONFIG_OHEZ_BEEPER_ENGINE. Monophonic, and that is
 * the trade rather than a shortfall: this panel has one tone available at a
 * time, and the other engine spends it on three voices interleaved at two
 * milliseconds each, which buys a real chord and pays a grain, a kilohertz
 * floor and five hundred timer writes a second for it. This one spends the
 * whole tone on one note and buys expression with what it saves -- an envelope
 * that is not one of three fixed shapes, a glide that is not linear in hertz, a
 * vibrato, a tremolo, and a note that can repeat.
 *
 * Deliberately free of every dependency -- no FreeRTOS, no esp_log, no port
 * layer, nothing but <stdint.h> and beeper_common.h, which is the same rule
 * again. Three callers need it and they have nothing else in common:
 * beeper_control.cpp walks it from a task on the device, the simulator's
 * port_beeper.c walks it from an SDL audio callback, and test/host walks it
 * from Unity with no clock at all. Every function here is pure, and that is a
 * requirement rather than a property -- see beeper_seq_frame().
 *
 * "Pure" here means no *mutable* file scope. The envelope and effect tables in
 * beeper_seq.c are `static const` and are read by everything; that is what the
 * mixer's gain_q8[] already is, and it is the point of them being a table.
 *
 * -------------------------------------------------------------- the envelope
 *
 * Four stages -- attack, decay, sustain, release -- and the note's duration is
 * the budget, not an event that arrives. There is no key-off on a piezo: a
 * chime's length is known before it starts, so the release is the tail at the
 * end of the note rather than a reaction to anything.
 *
 * The three stage lengths are PERCENTAGES OF THE NOTE with a cap in
 * milliseconds, and that pair is the whole reason one table describes the whole
 * interface. A twelve-millisecond contact tick and a four-hundred-millisecond
 * boot note have to sound like the same instrument; a fixed five-millisecond
 * attack is a sixth of the first and an eightieth of the second, so they would
 * not. The cap is the other end of the same problem: sixty per cent of four
 * hundred milliseconds is a two-hundred-and-forty-millisecond fade-in, which is
 * not a swell, it is a mistake.
 *
 * The three shapes the mixer had are three rows of this table and nothing is
 * lost. The PAD's peak here is a true 255 rather than the 254 the mixer's two
 * halves meet at -- that artefact is pinned by a mixer test on purpose, and
 * nothing audible turns on one part in 255, so the two engines are allowed to
 * differ by it rather than one being bent to match the other.
 *
 * --------------------------------------------------------------- the effects
 *
 * Three, chosen because they are what a square wave on a resonant transducer
 * can actually express, and no more:
 *
 *   SWEEP    is f_start -> f_end over the note, either linear in hertz (what
 *            the mixer did) or as a GLIDE that interpolates the *period*. A
 *            linear sweep in hertz spends most of its time at the top: 1200 to
 *            2800 crosses its first octave in a third of the note. The glide is
 *            geometric to within a per cent, costs one divide, and is also what
 *            the hardware would give you -- LEDC's frequency is a divider, so a
 *            linear walk of the divider is exactly this.
 *
 *   VIBRATO  is a pitch LFO, depth in PER MILLE OF THE CARRIER rather than in
 *            hertz, so one preset row behaves the same at 1.2 kHz and at
 *            3.9 kHz. A fixed excursion in hertz would be a siren at the bottom
 *            of the band and a shimmer at the top, and every row would need a
 *            twin. It is also the one thing the mixer explicitly could not do:
 *            its header says two voices a few hertz apart do not beat, because
 *            phase is not carried across a slot boundary.
 *
 *   TREMOLO  is an amplitude LFO, and it only ever goes DOWN. Upward tremolo
 *            would push a note past the peak its envelope asked for, and the
 *            master volume has no headroom above 100 to give it back.
 *
 * Both LFOs are triangles and both restart at the start of every note. A
 * triangle because at five to twenty hertz through a piezo it is
 * indistinguishable from a sine, and a sine costs either a table in flash or a
 * float in a function that is walked at audio rate. Restarting per note because
 * the alternative is a trill whose three strikes each catch the vibrato at a
 * different place -- and because a phase that ran across notes would have to be
 * carried in an argument or hidden in a static, and the second of those is
 * forbidden here.
 */
#ifndef BEEPER_SEQ_H
#define BEEPER_SEQ_H

#include "beeper_common.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The resolution an envelope, a sweep and an LFO are all sampled at.
 *
 * Five milliseconds, the same step the mixer's monophonic frame used, and kept
 * rather than shortened: it is fine enough that a swept note is heard as a
 * slide, and it is two hundred wakeups a second rather than the five hundred an
 * interleaved chord costs -- so the default engine is cheaper than the optional
 * one even with an LFO running. BEEPER_SEQ_LFO_MAX_CHZ is what keeps that
 * true; see there. */
#define BEEPER_SEQ_STEP_MS 5

/* The fastest LFO any preset may ask for, in centihertz.
 *
 * Two jobs. It keeps every shipped LFO at eight or more samples per cycle on
 * the step above, so the simulator and the panel hear the same wobble rather
 * than the simulator hearing a smooth one and the panel a stepped one. And it
 * bounds beeper_seq_lfo()'s multiply: elapsed is at most a note's duration in a
 * uint16_t, and 65535 * 2500 is comfortably inside a uint32_t where
 * 65535 * 65535 is not. A host test asserts no preset exceeds it. */
#define BEEPER_SEQ_LFO_MAX_CHZ 2500

/* Advisory, and enforced on the tables rather than here: a sequence longer than
 * this is a jingle, not a chime, and the queue plays them serially. */
#define BEEPER_SEQ_NOTES_MAX 16

/* How a note gets from f_start to f_end. */
enum beeper_seq_sweep_e
{
    BEEPER_SEQ_SWEEP_LINEAR = 0, /* linear in hertz; what the mixer did      */
    BEEPER_SEQ_SWEEP_GLIDE,      /* linear in period; geometric, near enough */
    BEEPER_SEQ_SWEEP_COUNT
};

/* The envelope vocabulary. Rows, not code: beeper_seq_env_preset() indexes them
 * and there is no switch anywhere, which is what stops a new row needing an
 * edit in three places -- and what keeps an out-of-range index out of a
 * -Wswitch that could not have caught it anyway. */
enum beeper_seq_env_e
{
    BEEPER_SEQ_ENV_FLAT = 0, /* gated on and off. Clicks at both ends, and
                              * sometimes that is exactly the sound wanted    */
    BEEPER_SEQ_ENV_CLICK,    /* FLAT with the click taken off the end only    */
    BEEPER_SEQ_ENV_PLUCK,    /* instant, then away: what a struck thing does  */
    BEEPER_SEQ_ENV_PAD,      /* eased in and out; a swell                     */
    BEEPER_SEQ_ENV_STAB,     /* an edge, a drop, a short hold                 */
    BEEPER_SEQ_ENV_BELL,     /* instant, long decay, no click at the end      */
    BEEPER_SEQ_ENV_SWELL,    /* slow in, slow out; nothing has an edge        */
    BEEPER_SEQ_ENV_BLOOM,    /* a soft arrival that settles and stays         */
    BEEPER_SEQ_ENV_COUNT
};

/* The effect vocabulary, same idea. */
enum beeper_seq_fx_e
{
    BEEPER_SEQ_FX_NONE = 0,  /* a linear sweep and nothing else               */
    BEEPER_SEQ_FX_GLIDE,     /* the same, swept through the period            */
    BEEPER_SEQ_FX_SHIMMER,   /* a slow shallow vibrato                        */
    BEEPER_SEQ_FX_WOBBLE,    /* a fast deep one: unease                       */
    BEEPER_SEQ_FX_SIREN,     /* slow and very deep: an alarm                  */
    BEEPER_SEQ_FX_BREATHE,   /* a soft tremolo                                */
    BEEPER_SEQ_FX_PULSE,     /* a hard one; a note with a grain on it         */
    BEEPER_SEQ_FX_CHIRP,     /* a glide with a hard tremolo over it           */
    BEEPER_SEQ_FX_COUNT
};

/* One stage set, shared by every note that names it.
 *
 * `sustain` is a fraction of the note's peak in 0..255, not a percentage: the
 * envelope is already in 0..255 everywhere else and a second scale would be one
 * conversion nobody remembers. A `*_max_ms` of 0 means uncapped. */
struct beeper_seq_env_s
{
    uint8_t  attack_pct;
    uint8_t  decay_pct;
    uint8_t  release_pct;
    uint8_t  sustain;
    uint16_t attack_max_ms;
    uint16_t release_max_ms;
};

/* One effect set. Eight bytes, and a rate of 0 is what switches its LFO off --
 * so a zero-filled row is FX_NONE, which is the failure that does the least
 * damage. */
struct beeper_seq_fx_s
{
    uint16_t vib_rate_chz;  /* centihertz: 650 is 6.5 Hz. Integer hertz is too
                             * coarse for a vibrato -- 5 and 6 are audibly
                             * different wobbles -- and this fits a uint16_t
                             * with three orders of magnitude to spare.       */
    uint16_t trem_rate_chz;
    uint8_t  vib_depth;     /* +/- this many per mille of the carrier        */
    uint8_t  trem_depth;    /* how far DOWN the level dips, 0..255 of it     */
    uint8_t  sweep;         /* enum beeper_seq_sweep_e                       */
    uint8_t  reserved;      /* 0. Named rather than implicit so a test can say
                             * so, and so a later field cannot inherit
                             * garbage from an under-specified initialiser.  */
};

/* One note: twelve bytes, no padding, every byte named.
 *
 * `volume` is this note's level against the others of its tune -- the balance,
 * written into the table by whoever composed it. The config setting is a master
 * on top, applied in beeper_level_permille(). There is deliberately no second
 * per-note scalar; two places to look when a tune is unbalanced is one too
 * many.
 *
 * `repeat` is the byte this struct would have padded with. 0 and 1 both mean
 * once, on purpose: a note left half-written by a table that forgot a field
 * should be *audible*, because silent is the failure mode nobody notices. It
 * repeats the whole note including its pause, and the envelope and both LFOs
 * restart on each pass -- a trill is three strikes, not one long note with gaps
 * cut into it. */
struct beeper_seq_note_s
{
    uint16_t f_start;
    uint16_t f_end;      /* == f_start is a steady note        */
    uint16_t duration_ms;
    uint16_t pause_ms;
    uint8_t  volume;     /* 0..100, the peak of the envelope   */
    uint8_t  env;        /* enum beeper_seq_env_e              */
    uint8_t  fx;         /* enum beeper_seq_fx_e               */
    uint8_t  repeat;     /* 0 or 1 is once                     */
};

/* A whole tune, queued as one item.
 *
 * Eight bytes on the device, exactly what a chime was, so the four-deep queue
 * and its by-value send are unchanged. One entry rather than one per note, and
 * that is not only tidiness: a five-note sequence queued note by note would
 * have its tail silently dropped, and this way a tune either plays or does not.
 * Everything it points at must outlive the call, which is what makes a table in
 * flash the natural way to write one. */
struct beeper_seq_s
{
    const struct beeper_seq_note_s *notes;
    uint8_t                         count;
};

/* The preset rows, by index. Never NULL: an index past the end returns row
 * zero, because a tune with a bad index should be a plain note rather than a
 * crash, and the table tests are what catch the index itself. */
const struct beeper_seq_env_s *beeper_seq_env_preset(uint8_t env);
const struct beeper_seq_fx_s  *beeper_seq_fx_preset(uint8_t fx);

/* A triangle in -127..+127, at `rate_chz`, `elapsed_ms` from the note's start.
 * Starts at 0 and rises. Public because both modulators use it and both are
 * tested through it; a rate of 0 is a flat 0, which is how an LFO is off. */
int8_t beeper_seq_lfo(uint32_t elapsed_ms, uint16_t rate_chz);

/* The envelope at `elapsed` into a note of `duration`, in 0..255.
 *
 * `duration` of 0 returns the peak rather than dividing by it, and an `elapsed`
 * at or past the end returns 0 rather than wrapping. */
uint8_t beeper_seq_envelope(uint8_t env, uint32_t elapsed, uint32_t duration);

/* Where this note's pitch and level are, `elapsed` into one pass of it. Sweep
 * and vibrato for the first; envelope, note volume and tremolo for the second,
 * in 0..255 before the master. */
uint16_t beeper_seq_pitch(const struct beeper_seq_note_s *n, uint32_t elapsed);
uint8_t  beeper_seq_amplitude(const struct beeper_seq_note_s *n, uint32_t elapsed);

/* Fill the slot for the frame beginning at `t_ms`.
 *
 * Returns the frame's length in milliseconds, or 0 when the tune is over. A
 * frame with slot->level 0 is a rest one step long, which is what a tune
 * mid-pause looks like, and the caller must program silence rather than treat
 * it as the end.
 *
 * THIS FUNCTION MUST STAY PURE. Not a style preference: the simulator renders a
 * tune by evaluating it at times of its own choosing from an audio callback,
 * the host tests call it out of order on purpose, and a hidden static would
 * make both of those quietly wrong rather than loudly broken. Where the tune
 * has got to comes out of the argument list, never out of a file scope. */
uint16_t beeper_seq_frame(const struct beeper_seq_s *seq, uint32_t t_ms,
                          uint8_t master, struct beeper_slot_s *slot);

/* How long the whole tune lasts: every note's duration and pause, repeats
 * included. */
uint32_t beeper_seq_duration_ms(const struct beeper_seq_s *seq);

/* The lowest and highest frequency anywhere in the tune -- both ends of every
 * sweep AND the full excursion of every vibrato around them. The band test
 * reads this, so a preset deep enough to swing a 3.9 kHz note past the top of
 * the piezo's band is caught there rather than on a panel. Both are 0 for a
 * tune with no notes. */
void beeper_seq_freq_range(const struct beeper_seq_s *seq, uint16_t *lo,
                           uint16_t *hi);

/* How many notes this tune strikes, repeats expanded. For the table tests,
 * which check that nothing runs away. */
uint32_t beeper_seq_note_count(const struct beeper_seq_s *seq);

#ifdef __cplusplus
}
#endif

#endif /* BEEPER_SEQ_H */
