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
 * families are named after. What it can do depends on the engine -- an
 * interleaved chord under beeper_mixer.h, or an envelope and a wobble under
 * beeper_seq.h, and see either header for what that costs. It still cannot do
 * noise, reverb, or a voice under either. Nobody should read these tables
 * expecting a recording.
 *
 * Deliberately free of <lvgl.h>: the table files include this and nothing
 * else, which is what lets the host tests link fifty-four hand-written sounds
 * per engine on a target that has no display and no buzzer. Same argument that
 * keeps ui_geometry.hpp clean. The one function here that needs an object takes
 * it through the forward declaration below.
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
 * One thing stays silent on purpose, and this is where to find out why rather
 * than to notice a gap and fill it: OTA. A firmware update is driven from a
 * browser by somebody who is not looking at the panel.
 *
 * ------------------------------------------------------------ MQTT, and door
 *
 * MQTT used to be the second of those, on the grounds that a panel which
 * chirps at three in the morning because a broker blipped is a defect. That
 * ground still holds and is now enforced rather than stated:
 * ui_beep_mqtt_setup() registers `sound/set` through
 * ohez_mqtt_subscribe_live(), so a *retained* message -- the only kind a
 * reconnect replays -- never reaches a handler. What is left is somebody
 * publishing on purpose, which is an installation asking the panel to make a
 * noise, and that is the whole point of the topic.
 *
 * Every sound in the vocabulary is reachable that way, including the ones the
 * UI plays for itself: there is nothing to be gained by letting a broker play
 * sixteen of the eighteen and guess about the rest. UI_SOUND_DOOR_CHIME is the
 * one that exists only for it. No gesture on this panel means "somebody is at
 * the door", so there is deliberately no BEEPER_EVENT_DOOR_CHIME() below and
 * no call site to go looking for -- a doorbell is a thing an installation
 * knows about and a touchscreen does not. It is in the vocabulary rather than
 * bolted on beside it so that what a door sounds like is a theme's decision,
 * the same as everything else here, and so that the host tests hold it to the
 * same policy as everything else here.
 */

#include "control/beeper_control.hpp"
#include "ui_theme.hpp"

#include <stdbool.h>
#include <stdint.h>
#include <strings.h>

/* As LVGL declares it, so including <lvgl.h> here is not necessary. Repeating
 * an identical typedef is legal, and this header is included by a translation
 * unit that must not see LVGL at all. */
typedef struct _lv_obj_t lv_obj_t;

/* The vocabulary, written down once.
 *
 * The enum and all three families' tables are generated from this list, so a
 * family that forgets a chime is a compile error naming the array it is
 * missing -- rather than an eighteen-element positional initialiser silently
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
    X(WAKE, wake)                                                              \
    /* somebody is at the door. No gesture means this -- MQTT only        */   \
    X(DOOR_CHIME, door_chime)

enum ui_sound_e
{
#define X(name, sym) UI_SOUND_##name,
    UI_SOUND_LIST(X)
#undef X
    UI_SOUND_COUNT
};

/* The vocabulary's names, for the host tests, which walk every family against
 * every entry and need something to put in the failure message.
 *
 * In the header rather than in a table file, the way ui_theme.hpp already does
 * it for ui_theme_names[]: there are two table files now, one per engine, and
 * both of them would otherwise define this identically -- which links fine on
 * the panel, where only one is compiled, and collides in the host test binary,
 * where both are. */
static const char *const ui_sound_names[UI_SOUND_COUNT] = {
#define X(name, sym) #sym,
    UI_SOUND_LIST(X)
#undef X
};

/* A name back to its sound, or UI_SOUND_COUNT for one that is not in the
 * vocabulary.
 *
 * This is how `sound/set` reads its payload -- see ui_beep_mqtt_setup() -- and
 * it is here rather than in ui_beep.cpp so that the host tests can reach it:
 * ui_beep.cpp needs <lvgl.h> and they have none, and a lookup that quietly
 * stopped matching would turn every message on that subtree into a warning
 * line nobody is reading.
 *
 * Case-insensitive because these names travel through a broker, where they are
 * typed by a person into a rule or a command line rather than generated, and
 * "DOOR_CHIME" failing while "door_chime" works is a trap with no upside. */
static inline enum ui_sound_e ui_sound_from_name(const char *name)
{
    if (name == NULL)
        return UI_SOUND_COUNT;

    for (int s = 0; s < UI_SOUND_COUNT; s++)
        if (strcasecmp(name, ui_sound_names[s]) == 0)
            return (enum ui_sound_e)s;

    return UI_SOUND_COUNT;
}

/* One set per family, shared by its day and night variants -- a theme does not
 * sound different after dark.
 *
 * Two engines, two note formats, so two sets of tables and two types to hold
 * them. They MUST NOT share a symbol or a struct tag: test/host links both
 * table files into one binary, because fifty-four hand-written sounds each are
 * the only place either set is checked at all, and two different definitions of
 * one `struct ui_sound_s` would be an ODR violation that LTO eventually
 * notices.
 *
 * The firmware compiles exactly one of them. ui_style.hpp holds a
 * `const ui_sound_set_s *sound`, and ui_style.cpp's six theme rows name
 * UI_SOUND_SET_MATERIAL / _LCARS / _JARVIS -- so neither of those files has a
 * preprocessor conditional in it, and this is the only #if in the UI layer.
 *
 * Both set structs name their member `chime`, which is what lets ui_beep.cpp's
 * backstop and mute gate stay engine-blind. A typedef rather than the tag
 * spelled out is a small deviation from the house style and the smallest one
 * available: the point is that the name is the same on both sides and only its
 * meaning changes. */
#if CONFIG_OHEZ_BEEPER_ENGINE_SEQ

struct ui_tune_set_s
{
    struct beeper_seq_s chime[UI_SOUND_COUNT];
};

typedef struct ui_tune_set_s ui_sound_set_s;

extern const struct ui_tune_set_s ui_tune_material;
extern const struct ui_tune_set_s ui_tune_lcars;
extern const struct ui_tune_set_s ui_tune_jarvis;

extern const struct ui_tune_set_s *const ui_tune_sets[UI_THEME_FAMILY_COUNT];

#define UI_SOUND_SET_MATERIAL ui_tune_material
#define UI_SOUND_SET_LCARS   ui_tune_lcars
#define UI_SOUND_SET_JARVIS  ui_tune_jarvis

#else

struct ui_chime_set_s
{
    struct beeper_chime_s chime[UI_SOUND_COUNT];
};

typedef struct ui_chime_set_s ui_sound_set_s;

extern const struct ui_chime_set_s ui_chime_material;
extern const struct ui_chime_set_s ui_chime_lcars;
extern const struct ui_chime_set_s ui_chime_jarvis;

extern const struct ui_chime_set_s *const ui_chime_sets[UI_THEME_FAMILY_COUNT];

#define UI_SOUND_SET_MATERIAL ui_chime_material
#define UI_SOUND_SET_LCARS   ui_chime_lcars
#define UI_SOUND_SET_JARVIS  ui_chime_jarvis

#endif

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

/* Claim `sound/set` on the broker, so that an installation can play any sound
 * of the theme in force by publishing its name from ui_sound_names[].
 *
 * Called from main.cpp before ohez_mqtt_setup(), the same as relay_setup() and
 * led_setup() and for the same reason: the client asks the broker for every
 * registered filter when it connects. Costs nothing on a panel with MQTT
 * switched off -- there is no connection to subscribe on -- and nothing on one
 * with the beeper switched off either, because ui_beep_play() is still the only
 * way in and both of its gates are still in front of it. */
void ui_beep_mqtt_setup(void);

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
