/* The sound-table policy both engines' suites check against.
 *
 * Shared rather than copied, and that is not only tidiness: may_go_low() below
 * deliberately has no `default:`, so an eighteenth sound is a -Wswitch warning
 * until somebody classifies it. Two copies would warn twice -- and somebody
 * would answer one of them and not the other, which is exactly the drift the
 * missing `default:` exists to prevent.
 *
 * These are editorial decisions about the interface, not properties of an
 * engine, which is why a chime table and a tune table are held to the same
 * ones.
 */
#ifndef TEST_UI_BEEP_POLICY_HPP
#define TEST_UI_BEEP_POLICY_HPP

#include <stdbool.h>
#include <stdio.h>

#include "ui/ui_beep.hpp"
#include "ui/ui_theme.hpp"

/* Immediate feedback has to be over before the finger is: past about this, a
 * press sound stops being an acknowledgement and starts being an echo. */
#define FEEDBACK_MAX_MS 150

/* Everything else. A sound longer than this is a jingle, and the panel plays
 * them serially -- so it is also one that delays the next. */
#define CHIME_MAX_MS 700

/* Which sounds may go below the piezo's useful band.
 *
 * 440 Hz is a mistake in a chime meant to be heard across a room, and is the
 * entire point of one meant to be impossible to ignore. So this is an editorial
 * decision about which gestures are allowed to be unpleasant, and it is written
 * down here rather than inferred from the notes.
 *
 * Deliberately no `default:`. Every enumerator is listed, so adding an
 * eighteenth sound is a -Wswitch warning until somebody has decided which kind
 * it is, rather than inheriting an exemption or an obligation by accident. */
static inline bool may_go_low(enum ui_sound_e sound)
{
    switch (sound)
    {
    case UI_SOUND_ERROR:
        return true;

    case UI_SOUND_PRESS:
    case UI_SOUND_TICK:
    case UI_SOUND_TICK_BACK:
    case UI_SOUND_TOGGLE_ON:
    case UI_SOUND_TOGGLE_OFF:
    case UI_SOUND_CHANGE:
    case UI_SOUND_ACCEPT:
    case UI_SOUND_CANCEL:
    case UI_SOUND_LINK:
    case UI_SOUND_LINK_BACK:
    case UI_SOUND_SCREEN:
    case UI_SOUND_SCREEN_OUT:
    case UI_SOUND_NOTIFY:
    case UI_SOUND_WARNING:
    case UI_SOUND_BOOT:
    case UI_SOUND_WAKE:
    case UI_SOUND_COUNT:
        return false;
    }

    return false;
}

static inline bool is_feedback(enum ui_sound_e sound)
{
    return sound == UI_SOUND_PRESS || sound == UI_SOUND_TICK ||
           sound == UI_SOUND_TICK_BACK || sound == UI_SOUND_CHANGE;
}

/* "lcars/screen_out", for a failure message that says which of the fifty-one. */
static inline const char *where(int family, enum ui_sound_e sound)
{
    static char buf[64];

    snprintf(buf, sizeof(buf), "%s/%s", ui_theme_name((enum ui_theme_family_e)family),
             ui_sound_names[sound]);

    return buf;
}

#endif /* TEST_UI_BEEP_POLICY_HPP */
