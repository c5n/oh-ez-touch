#ifndef UI_BEEP_HPP
#define UI_BEEP_HPP

/* What the panel sounds like, per theme family.
 *
 * This was six macros of fixed-pitch notes shared by every family, so a window
 * opened with the same C-E-G arpeggio whether the panel was pretending to be a
 * starship or a workshop. Sound is part of a theme's identity in exactly the
 * way its palette is, so it comes out of the theme table.
 *
 * The honest caveat, and it belongs in the code rather than in a commit
 * message: a piezo on one pin cannot reproduce the sampled sounds these
 * families are named after. It can now play a chord -- see beeper_mixer.h for
 * how, and for what that costs -- which is most of what makes an LCARS blip
 * recognisable, because those are stacked intervals rather than tones. It
 * still cannot do noise, reverb, or a voice. Nobody should read these tables
 * expecting a recording.
 *
 * Deliberately free of <lvgl.h>: ui_beep_tables.cpp includes this and nothing
 * else, which is what lets the host tests link forty-five hand-written chimes
 * on a target that has no display and no buzzer. Same argument that keeps
 * ui_geometry.hpp clean. The one function here that needs an object takes it
 * through the forward declaration below.
 *
 * ------------------------------------------------------- the layering policy
 *
 * Two sounds can reach the queue for one gesture, and that is by design rather
 * than by accident. Getting it wrong is the easiest way to make this panel
 * unpleasant, so the rule is written down here and every call site is expected
 * to have read it.
 *
 *   1. LAYER ONE, CONTACT, is UI_SOUND_PRESS. It plays on LV_EVENT_PRESSED,
 *      from ui_motion_pressable(), unconditionally, for every pressable
 *      object. It says "the glass felt you" and nothing else. It is at most
 *      twenty milliseconds long and about a third of the level of everything
 *      else.
 *
 *   2. LAYER TWO, OUTCOME, is every other sound. It plays on the event that
 *      decides the outcome -- CLICKED, RELEASED, READY. No handler may play a
 *      layer-two sound on PRESSED, and no gesture may produce two of them.
 *
 *   3. The two are separated by however long the finger dwells, which is never
 *      less than about fifty milliseconds and usually two hundred. So it reads
 *      as "tick ... chime", a key that clicks and then a result, rather than
 *      as two events.
 *
 * What makes that safe rather than merely stated is that the two layers touch
 * disjoint sets of objects. ui_motion_pressable() reaches tiles, themed
 * buttons, back bars and settings rows; it does not reach the sliders, the
 * colour fields or the keyboard, and those three play their own sounds. THE
 * KEYBOARD IS THE ONE THAT WOULD COLLIDE: if anybody ever makes it
 * ui_motion_pressable(), every key will sound twice.
 *
 * ui_beep_play() has a backstop for the same reason a belt has braces --
 * see UI_BEEP_TICK_MIN_GAP_MS in ui_beep.cpp.
 *
 * Two things stay silent on purpose, and this is where to find out why rather
 * than to notice a gap and fill it: MQTT and OTA. A panel that chirps at three
 * in the morning because a broker blipped is a defect, and a firmware update
 * is driven from a browser by somebody who is not looking at the panel.
 */

#include "control/beeper_control.hpp"
#include "ui_theme.hpp"

#include <stdbool.h>
#include <stdint.h>

/* As LVGL declares it, so including <lvgl.h> here is not necessary. Repeating
 * an identical typedef is legal, and this header is included by a translation
 * unit that must not see LVGL at all. */
typedef struct _lv_obj_t lv_obj_t;

/* The vocabulary, written down once.
 *
 * The enum and all three families' tables are generated from this list, so a
 * family that forgets a chime is a compile error naming the array it is
 * missing -- rather than a seventeen-element positional initialiser silently
 * shifted by one, which is what the old seven-entry tables would have become. */
#define UI_SOUND_LIST(X)                                                       \
    /* contact acknowledged, before anything has been decided             */   \
    X(PRESS, press)                                                            \
    /* one grain gained or lost: a key, a backspace, a slider detent      */   \
    X(TICK, tick)                                                              \
    X(TICK_BACK, tick_back)                                                    \
    /* a binary went to its active or its inactive state                  */   \
    X(TOGGLE_ON, toggle_on)                                                    \
    X(TOGGLE_OFF, toggle_off)                                                  \
    /* a value that is not a binary, moved and committed                  */   \
    X(CHANGE, change)                                                          \
    /* a draft committed; a modal dismissed with nothing committed        */   \
    X(ACCEPT, accept)                                                          \
    X(CANCEL, cancel)                                                          \
    /* deeper into a hierarchy without leaving the surface, and back out  */   \
    X(LINK, link)                                                              \
    X(LINK_BACK, link_back)                                                    \
    /* a surface covered the one you were on, and uncovered it again      */   \
    X(SCREEN, screen)                                                          \
    X(SCREEN_OUT, screen_out)                                                  \
    /* an unsolicited banner, by severity; or the panel refused           */   \
    X(NOTIFY, notify)                                                          \
    X(WARNING, warning)                                                        \
    X(ERROR, error)                                                            \
    /* the firmware is ready. Once per power-up, and the Test button      */   \
    X(BOOT, boot)                                                              \
    /* the tap that woke the display and was consumed by waking it        */   \
    X(WAKE, wake)

enum ui_sound_e
{
#define X(name, sym) UI_SOUND_##name,
    UI_SOUND_LIST(X)
#undef X
    UI_SOUND_COUNT
};

/* One per family, shared by its day and night variants -- a theme does not
 * sound different after dark. */
struct ui_sound_s
{
    struct beeper_chime_s chime[UI_SOUND_COUNT];
};

/* Play `sound` from the theme in force, unless the beeper is off or the
 * backstop decides this particular tick would land inside a chime. */
void ui_beep_play(enum ui_sound_e sound);

/* The runtime mute. Starts false, which is what keeps the banners raised while
 * the UI is still being built from announcing themselves before the panel is
 * ready; main.cpp turns it on at the end of ohez_setup(), immediately before
 * the boot chime. */
void ui_beep_set_enabled(bool en);

/* Give `obj` the contact tick on LV_EVENT_PRESSED.
 *
 * Called by ui_motion_pressable(), which is what every tile, themed button,
 * back bar and settings row already goes through. It is also public for the
 * two widgets that want the sound without the plate deformation: a slider and
 * a colour field are dragged rather than pressed, so the motion feedback would
 * be wrong on them and the acknowledgement is still right. */
void ui_beep_attach_press(lv_obj_t *obj);

/* The three tables, and two ways to enumerate them.
 *
 * The arrays exist for the host tests, which check all three families against
 * every entry of the vocabulary and need a name to put in the failure message.
 * Nothing on the device uses them: the theme table in ui_style.cpp points
 * straight at the three objects. */
extern const struct ui_sound_s ui_sound_default;
extern const struct ui_sound_s ui_sound_lcars;
extern const struct ui_sound_s ui_sound_jarvis;

extern const char *const            ui_sound_names[UI_SOUND_COUNT];
extern const struct ui_sound_s *const ui_sound_sets[UI_THEME_FAMILY_COUNT];

/* The call sites keep macro names: what a gesture sounds like is a theme's
 * business, but *which* gesture happened is the UI's. */
#define BEEPER_EVENT_PRESS()       ui_beep_play(UI_SOUND_PRESS)
#define BEEPER_EVENT_TICK()        ui_beep_play(UI_SOUND_TICK)
#define BEEPER_EVENT_TICK_BACK()   ui_beep_play(UI_SOUND_TICK_BACK)
#define BEEPER_EVENT_TOGGLE_ON()   ui_beep_play(UI_SOUND_TOGGLE_ON)
#define BEEPER_EVENT_TOGGLE_OFF()  ui_beep_play(UI_SOUND_TOGGLE_OFF)
#define BEEPER_EVENT_CHANGE()      ui_beep_play(UI_SOUND_CHANGE)
#define BEEPER_EVENT_ACCEPT()      ui_beep_play(UI_SOUND_ACCEPT)
#define BEEPER_EVENT_CANCEL()      ui_beep_play(UI_SOUND_CANCEL)
#define BEEPER_EVENT_LINK()        ui_beep_play(UI_SOUND_LINK)
#define BEEPER_EVENT_LINK_BACK()   ui_beep_play(UI_SOUND_LINK_BACK)
#define BEEPER_EVENT_SCREEN()      ui_beep_play(UI_SOUND_SCREEN)
#define BEEPER_EVENT_SCREEN_OUT()  ui_beep_play(UI_SOUND_SCREEN_OUT)
#define BEEPER_EVENT_NOTIFY()      ui_beep_play(UI_SOUND_NOTIFY)
#define BEEPER_EVENT_WARNING()     ui_beep_play(UI_SOUND_WARNING)
#define BEEPER_EVENT_ERROR()       ui_beep_play(UI_SOUND_ERROR)
#define BEEPER_EVENT_BOOT()        ui_beep_play(UI_SOUND_BOOT)
#define BEEPER_EVENT_WAKE()        ui_beep_play(UI_SOUND_WAKE)

#endif /* UI_BEEP_HPP */
