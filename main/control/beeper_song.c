/**
 * @file beeper_song.c
 *
 * See beeper_song.h. Thirty seconds of one piezo, in seven movements, each of
 * which exists to make one part of the engine audible. A host test pins the
 * length, because the movement headings below give theirs and a table nobody
 * re-measured would drift away from them silently.
 *
 * -------------------------------------------------------------- what for
 *
 * beeper_seq.h describes eight envelopes and eight effects in prose, and prose
 * is the wrong medium for it: nobody can hear the difference between STAB and
 * PLUCK by reading that one is "an edge, a drop, a short hold" and the other is
 * "instant, then away". The themed families each use a handful of the rows and
 * never put two of them side by side, so the only way to compare was to edit a
 * table, rebuild and press something.
 *
 * So the ordering here is not musical, it is didactic: the same figure is
 * repeated through every envelope in turn, then held through every effect in
 * turn, and the two sweeps are played back to back on the same interval because
 * the whole point of GLIDE is that it is *not* the other one. What frames it at
 * both ends is a tune, because half a minute of test tones is not something
 * anybody listens to twice.
 *
 * ------------------------------------------------------------- the rules
 *
 * Every constraint the themed tables obey applies here too, except the two that
 * are about being a chime rather than about being sound -- the note count and
 * the seven-hundred-millisecond ceiling. The rest are real:
 *
 *   - Between 1 and 4 kHz, INCLUDING what a vibrato swings a note to. SIREN is
 *     +/- 120 per mille, so a note carrying it may not be written above about
 *     3.5 kHz, and this is invisible in the table -- every number in a row can
 *     be inside the band while what the panel emits is not.
 *
 *   - A note carrying an LFO is at least one period of it long. SHIMMER needs
 *     84 ms, WOBBLE 67, SIREN 250, BREATHE 200, PULSE 63 and CHIRP 50. An
 *     ornament that does not complete a cycle is a pitch bend, and the whole
 *     movement below exists to demonstrate the ornament rather than the bend.
 *
 *   - No note shorter than one 5 ms step.
 *
 * test/host walks all of it, the same way it walks the themed families.
 *
 * -------------------------------------------------------------- the pitches
 *
 * G major, from G6 up, because the piezo's useful band is about 1 to 4 kHz and
 * that puts a two-octave scale inside it with room at both ends for a vibrato
 * to swing into. Nothing here is transposed; the notes are equal-tempered to
 * the nearest hertz and the table says which is which.
 */
#include "sdkconfig.h"

#if CONFIG_OHEZ_BEEPER_ENGINE_SEQ

#include "beeper_song.h"

/* The same spelling the themed tables use, so that the two read alike:
 * f_start, f_end, duration, pause, volume, envelope, effect. */
#define N(fs, fe, d, p, v, e, x)                                               \
    {                                                                          \
        (uint16_t)(fs), (uint16_t)(fe), (uint16_t)(d), (uint16_t)(p),          \
            (uint8_t)(v), (uint8_t)(e), (uint8_t)(x), 1                        \
    }

#define NR(fs, fe, d, p, v, e, x, r)                                           \
    {                                                                          \
        (uint16_t)(fs), (uint16_t)(fe), (uint16_t)(d), (uint16_t)(p),          \
            (uint8_t)(v), (uint8_t)(e), (uint8_t)(x), (uint8_t)(r)             \
    }

#define T(f, d, p, v, e) N(f, f, d, p, v, e, NONE)

#define FLAT    BEEPER_SEQ_ENV_FLAT
#define CLICK   BEEPER_SEQ_ENV_CLICK
#define PLUCK   BEEPER_SEQ_ENV_PLUCK
#define PAD     BEEPER_SEQ_ENV_PAD
#define STAB    BEEPER_SEQ_ENV_STAB
#define BELL    BEEPER_SEQ_ENV_BELL
#define SWELL   BEEPER_SEQ_ENV_SWELL
#define BLOOM   BEEPER_SEQ_ENV_BLOOM

#define NONE    BEEPER_SEQ_FX_NONE
#define GLIDE   BEEPER_SEQ_FX_GLIDE
#define SHIMMER BEEPER_SEQ_FX_SHIMMER
#define WOBBLE  BEEPER_SEQ_FX_WOBBLE
#define SIREN   BEEPER_SEQ_FX_SIREN
#define BREATHE BEEPER_SEQ_FX_BREATHE
#define PULSE   BEEPER_SEQ_FX_PULSE
#define CHIRP   BEEPER_SEQ_FX_CHIRP

/* G major from G6. Named rather than spelled out, because the movements below
 * are about envelopes and effects and a bare 2637 in the middle of one is a
 * number to decode rather than a note to read. */
#define G6  1568
#define A6  1760
#define B6  1976
#define C7  2093
#define D7  2349
#define E7  2637
#define Fs7 2960
#define G7  3136
#define A7  3520
#define B7  3951

/* The level a note carries against the others of the piece. The config's master
 * volume is on top of this, as everywhere else -- so the piece is as loud as
 * the panel is set to be, and the sixth movement below is about the range
 * *within* that, not about overriding it. */
#define P   30 /* quiet   */
#define MF  55 /* the working level of the piece */
#define F   75 /* loud    */

static const struct beeper_seq_note_s song_notes[] = {

    /* ---- I. Statement ------------------------------------------- 2.9 s ----
     *
     * A tune, so that what follows is heard as a demonstration inside a piece
     * of music rather than as a diagnostic. PLUCK against BELL: the two
     * envelopes the themed families lean on hardest, and the pair whose
     * difference -- a decay that ends at nothing against one that is still
     * ringing when the next note starts -- is the easiest to hear cold. */
    T(G6,  180,  20, MF, PLUCK),
    T(A6,  180,  20, MF, PLUCK),
    T(C7,  240,  20, MF, BELL),
    T(B6,  180,  20, MF, PLUCK),
    T(G6,  400,  80, MF, BELL),

    T(C7,  180,  20, MF, PLUCK),
    T(D7,  180,  20, MF, PLUCK),
    T(E7,  240,  20, MF, BELL),
    T(D7,  180,  20, MF, PLUCK),
    T(B6,  500, 220, MF, BELL),

    /* ---- II. The eight envelopes -------------------------------- 5.4 s ----
     *
     * One figure -- a short note and a long one a fifth above it -- read eight
     * times, once per row of the envelope table, in the order they are declared.
     * Identical pitches, identical lengths, identical level: the only thing
     * that changes is the shape, which is the only way to hear a shape.
     *
     * FLAT clicks at both ends and is supposed to. CLICK takes the click off
     * the end only, which is the difference to listen for, and it is a small
     * one. PLUCK and STAB both start instantly and part company after that.
     * PAD, SWELL and BLOOM all ease in, and on a 120 ms note SWELL's 60 per
     * cent attack is most of it -- which is the cap in beeper_seq.h being
     * demonstrated rather than a mistake. */
    T(B6,  120,  15, MF, FLAT),  T(Fs7, 250, 280, MF, FLAT),
    T(B6,  120,  15, MF, CLICK), T(Fs7, 250, 280, MF, CLICK),
    T(B6,  120,  15, MF, PLUCK), T(Fs7, 250, 280, MF, PLUCK),
    T(B6,  120,  15, MF, STAB),  T(Fs7, 250, 280, MF, STAB),
    T(B6,  120,  15, MF, BELL),  T(Fs7, 250, 280, MF, BELL),
    T(B6,  120,  15, MF, PAD),   T(Fs7, 250, 280, MF, PAD),
    T(B6,  120,  15, MF, SWELL), T(Fs7, 250, 280, MF, SWELL),
    T(B6,  120,  15, MF, BLOOM), T(Fs7, 250, 380, MF, BLOOM),

    /* ---- III. The two sweeps ------------------------------------ 3.9 s ----
     *
     * The same rise, an octave and a fourth, twice: first linear in hertz, then
     * as a GLIDE that walks the period. They are the same two endpoints and
     * they do not sound alike -- the linear one crosses its first octave in a
     * third of the note and then crawls, which is the entire reason GLIDE
     * exists and is the hardest claim in beeper_seq.h to take on trust.
     *
     * Then the same glide downwards, and then CHIRP, which is that plus a hard
     * tremolo. */
    N(1200, 3200, 700, 140, MF, PAD,  NONE),
    N(1200, 3200, 700, 140, MF, PAD,  GLIDE),
    N(3200, 1200, 700, 200, MF, PAD,  GLIDE),
    N(1400, 3600, 420, 160, MF, STAB, CHIRP),
    N(3600, 1400, 420, 320, MF, STAB, CHIRP),

    /* ---- IV. The five ornaments --------------------------------- 5.6 s ----
     *
     * One held pitch, five effect rows. Every note here is comfortably longer
     * than one period of the LFO it carries, which is the floor the host test
     * enforces and the reason this movement is the slowest of the seven.
     *
     * SHIMMER and WOBBLE are the same mechanism a factor of four apart in depth
     * -- an ornament against unease. SIREN is deeper again and slow enough to
     * be heard as pitch rather than as texture, and it is written at C7 rather
     * than up at G7 because +/- 120 per mille of 3136 is outside the band. The
     * last two are the amplitude side: BREATHE dips softly, PULSE hard, and
     * neither ever goes *up*, because there is no headroom above the peak the
     * envelope asked for. */
    N(C7, C7,  850, 170, MF, PAD, SHIMMER),
    N(C7, C7,  850, 170, MF, PAD, WOBBLE),
    N(C7, C7, 1050, 170, MF, PAD, SIREN),
    N(D7, D7,  950, 170, MF, PAD, BREATHE),
    N(D7, D7,  850, 340, MF, PAD, PULSE),

    /* ---- V. Repeats and runs ------------------------------------ 3.5 s ----
     *
     * `repeat` is the field that makes a trill one row instead of six, and the
     * thing to hear is that it really is six strikes: the envelope and both
     * LFOs restart on every pass, so this is not one long note with gaps cut
     * into it. Three trills, getting shorter and higher, and then a scale run
     * up and back to show the engine doing ordinary quick notes. */
    NR(E7, E7,  60,  40, MF, PLUCK, NONE, 6),
    NR(G7, G7,  45,  30, MF, STAB,  NONE, 4),
    NR(B6, B6,  90,  60, MF, CLICK, NONE, 3),

    T(G6,  85,  10, P,  PLUCK),
    T(A6,  85,  10, P,  PLUCK),
    T(B6,  85,  10, MF, PLUCK),
    T(C7,  85,  10, MF, PLUCK),
    T(D7,  85,  10, MF, PLUCK),
    T(E7,  85,  10, F,  PLUCK),
    T(Fs7, 85,  10, F,  PLUCK),
    T(G7,  85,  10, F,  PLUCK),
    T(A7,  85,  10, F,  PLUCK),
    T(B7, 180,  90, F,  BELL),

    /* And back down. B7 is the top of the piece and the one note written above
     * 3.5 kHz -- it carries no effect, because there is no room left between it
     * and the top of the band for one to swing into. */
    T(A7,  85,  10, F,  PLUCK),
    T(G7,  85,  10, F,  PLUCK),
    T(Fs7, 85,  10, MF, PLUCK),
    T(E7,  85,  10, MF, PLUCK),
    T(D7,  85,  10, MF, PLUCK),
    T(C7,  85,  10, P,  PLUCK),
    T(B6,  85,  10, P,  PLUCK),
    T(G6, 220, 160, MF, BELL),

    /* ---- VI. Dynamics ------------------------------------------- 2.1 s ----
     *
     * One pitch, one envelope, nine levels: up from a whisper and back down.
     * This is the per-note `volume` field on its own, with everything else held
     * still -- and it is worth hearing on a panel rather than here, because
     * duty cycle is loudness only roughly and a piezo's response to it is not a
     * straight line. */
    T(C7, 180, 30, 10,  BELL),
    T(C7, 180, 30, 22,  BELL),
    T(C7, 180, 30, 36,  BELL),
    T(C7, 180, 30, 52,  BELL),
    T(C7, 180, 30, 100, BELL),
    T(C7, 180, 30, 52,  BELL),
    T(C7, 180, 30, 36,  BELL),
    T(C7, 180, 30, 22,  BELL),
    T(C7, 180, 240, 10, BELL),

    /* ---- VII. Finale -------------------------------------------- 6.5 s ----
     *
     * The opening figure again, rising through two octaves this time, and then
     * everything at once: a glide up into a shimmering held note, a bloom, and
     * a last one left to ring. Which is also the answer to "what is all this
     * for" -- the movements above are a list, and this is what a list is for. */
    T(G6,  160,  15, MF, PLUCK),
    T(B6,  160,  15, MF, PLUCK),
    T(D7,  160,  15, MF, PLUCK),
    T(G7,  240,  20, F,  BELL),

    T(A6,  160,  15, MF, PLUCK),
    T(C7,  160,  15, MF, PLUCK),
    T(E7,  160,  15, MF, PLUCK),
    T(A7,  240, 120, F,  BELL),

    N(1200, G7, 700,  60, MF, SWELL, GLIDE),
    N(A7,   A7, 450,  40, F,  PAD,   SHIMMER),
    N(G7,   G7, 450,  40, F,  BLOOM, SHIMMER),
    N(D7,   D7, 450,  40, MF, BLOOM, SHIMMER),
    N(B6,   B6, 500,  40, MF, BLOOM, SHIMMER),
    N(G6,   G6, 2100,  0, F,  BLOOM, SHIMMER),
};

const struct beeper_seq_s *beeper_song(void)
{
    /* Built once, in flash, and handed out by pointer: beeper_play_seq() copies
     * the eight bytes onto the queue and the notes stay here. */
    static const struct beeper_seq_s song = {
        song_notes, (uint8_t)(sizeof(song_notes) / sizeof(song_notes[0]))};

    return &song;
}

#endif /* CONFIG_OHEZ_BEEPER_ENGINE_SEQ */
