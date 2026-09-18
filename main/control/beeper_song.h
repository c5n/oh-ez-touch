/**
 * @file beeper_song.h
 *
 * The demonstration tune: half a minute that plays the sequencer's whole
 * vocabulary in order.
 *
 * Only a table. What starts it, what stops it and what knows whether it is
 * sounding are all in beeper_control.hpp behind beeper_demo_*(), and that is
 * the header a caller wants -- this one exists so that the notes can be walked
 * and measured by test/host, which has no clock, no task and no buzzer. Same
 * split, and the same reason, as the themed tables in ui/ui_beep_tables_seq.cpp.
 *
 * Sequencer only. The polyphonic engine has a different note format and a
 * different set of things worth demonstrating, and writing a second piece for
 * an engine behind a menuconfig switch is work nobody asked for -- so the whole
 * file is guarded and beeper_demo_available() answers false over there. That is
 * a deliberate asymmetry with the themed tables, which do ship twice: those
 * have to, because a panel is silent without them.
 *
 * Nothing but beeper_seq.h, which is nothing but <stdint.h>.
 */
#ifndef BEEPER_SONG_H
#define BEEPER_SONG_H

#include "sdkconfig.h"

#if CONFIG_OHEZ_BEEPER_ENGINE_SEQ

#include "beeper_seq.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The tune. Never NULL, and outlives every caller: it is a table in flash.
 *
 * Longer than BEEPER_SEQ_NOTES_MAX by a long way, and that is the one rule it
 * is exempt from. The advisory in beeper_seq.h is about *chimes* -- a sound
 * that answers a gesture and holds up the next one, which is why the table
 * tests cap the themed families at twelve struck notes and seven hundred
 * milliseconds. This is not a chime. It is the thing somebody presses a button
 * to listen to, and it is still one queue item, because `count` is a uint8_t
 * and the whole piece fits inside one.
 */
const struct beeper_seq_s *beeper_song(void);

#ifdef __cplusplus
}
#endif

#endif /* CONFIG_OHEZ_BEEPER_ENGINE_SEQ */

#endif /* BEEPER_SONG_H */
