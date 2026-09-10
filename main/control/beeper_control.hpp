#ifndef BEEPER_CONTROL_HPP
#define BEEPER_CONTROL_HPP

#include "beeper_control_pitches.h"

#include <stdint.h>

#ifndef BEEPER_CONTROL_QUEUE_LENGTH
#define BEEPER_CONTROL_QUEUE_LENGTH 4
#endif

/* Notes are played from a task of their own, so that a UI event can ask for a
 * chime without blocking the screen for its duration. Shared by both targets;
 * the tone behind it is port_beeper, which is silent in the simulator and on
 * the Lanbon, neither of which has a buzzer.
 *
 * A note is a *swept* tone with an envelope, not a fixed pitch gated on and
 * off. That is the whole difference between this and a doorbell: the sounds
 * this panel is imitating are chirps and swells, and a square wave switched
 * abruptly also clicks at both ends. Both come out of the same primitive --
 * port_beeper_tone() takes an arbitrary frequency and an arbitrary duty on
 * every call -- so none of it needs anything new from the port layer.
 *
 * f_start == f_end is a steady note, which is what everything used to be. */
enum beeper_shape_e
{
    BEEPER_SHAPE_FLAT = 0, /* on for the duration; a blip                    */
    BEEPER_SHAPE_PLUCK,    /* instant attack, exponential decay; a chirp     */
    BEEPER_SHAPE_PAD       /* eased in and out; a swell                      */
};

struct beeper_note_s
{
    uint16_t f_start;
    uint16_t f_end;
    uint16_t duration_ms;
    uint16_t pause_ms;
    uint8_t  volume; /* 0..100, the peak of the envelope */
    uint8_t  shape;  /* enum beeper_shape_e              */
};

/* A whole chime, queued as one item.
 *
 * One entry rather than one per note, and that is not only tidiness: the queue
 * is four deep, so a five-note sequence queued note by note would have its
 * tail silently dropped. This way a chime either plays or does not. `notes`
 * must outlive the call, which is what makes a table in flash the natural way
 * to write one. */
struct beeper_chime_s
{
    const struct beeper_note_s *notes;
    uint8_t                     count;
};

void beeper_play(const struct beeper_chime_s *chime);

/* One steady note, as before. Kept for callers with nothing to say about
 * shape; it is a one-note chime underneath. */
void beeper_playNote(uint16_t note, uint8_t volume, uint16_t duration, uint16_t pause);

/* Bring the PWM up, silent. */
void beeper_setup(void);

/* Start the queue and the task. Idempotent: it is called from setup() and
 * again from settings_apply_live() on every save, and used to leak a queue and
 * a task each time. */
void beeper_enable(void);

#endif
