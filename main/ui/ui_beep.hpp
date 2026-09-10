#ifndef UI_BEEP_HPP
#define UI_BEEP_HPP

/* What the panel sounds like, per theme family.
 *
 * This was six macros of fixed-pitch notes shared by every family, so a window
 * opened with the same C-E-G arpeggio whether the panel was pretending to be a
 * starship or a workshop. Sound is part of a theme's identity in exactly the
 * way its palette is, so it comes out of the theme table now.
 *
 * The honest caveat, and it belongs in the code rather than in a commit
 * message: a monophonic square-wave piezo cannot reproduce the sampled sounds
 * these families are named after. What it can carry is their rhythm and their
 * contour -- LCARS as rapid blips across wide intervals, Reticle as slow
 * swells -- and that is most of what makes either recognisable. Nobody should
 * read these tables expecting a recording.
 *
 * The call sites keep their macro names: what a gesture sounds like is a
 * theme's business, but *which* gesture happened is the UI's, and that has not
 * changed.
 */

#include "control/beeper_control.hpp"

#include <stdint.h>

enum ui_sound_e
{
    UI_SOUND_TOUCH = 0,  /* a press acknowledged            */
    UI_SOUND_CHANGE,     /* a value the user just moved     */
    UI_SOUND_LINK,       /* going deeper                    */
    UI_SOUND_LINK_BACK,  /* coming back                     */
    UI_SOUND_SCREEN,     /* a control screen opening        */
    UI_SOUND_SCREEN_OUT, /* and closing                     */
    UI_SOUND_ERROR,
    UI_SOUND_COUNT
};

/* One per family, shared by its day and night variants -- a theme does not
 * sound different after dark. */
struct ui_sound_s
{
    struct beeper_chime_s chime[UI_SOUND_COUNT];
};

void ui_beep_play(enum ui_sound_e sound);

extern const struct ui_sound_s ui_sound_default;
extern const struct ui_sound_s ui_sound_lcars;
extern const struct ui_sound_s ui_sound_jarvis;

#define BEEPER_EVENT_TOUCH()        ui_beep_play(UI_SOUND_TOUCH)
#define BEEPER_EVENT_CHANGE()       ui_beep_play(UI_SOUND_CHANGE)
#define BEEPER_EVENT_LINK()         ui_beep_play(UI_SOUND_LINK)
#define BEEPER_EVENT_LINK_BACK()    ui_beep_play(UI_SOUND_LINK_BACK)
#define BEEPER_EVENT_WINDOW()       ui_beep_play(UI_SOUND_SCREEN)
#define BEEPER_EVENT_WINDOW_CLOSE() ui_beep_play(UI_SOUND_SCREEN_OUT)
#define BEEPER_EVENT_ERROR()        ui_beep_play(UI_SOUND_ERROR)

#endif /* UI_BEEP_HPP */
