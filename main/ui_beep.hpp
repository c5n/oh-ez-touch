#ifndef UI_BEEP_HPP
#define UI_BEEP_HPP

/* The UI's sound policy, one macro per kind of thing the user just did.
 *
 * These lived in openhab_ui.cpp until the settings screen needed the same
 * sounds for the same gestures -- a window opening should not sound different
 * depending on which file created it. Macros rather than functions because a
 * chime is a sequence of notes and naming each sequence beats repeating it.
 *
 * They are unconditional now. Where there is no buzzer -- the simulator, and
 * the Lanbon L8 -- the notes are queued and played into a port_beeper that
 * makes no sound, which costs nothing and keeps the guards out of the UI.
 */

#include "driver/beeper_control.hpp"

#ifndef BEEPER_VOLUME
#define BEEPER_VOLUME 50
#endif

#define BEEPER_EVENT_CHANGE()              \
    {                                      \
        beeper_playNote(NOTE_C7, BEEPER_VOLUME, 5, 0); \
    }
#define BEEPER_EVENT_LINK()                  \
    {                                        \
        beeper_playNote(NOTE_C7, BEEPER_VOLUME, 20, 10); \
        beeper_playNote(NOTE_E7, BEEPER_VOLUME, 10, 0);  \
    }
#define BEEPER_EVENT_LINK_BACK()            \
    {                                       \
        beeper_playNote(NOTE_E7, BEEPER_VOLUME, 10, 5); \
        beeper_playNote(NOTE_C7, BEEPER_VOLUME, 10, 5); \
        beeper_playNote(NOTE_A6, BEEPER_VOLUME, 20, 0); \
    }
#define BEEPER_EVENT_WINDOW()               \
    {                                       \
        beeper_playNote(NOTE_C7, BEEPER_VOLUME, 10, 0); \
        beeper_playNote(NOTE_E7, BEEPER_VOLUME, 10, 0); \
        beeper_playNote(NOTE_G7, BEEPER_VOLUME, 20, 0); \
    }
#define BEEPER_EVENT_WINDOW_CLOSE()         \
    {                                       \
        beeper_playNote(NOTE_G7, BEEPER_VOLUME, 10, 0); \
        beeper_playNote(NOTE_E7, BEEPER_VOLUME, 10, 0); \
        beeper_playNote(NOTE_C7, BEEPER_VOLUME, 20, 0); \
    }
#define BEEPER_EVENT_ERROR()                 \
    {                                        \
        beeper_playNote(NOTE_E3, BEEPER_VOLUME, 50, 0); \
        beeper_playNote(NOTE_C3, BEEPER_VOLUME, 100, 0); \
    }

#endif // UI_BEEP_HPP
