#ifndef BEEPER_CONTROL_HPP
#define BEEPER_CONTROL_HPP

/* sdkconfig.h first and explicitly: an unset Kconfig bool is undefined rather
 * than 0, so a file that tested CONFIG_OHEZ_BEEPER_ENGINE_SEQ without this in
 * scope would silently compile against the other engine. Same rule main/debug.h
 * writes down, and the #error below is what turns a missed include from a
 * wrong-engine build into a failure. */
#include "sdkconfig.h"

#if CONFIG_OHEZ_BEEPER_ENGINE_SEQ
#include "beeper_seq.h"
#elif CONFIG_OHEZ_BEEPER_ENGINE_MIXER
#include "beeper_mixer.h"
#else
#error "no beeper engine selected -- see CONFIG_OHEZ_BEEPER_ENGINE"
#endif

#include <stdint.h>

#ifndef BEEPER_CONTROL_QUEUE_LENGTH
#define BEEPER_CONTROL_QUEUE_LENGTH 4
#endif

/* Chimes are played from a task of their own, so that a UI event can ask for
 * one without blocking the screen for its duration. Shared by both targets;
 * the tone behind it is port_beeper, which is silent on the Lanbon, and which
 * on the simulator renders the whole chime itself -- see port_beeper_render().
 *
 * What a chime *is*, and the arithmetic that turns one into a frequency and a
 * duty, is beeper_seq.h or beeper_mixer.h depending on which engine is
 * selected -- see CONFIG_OHEZ_BEEPER_ENGINE. What is left here is the queue,
 * the task, and the delays: the parts that need a clock and cannot be tested
 * without one, and the parts both engines need identically. That is why this
 * is one file with two small guarded holes in it rather than two files: the
 * tick floor below and the overrun resync were hard to get right once, and two
 * copies of them would drift.
 *
 * The queue entry differs between the engines and so does the name that fills
 * it -- beeper_play_seq() against beeper_play(). Two names rather than one
 * overloaded one, so that a caller which forgot its own guard fails with "no
 * member named beeper_play_seq" rather than with a type mismatch.
 *
 * beeper_playNote() used to live here for callers with a loose frequency and
 * nothing to say about shape. It had exactly one, the hard-coded C4 that
 * main.cpp played on wake, and that is a themed chime now like everything
 * else -- so the queue carries a whole chime by value, eight bytes, and no
 * reader of the task has to work out why the note pointer might be null. */

/* Queue a chime or a tune. Dropped rather than waited on when the queue is
 * full, and silently ignored when the beeper is disabled: a missed blip is not
 * worth blocking a touch handler for. */
#if CONFIG_OHEZ_BEEPER_ENGINE_SEQ
void beeper_play_seq(const struct beeper_seq_s *seq);
#else
void beeper_play(const struct beeper_chime_s *chime);
#endif

/* Force the next queued chime past the mute: the locate chime from the web
 * API, whose whole point is the panel whose position -- and whose settings --
 * are not known. One shot, not a setting: the flag travels with the single
 * item queued after this call and is gone with it, so the chime after that is
 * muted again. The queue and the task are brought up if the beeper was never
 * switched on, so a panel that has always been muted still answers. */
void beeper_force_next(void);

/* Abandon whatever is sounding and everything queued behind it.
 *
 * The mute gate could always do this -- beeper_set_enabled(false) empties the
 * queue and the walk rechecks the flag every frame -- but that is a *setting*,
 * and a caller that wants to cut one sound short should not have to switch the
 * panel's sound off and back on to do it. Nothing needed the distinction while
 * every sound was under a second; the demonstration tune is half a minute, and
 * a toggle that cannot stop what it started is not a toggle.
 *
 * Works on both targets, which is the part that needed the port layer: on the
 * panel the walk breaks at its next frame, and on the simulator the tune was
 * handed to an audio callback whole and only port_beeper_stop() can take it
 * back. A sound queued *after* this call is unaffected -- what is cancelled is
 * what was outstanding when it ran, not the beeper. */
void beeper_stop(void);

/* --------------------------------------------------- the demonstration tune
 *
 * A half-minute piece that plays the engine's whole vocabulary in order --
 * every envelope, every effect, sweeps both ways, repeats and a dynamic range
 * -- so that what the presets actually sound like can be heard rather than
 * read off a table. The settings screen's Audio page has the button; there is
 * no other caller and nothing plays it by itself.
 *
 * Deliberately behind an opaque façade rather than exported as a tune. Only
 * the sequencer has one, so a caller holding a `struct beeper_seq_s *` would
 * need a preprocessor conditional, and ui_settings.cpp having one would be the
 * second #if in the UI layer -- see the note about the first in ui_beep.hpp.
 * beeper_demo_available() answers the same question at runtime and costs
 * nothing, because the mixer build compiles the other three to no-ops. */

/* Whether this build has one. False under the polyphonic engine, where the
 * tune does not exist and the button should not be drawn. */
bool beeper_demo_available(void);

/* Start it, from the beginning, cancelling anything already sounding.
 *
 * @return false when there is nothing to play or the beeper is switched off,
 *   in which case nothing started and beeper_demo_playing() stays false.
 */
bool beeper_demo_start(void);

/* Stop it early. A no-op when it is not playing. */
void beeper_demo_stop(void);

/* Whether it is sounding now.
 *
 * Polled rather than notified -- ui_settings_loop() and ui_beep_play() both
 * ask, and neither wants a callback from the beeper task. It goes false by
 * itself when the tune reaches its end, so a caller that only ever calls
 * beeper_demo_start() does not leak a "playing" state. */
bool beeper_demo_playing(void);

/* Bring the PWM up, silent. */
void beeper_setup(void);

/* Turn the sound on or off, live.
 *
 * Called from setup() and again from settings_apply_live() on every save.
 * Enabling creates the queue and the task, once -- lazily, so a panel with the
 * beeper switched off never pays the task's two kilobytes.
 *
 * Disabling does not tear them down. Deleting a task in the middle of a note
 * would leave the LEDC channel sounding, and recreating one on every save is
 * the leak the old beeper_enable() was written to stop. It empties the queue
 * and sets a flag that beeper_play() consults and that the task rechecks at
 * every frame boundary, so a chime already in flight stops within a frame
 * rather than at its end. */
void beeper_set_enabled(bool enabled);

/* The master level, 0..100, applied to every note of every chime on top of
 * whatever the table asked for. 25 is what this panel sounded like before
 * there was a setting; see port/esp32/port_beeper.c for why that number. */
void beeper_set_volume(uint8_t percent);

#endif
