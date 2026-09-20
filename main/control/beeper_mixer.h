/**
 * @file beeper_mixer.h
 *
 * What a chime is, and the arithmetic that turns one into a tone on a pin.
 *
 * One of the two beeper engines -- see CONFIG_OHEZ_BEEPER_ENGINE -- and the
 * optional one. The default is beeper_seq.h, which spends the same single tone
 * on one expressive note instead of three interleaved ones. What both of them
 * and the port layer agree on lives in beeper_common.h.
 *
 * Deliberately free of every dependency -- no FreeRTOS, no esp_log, no port
 * layer, nothing but <stdint.h> and the header just named, which is the same
 * rule again. Three callers need it and they have nothing else in common: beeper_control.cpp walks it from a task on the device, the
 * simulator's port_beeper.c walks it from an SDL audio callback at sample
 * resolution, and test/host walks it from Unity with no clock at all. Every
 * function here is pure, and that is a requirement rather than a property --
 * see beeper_mixer_frame().
 *
 * ---------------------------------------------------------------- polyphony
 *
 * The panel has one piezo on one GPIO, driven by one LEDC timer, and the
 * frequency is a property of the timer. So there is exactly one tone available
 * at a time and a chord has to be *interleaved*: each voice holds the channel
 * for a slot, in turn, fast enough that the ear fuses them.
 *
 * That is a real chord and it is also an honest compromise. Interleaving
 * amplitude-modulates every voice at the frame rate, which puts sidebands at
 * f +/- the frame rate; and a slot has to hold several cycles of its voice or
 * the ear hears the slot rate instead of the note. The two pressures fix the
 * numbers below between them, and they leave a grain on every chord -- right
 * for LCARS, which is machines acknowledging an instruction, and the reason
 * the Material tables use single voices for everything small.
 *
 * Two rules come out of it, and one of them is enforced by a host test because
 * a comment would not be enough:
 *
 *   1. Nothing below BEEPER_POLY_MIN_HZ is ever stacked. A 2 ms slot at 2 kHz
 *      is four cycles; at 800 Hz it is under two, and the pitch dissolves into
 *      the slot rate. The low alert sounds stay monophonic, which costs
 *      nothing -- everything except them already lives at 1.2-3 kHz, because
 *      that is where a small piezo is loud.
 *
 *   2. The frame is as long as the voices sounding *now*, not a fixed three
 *      slots, and one voice is not interleaved at all -- it gets an
 *      uninterrupted carrier on a BEEPER_STEP_MS step, which is bit for bit
 *      what this driver did before it could do chords. The grain appears only
 *      where polyphony was actually asked for.
 *
 * Phase is not carried across a slot boundary, so two voices a few hertz apart
 * do not beat. No table should be written expecting shimmer from a detune.
 */
#ifndef BEEPER_MIXER_H
#define BEEPER_MIXER_H

#include "beeper_common.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* One voice's share of a polyphonic frame. Three voices is a 6 ms frame, i.e.
 * 167 Hz, comfortably above the ~50 Hz at which alternating tones stop fusing
 * into a chord and start being heard as an arpeggio. */
#define BEEPER_SLOT_MS 2

/* A monophonic frame, and the resolution a sweep and an envelope are sampled
 * at. Unchanged: 5 ms is fine enough that a swept note is heard as a slide
 * rather than as steps, and coarse enough that a 200 ms chime is forty wakeups
 * of a task that is otherwise asleep. */
#define BEEPER_STEP_MS 5

/* More than three on a 2 kHz piezo is not resolvable, and the frame would grow
 * past the fusion floor. A chime with more voices sounding at once keeps the
 * first three *in table order*, so the table's first voice is the lead. */
#define BEEPER_VOICES_MAX 3

/* Below this a slot is shorter than two periods. See rule 1 above. */
#define BEEPER_POLY_MIN_HZ 1000

/* f_start == f_end is a steady note, which is what everything used to be. */
enum beeper_shape_e
{
    BEEPER_SHAPE_FLAT = 0, /* on for the duration; a blip                */
    BEEPER_SHAPE_PLUCK,    /* instant attack, then decay; a chirp        */
    BEEPER_SHAPE_PAD,      /* eased in and out; a swell                  */
    BEEPER_SHAPE_COUNT
};

/* A swept tone with an envelope.
 *
 * `volume` is this note's level against the other voices of its chime -- the
 * mix balance, written into the table by whoever composed the chime. The
 * config setting is a master on top of it, applied in beeper_level_permille().
 * There is deliberately no second per-voice scalar: two places to look when a
 * chord is unbalanced is one too many. */
struct beeper_note_s
{
    uint16_t f_start;
    uint16_t f_end;
    uint16_t duration_ms;
    uint16_t pause_ms;
    uint8_t  volume; /* 0..100, the peak of the envelope */
    uint8_t  shape;  /* enum beeper_shape_e              */
};

/* One line of a chime: its notes played one after another, on its own clock.
 *
 * `start_ms` lands in the padding this struct would have had anyway, and it
 * earns the space: a fifth joining a root twenty-five milliseconds late is the
 * commonest polyphonic gesture there is, and the alternative spelling -- a
 * leading note at volume 0 -- is a lie that the envelope code then has to
 * evaluate. */
struct beeper_voice_s
{
    const struct beeper_note_s *notes;
    uint16_t                    start_ms;
    uint8_t                     count;
};

/* A whole chime, queued as one item.
 *
 * One entry rather than one per note, and that is not only tidiness: the queue
 * is four deep, so a five-note sequence queued note by note would have its
 * tail silently dropped. This way a chime either plays or does not. Everything
 * it points at must outlive the call, which is what makes a table in flash the
 * natural way to write one. */
struct beeper_chime_s
{
    const struct beeper_voice_s *voices;
    uint8_t                      count;
};

/* The envelope, as a fraction of the peak in 0..255.
 *
 * Duty is amplitude on a piezo, so ramping it is the difference between a
 * square wave gated on and off -- which clicks at both ends and sounds like a
 * doorbell -- and something with an attack and a decay.
 *
 * `duration` of 0 returns the peak rather than dividing by it, and an
 * `elapsed` past the end saturates rather than wrapping: this is public now,
 * so the loop that used to be the only caller is no longer the only guard. */
uint8_t beeper_envelope(uint8_t shape, uint32_t elapsed, uint32_t duration);

/* Where `v` is at `t_ms`, measured from the start of its chime.
 *
 * Returns false once every note and every pause of it is behind us. While it
 * returns true the voice is alive but not necessarily sounding: *level_out is
 * 0 before start_ms and inside a note's pause, and the caller must treat that
 * as "occupies no slot" rather than as "plays silence", or a resting voice
 * would steal a third of the frame from the ones that are singing. */
bool beeper_voice_sample(const struct beeper_voice_s *v, uint32_t t_ms,
                         uint16_t *freq_out, uint8_t *level_out);

/* Put back what interleaving took away.
 *
 * A voice on for only 1/n of the frame has 1/sqrt(n) of the RMS it would have
 * had alone, so a chord would otherwise be quieter than one of its own notes
 * and -- worse -- every surviving line would jump in level the moment a
 * neighbour finished. Clamped, because at a high master volume there may be no
 * headroom left to put it back with. */
uint16_t beeper_slot_gain(uint16_t level, uint8_t voices);

/* Fill the slots for the frame beginning at `t_ms`.
 *
 * Returns the frame's length in milliseconds, or 0 when the chime is over.
 * *count is how many slots are sounding; a frame with none is a rest one step
 * long, which is what a chime whose voices are all mid-pause looks like.
 *
 * THIS FUNCTION MUST STAY PURE. Not a style preference: the simulator renders
 * a chime by evaluating it at arbitrary times from an audio callback, the host
 * tests call it out of order on purpose, and a hidden static would make both
 * of those quietly wrong rather than loudly broken. Whose turn it is comes out
 * of the argument list, never out of a file scope. */
uint16_t beeper_mixer_frame(const struct beeper_chime_s *chime, uint32_t t_ms,
                            uint8_t master, struct beeper_slot_s slots[BEEPER_VOICES_MAX],
                            uint8_t *count);

/* How long the whole chime lasts: the last millisecond any voice is still
 * playing or pausing. */
uint32_t beeper_chime_duration_ms(const struct beeper_chime_s *chime);

/* The most voices this chime ever has sounding at once. */
uint8_t beeper_chime_peak_voices(const struct beeper_chime_s *chime);

/* The lowest and highest frequency anywhere in the chime, sweeps included.
 * Both are set to 0 for a chime with no notes. */
void beeper_chime_freq_range(const struct beeper_chime_s *chime, uint16_t *lo,
                             uint16_t *hi);

#ifdef __cplusplus
}
#endif

#endif /* BEEPER_MIXER_H */
