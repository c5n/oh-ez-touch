/**
 * @file ui_beep_tables.cpp
 *
 * Three families, three voices -- and now, where it earns itself, more than
 * one at a time.
 *
 * The polyphonic engine's tables -- see CONFIG_OHEZ_BEEPER_ENGINE. The whole
 * file is guarded, because the other engine has its own fifty-four in
 * ui_beep_tables_seq.cpp and a panel carries only the set it plays.
 *
 * Separate from ui_beep.cpp so that the tables can be linked by the host tests,
 * which have no LVGL and no buzzer: everything here includes ui_beep.hpp and
 * <stdint.h> and nothing else, while ui_beep_play() needs ui_style.hpp and
 * therefore <lvgl.h>. Fifty-one hand-written chimes on a target that cannot
 * make a sound is exactly the kind of thing that needs a test, because a
 * missing one is invisible until somebody flashes a panel.
 *
 * ------------------------------------------------------------------ the band
 *
 * Frequencies sit between about 1.2 and 3.9 kHz because that is where a small
 * piezo is loudest -- a note an octave lower is not quieter on paper and is
 * much quieter in a room. The error sounds deliberately break that: being hard
 * to ignore matters more than being loud, and a klaxon that sounds like the
 * rest of the interface is not a klaxon.
 *
 * Nothing stacked ever goes below BEEPER_POLY_MIN_HZ. Interleaving gives each
 * voice a two-millisecond slot, which is four cycles at 2 kHz and under two
 * below one, and under two the ear hears the slot rate instead of the note.
 * That is why the LCARS and Material alerts are single voices: they are the
 * only chimes that live down there. test_ui_beep.cpp enforces it.
 *
 * ---------------------------------------------------------------- the voices
 *
 * LCARS uses chords where an interval is what is wanted and single voices
 * where a figure is, which is the same editorial line ui_beep_tables_seq.cpp
 * draws and the reason the two families read alike across the engines: the
 * contours, the rhythms and the registers here are the ones written out at
 * length over there, and that is the file to read for why each one is what it
 * is. What this engine adds is the stacking, and it is spent on the three
 * sounds where the show's panels are audibly an interval rather than a tone.
 *
 * Reticle uses them for triads and lets slow envelopes hide the grain.
 * Everything is a PAD and everything sweeps; affirmative rises.
 *
 * Material uses them hardly at all, and that is the design rather than a
 * shortfall. It is the family that must not draw attention, single voices have
 * no grain to hide, and a dyad is saved for the two moments where something
 * arrives or leaves.
 */
#include "sdkconfig.h"

#if CONFIG_OHEZ_BEEPER_ENGINE_MIXER

#include "ui_beep.hpp"

/* f_start, f_end, duration, pause, volume, shape */
#define N(fs, fe, d, p, v, sh)                                                 \
    {                                                                          \
        (uint16_t)(fs), (uint16_t)(fe), (uint16_t)(d), (uint16_t)(p),          \
            (uint8_t)(v), (uint8_t)(sh)                                        \
    }

/* A voice: its notes, and how long after the chime starts it comes in. */
#define V(a)      {(a), 0, (uint8_t)(sizeof(a) / sizeof((a)[0]))}
#define VAT(a, t) {(a), (uint16_t)(t), (uint8_t)(sizeof(a) / sizeof((a)[0]))}

#define CHIME(v) {(v), (uint8_t)(sizeof(v) / sizeof((v)[0]))}

/* A chime with one line, which is still most of them: the notes and the single
 * voice that plays them, in one declaration. */
#define MONO(name, ...)                                                        \
    static const struct beeper_note_s  name##_n[] = {__VA_ARGS__};             \
    static const struct beeper_voice_s name[]     = {V(name##_n)}

/* The level a note carries is its balance against the other voices of its
 * chime, not its loudness in the room -- the config's master volume is what
 * decides that, and 25 of it reproduces exactly what this panel has always
 * sounded like. VOL is what every chime was written at before there was a
 * master; the other two are relative to it. */
#define VOL  50 /* a note in the foreground                                */
#define MED  30 /* the wake blip: it goes off in a dark room               */
#define LOW  17 /* the contact layer -- see the policy in ui_beep.hpp      */

#define PLUCK BEEPER_SHAPE_PLUCK
#define PAD   BEEPER_SHAPE_PAD
#define FLAT  BEEPER_SHAPE_FLAT

/* --------------------------------------------------------------- Material
 *
 * Restrained, consonant, and mostly one voice. */

MONO(material_press,     N(2093, 2093, 12, 0, LOW, PLUCK));
MONO(material_tick,      N(2349, 2349, 10, 0, LOW, PLUCK));
MONO(material_tick_back, N(1976, 1976, 10, 0, LOW, PLUCK));
MONO(material_change,    N(2093, 2093, 25, 0, VOL, PLUCK));
MONO(material_wake,      N(2093, 2093, 40, 0, MED, PLUCK));

/* Two notes a fourth apart, in sequence rather than together: going somewhere
 * is a move, and a move is two things one after the other. */
MONO(material_link,      N(2093, 2093, 30, 10, VOL, PLUCK),
                         N(2794, 2794, 40, 0, VOL, PLUCK));
MONO(material_link_back, N(2794, 2794, 30, 10, VOL, PLUCK),
                         N(2093, 2093, 40, 0, VOL, PLUCK));

MONO(material_notify,    N(2093, 2093, 30, 8, VOL, PLUCK),
                         N(2637, 2637, 45, 0, VOL, PLUCK));
MONO(material_warning,   N(1568, 1568, 80, 60, VOL, FLAT),
                         N(1568, 1568, 80, 0, VOL, FLAT));

/* The one Material chime that is deliberately outside the piezo's good band,
 * and the one that has to stay a single voice because of it. */
MONO(material_error,     N(660, 660, 90, 40, VOL, FLAT),
                         N(440, 440, 180, 0, VOL, FLAT));

/* Arrival and departure: the two places a second voice is worth its grain. */
static const struct beeper_note_s material_on_lead[]  = {N(2093, 2093, 20, 5, VOL, PLUCK),
                                                         N(2637, 2637, 35, 0, VOL, PLUCK)};
static const struct beeper_note_s material_on_pad[]   = {N(3136, 3136, 35, 0, VOL, PLUCK)};
static const struct beeper_voice_s material_toggle_on[] = {V(material_on_lead),
                                                           VAT(material_on_pad, 25)};

static const struct beeper_note_s material_off_lead[] = {N(2637, 2637, 20, 5, VOL, PLUCK),
                                                         N(2093, 2093, 35, 0, VOL, PLUCK)};
static const struct beeper_note_s material_off_pad[]  = {N(1568, 1568, 35, 0, VOL, PLUCK)};
static const struct beeper_voice_s material_toggle_off[] = {V(material_off_lead),
                                                            VAT(material_off_pad, 25)};

/* A major third, arriving a moment late: "done". */
static const struct beeper_note_s material_acc_lead[] = {N(2093, 2093, 50, 0, VOL, PLUCK)};
static const struct beeper_note_s material_acc_pad[]  = {N(2637, 2637, 30, 0, VOL, PLUCK)};
static const struct beeper_voice_s material_accept[]  = {V(material_acc_lead),
                                                         VAT(material_acc_pad, 20)};

static const struct beeper_note_s material_can_lead[] = {N(2637, 2637, 50, 0, VOL, PLUCK)};
static const struct beeper_note_s material_can_pad[]  = {N(2093, 2093, 30, 0, VOL, PLUCK)};
static const struct beeper_voice_s material_cancel[]  = {V(material_can_lead),
                                                         VAT(material_can_pad, 20)};

static const struct beeper_note_s material_open_lead[] = {N(2093, 2093, 25, 5, VOL, PLUCK),
                                                          N(2637, 2637, 25, 5, VOL, PLUCK),
                                                          N(3136, 3136, 45, 0, VOL, PLUCK)};
static const struct beeper_note_s material_open_pad[]  = {N(2093, 2093, 45, 0, VOL, PLUCK)};
static const struct beeper_voice_s material_screen[]   = {V(material_open_lead),
                                                          VAT(material_open_pad, 60)};

static const struct beeper_note_s material_shut_lead[] = {N(3136, 3136, 25, 5, VOL, PLUCK),
                                                          N(2637, 2637, 25, 5, VOL, PLUCK),
                                                          N(2093, 2093, 45, 0, VOL, PLUCK)};
static const struct beeper_note_s material_shut_pad[]  = {N(1568, 1568, 45, 0, VOL, PLUCK)};
static const struct beeper_voice_s material_screen_out[] = {V(material_shut_lead),
                                                            VAT(material_shut_pad, 60)};

static const struct beeper_note_s material_boot_lead[] = {N(2093, 2093, 45, 10, VOL, PLUCK),
                                                          N(2637, 2637, 45, 10, VOL, PLUCK),
                                                          N(3136, 3136, 120, 0, VOL, PLUCK)};
static const struct beeper_note_s material_boot_pad[]  = {N(2093, 2093, 120, 0, VOL, PLUCK)};
static const struct beeper_voice_s material_boot[]     = {V(material_boot_lead),
                                                          VAT(material_boot_pad, 110)};

/* Somebody is at the door -- MQTT only, see ui_beep.hpp. Two struck tones a
 * fourth apart, high then low, and one voice: a doorbell is a doorbell, and
 * this family does not decorate. */
MONO(material_door_chime, N(2637, 2637, 90, 25, VOL, PLUCK),
                          N(2093, 2093, 170, 0, VOL, PLUCK));

#define X(name, sym) CHIME(material_##sym),
const struct ui_chime_set_s ui_chime_material = {{UI_SOUND_LIST(X)}};
#undef X

/* ------------------------------------------------------------------ LCARS
 *
 * Reworked alongside the sequencer's copy, and to the same brief: get as close
 * to a TNG console as one pin allows. The reasoning behind every contour,
 * rhythm and register here is written out at length in ui_beep_tables_seq.cpp,
 * because it is editorial rather than a property of either engine, and one copy
 * of it is enough. In short: stepped rather than swept, falling as often as
 * rising, short, and in one bright register with the alerts breaking out of it
 * downwards.
 *
 * What this engine adds is the stacking, and it is spent where the show's
 * panels are audibly an interval rather than a tone -- the toggles, the
 * acknowledgements, the panel clusters, the standing alert and the note the
 * boot sequence lands on. The blips, the stutter, the keys and the klaxon stay
 * single voices: two of them are too short for the ear to fuse anything out of
 * six interleave slots, one is about rhythm rather than pitch, and the last
 * lives entirely below BEEPER_POLY_MIN_HZ. */

/* The contact tap, layer one. One dry blip, one voice: a twelve-millisecond
 * note is six interleave slots, and layer one is not allowed to say anything
 * except "the glass felt you" in any case. */
MONO(lcars_press,     N(2349, 2349, 12, 0, LOW, PLUCK));

/* Keys are dry single blips. Direction is the whole of the difference. */
MONO(lcars_tick,      N(2794, 2794, 16, 0, LOW, PLUCK));
MONO(lcars_tick_back, N(2093, 2093, 16, 0, LOW, PLUCK));

/* A control actuated: two steps a fourth apart, each doubled a fourth up. The
 * falling one is the console sound everybody can hum, so it goes on the
 * gesture that is not affirmative. */
static const struct beeper_note_s lcars_on_a[]  = {N(1976, 1976, 22, 6, VOL, PLUCK),
                                                   N(2637, 2637, 30, 0, VOL, PLUCK)};
static const struct beeper_note_s lcars_on_b[]  = {N(2637, 2637, 22, 6, VOL, PLUCK),
                                                   N(3520, 3520, 30, 0, VOL, PLUCK)};
static const struct beeper_voice_s lcars_toggle_on[] = {V(lcars_on_a), V(lcars_on_b)};

static const struct beeper_note_s lcars_off_a[] = {N(2637, 2637, 22, 6, VOL, PLUCK),
                                                   N(1976, 1976, 30, 0, VOL, PLUCK)};
static const struct beeper_note_s lcars_off_b[] = {N(3520, 3520, 22, 6, VOL, PLUCK),
                                                   N(2637, 2637, 30, 0, VOL, PLUCK)};
static const struct beeper_voice_s lcars_toggle_off[] = {V(lcars_off_a), V(lcars_off_b)};

/* Input registered: three narrow steps, over before the finger is. Steps of a
 * tone rather than of a fourth, which is what keeps it from being heard as the
 * toggle above. */
static const struct beeper_note_s lcars_chg_a[] = {N(2093, 2093, 16, 5, VOL, PLUCK),
                                                   N(2349, 2349, 16, 5, VOL, PLUCK),
                                                   N(2794, 2794, 22, 0, VOL, PLUCK)};
static const struct beeper_note_s lcars_chg_b[] = {N(2794, 2794, 16, 5, VOL, PLUCK),
                                                   N(3136, 3136, 16, 5, VOL, PLUCK),
                                                   N(3725, 3725, 22, 0, VOL, PLUCK)};
static const struct beeper_voice_s lcars_change[] = {V(lcars_chg_a), V(lcars_chg_b)};

/* The computer acknowledging an instruction: two blips and a held third. The
 * second voice drops out for the hold rather than doubling it -- a chord that
 * resolves to one tone is a gesture, and a fourth above 3136 is outside the
 * band anyway. */
static const struct beeper_note_s lcars_acc_a[] = {N(2093, 2093, 20, 6, VOL, PLUCK),
                                                   N(2637, 2637, 20, 6, VOL, PLUCK),
                                                   N(3136, 3136, 70, 0, VOL, PLUCK)};
static const struct beeper_note_s lcars_acc_b[] = {N(2794, 2794, 20, 6, VOL, PLUCK),
                                                   N(3520, 3520, 20, 0, VOL, PLUCK)};
static const struct beeper_voice_s lcars_accept[] = {V(lcars_acc_a), V(lcars_acc_b)};

static const struct beeper_note_s lcars_can_a[] = {N(2637, 2637, 20, 6, VOL, PLUCK),
                                                   N(2093, 2093, 20, 6, VOL, PLUCK),
                                                   N(1568, 1568, 70, 0, VOL, PLUCK)};
static const struct beeper_note_s lcars_can_b[] = {N(3520, 3520, 20, 6, VOL, PLUCK),
                                                   N(2794, 2794, 20, 0, VOL, PLUCK)};
static const struct beeper_voice_s lcars_cancel[] = {V(lcars_can_a), V(lcars_can_b)};

/* Moving about inside a panel: the stutter. Two strikes on one pitch and then a
 * step away from it. One voice, because what tells this from the toggle is its
 * rhythm and a second voice would only blur it. */
MONO(lcars_link,      N(2093, 2093, 12, 8, VOL, PLUCK),
                      N(2093, 2093, 12, 8, VOL, PLUCK),
                      N(3136, 3136, 26, 0, VOL, PLUCK));
MONO(lcars_link_back, N(3136, 3136, 12, 8, VOL, PLUCK),
                      N(3136, 3136, 12, 8, VOL, PLUCK),
                      N(2093, 2093, 26, 0, VOL, PLUCK));

/* A surface arriving over the one you were on: two rising pairs, the second
 * starting above the first and landing held, both doubled. */
static const struct beeper_note_s lcars_open_a[] = {N(1760, 1760, 14,  5, VOL, PLUCK),
                                                    N(2637, 2637, 14, 12, VOL, PLUCK),
                                                    N(2093, 2093, 14,  5, VOL, PLUCK),
                                                    N(3136, 3136, 34,  0, VOL, PLUCK)};
static const struct beeper_note_s lcars_open_b[] = {N(2349, 2349, 14,  5, VOL, PLUCK),
                                                    N(3520, 3520, 14, 12, VOL, PLUCK),
                                                    N(2794, 2794, 14,  5, VOL, PLUCK),
                                                    N(3725, 3725, 34,  0, VOL, PLUCK)};
static const struct beeper_voice_s lcars_screen[] = {V(lcars_open_a), V(lcars_open_b)};

static const struct beeper_note_s lcars_shut_a[] = {N(3136, 3136, 14,  5, VOL, PLUCK),
                                                    N(2093, 2093, 14, 12, VOL, PLUCK),
                                                    N(2637, 2637, 14,  5, VOL, PLUCK),
                                                    N(1760, 1760, 34,  0, VOL, PLUCK)};
static const struct beeper_note_s lcars_shut_b[] = {N(3725, 3725, 14,  5, VOL, PLUCK),
                                                    N(2794, 2794, 14, 12, VOL, PLUCK),
                                                    N(3520, 3520, 14,  5, VOL, PLUCK),
                                                    N(2349, 2349, 34,  0, VOL, PLUCK)};
static const struct beeper_voice_s lcars_screen_out[] = {V(lcars_shut_a), V(lcars_shut_b)};

/* The hail: the one rise in this family worth a sweep rather than steps, onto a
 * held tone with a fourth arriving under it. Linear in hertz is all this engine
 * sweeps -- the sequencer's copy glides through the period instead, which is
 * the one audible difference between the two versions of this sound. */
static const struct beeper_note_s lcars_note_a[] = {N(2093, 3136, 50, 10, VOL, PLUCK),
                                                    N(3520, 3520, 60, 0, VOL, PLUCK)};
static const struct beeper_note_s lcars_note_b[] = {N(2637, 2637, 60, 0, VOL, PLUCK)};
static const struct beeper_voice_s lcars_notify[] = {V(lcars_note_a),
                                                     VAT(lcars_note_b, 60)};

/* The standing alert: an alternating two-tone cadence, each note carrying a
 * tritone over it. Not a klaxon -- a klaxon is what the next one is -- but a
 * thing that will not stop until somebody deals with it. The sequencer warbles
 * the lower note instead; a stacked tritone is what this engine has and it is
 * the interval that never sounds like good news. */
static const struct beeper_note_s lcars_warn_a[] = {N(1568, 1568, 95, 45, VOL, FLAT),
                                                    N(1319, 1319, 95, 45, VOL, FLAT),
                                                    N(1568, 1568, 95, 45, VOL, FLAT),
                                                    N(1319, 1319, 110, 0, VOL, FLAT)};
static const struct beeper_note_s lcars_warn_b[] = {N(2217, 2217, 95, 45, VOL, FLAT),
                                                    N(1865, 1865, 95, 45, VOL, FLAT),
                                                    N(2217, 2217, 95, 45, VOL, FLAT),
                                                    N(1865, 1865, 110, 0, VOL, FLAT)};
static const struct beeper_voice_s lcars_warning[] = {V(lcars_warn_a), V(lcars_warn_b)};

/* The klaxon. A whoop that falls an octave, twice, and the whole of it below
 * the piezo's good band -- which is the exemption may_go_low() exists for, and
 * also why it has to stay a single voice: everything in it is under
 * BEEPER_POLY_MIN_HZ. Being impossible to ignore is worth more here than being
 * loud. */
MONO(lcars_error, N(660, 330, 230, 90, VOL, FLAT),
                  N(660, 330, 230, 0, VOL, FLAT));

/* Coming online: three steps up and a held note, with a fourth joining it at
 * the moment it lands. The only chime here long enough to be a statement
 * rather than an acknowledgement. */
static const struct beeper_note_s lcars_boot_a[] = {N(1568, 1568, 40, 10, VOL, PLUCK),
                                                    N(1976, 1976, 40, 10, VOL, PLUCK),
                                                    N(2349, 2349, 40, 10, VOL, PLUCK),
                                                    N(2794, 2794, 150, 0, VOL, PLUCK)};
static const struct beeper_note_s lcars_boot_b[] = {N(3725, 3725, 150, 0, VOL, PLUCK)};
static const struct beeper_voice_s lcars_boot[]  = {V(lcars_boot_a),
                                                    VAT(lcars_boot_b, 150)};

/* The tap that woke the display, and the whole of the feedback for it. The
 * falling two-tone, at the level that is audible in a dark room without being
 * an announcement. */
MONO(lcars_wake, N(2794, 2794, 14, 0, MED, PLUCK),
                 N(2093, 2093, 20, 0, MED, PLUCK));

/* Somebody is at the door -- MQTT only, see ui_beep.hpp. The one sound in this
 * family that is not a console: two tones a fourth apart, high then low, the
 * second left ringing with a fourth under it. A door announces a person and a
 * console answers a finger, and they should not sound alike across a room. */
static const struct beeper_note_s lcars_door_a[] = {N(2637, 2637, 160, 30, VOL, PLUCK),
                                                    N(1976, 1976, 260, 0, VOL, PLUCK)};
static const struct beeper_note_s lcars_door_b[] = {N(1568, 1568, 260, 0, VOL, PLUCK)};
static const struct beeper_voice_s lcars_door_chime[] = {V(lcars_door_a),
                                                         VAT(lcars_door_b, 190)};

#define X(name, sym) CHIME(lcars_##sym),
const struct ui_chime_set_s ui_chime_lcars = {{UI_SOUND_LIST(X)}};
#undef X

/* ----------------------------------------------------------------- Reticle
 *
 * Swells rather than blips: everything is a PAD, everything is a sweep, and
 * nothing has a hard edge. Affirmative rises, dismissal falls, and the moments
 * that matter arrive as triads. */

static const struct beeper_note_s hud_press_a[] = {N(2900, 3100, 20, 0, LOW, PAD)};
static const struct beeper_note_s hud_press_b[] = {N(3867, 4000, 20, 0, LOW, PAD)};
static const struct beeper_voice_s hud_press[]  = {V(hud_press_a), V(hud_press_b)};

MONO(hud_tick,      N(3000, 3100, 18, 0, LOW, PAD));
MONO(hud_tick_back, N(2600, 2500, 18, 0, LOW, PAD));

static const struct beeper_note_s hud_on_a[]  = {N(2200, 2400, 90, 0, VOL, PAD)};
static const struct beeper_note_s hud_on_b[]  = {N(2750, 3000, 90, 0, VOL, PAD)};
static const struct beeper_voice_s hud_toggle_on[] = {V(hud_on_a), V(hud_on_b)};

static const struct beeper_note_s hud_off_a[] = {N(2400, 2200, 90, 0, VOL, PAD)};
static const struct beeper_note_s hud_off_b[] = {N(3000, 2750, 90, 0, VOL, PAD)};
static const struct beeper_voice_s hud_toggle_off[] = {V(hud_off_a), V(hud_off_b)};

static const struct beeper_note_s hud_chg_a[] = {N(2200, 2800, 110, 0, VOL, PAD)};
static const struct beeper_note_s hud_chg_b[] = {N(2933, 3733, 110, 0, VOL, PAD)};
static const struct beeper_voice_s hud_change[] = {V(hud_chg_a), V(hud_chg_b)};

/* A rising figure that arrives on a C major triad. */
static const struct beeper_note_s hud_acc_a[] = {N(1568, 1568, 60, 0, VOL, PAD),
                                                 N(2093, 2093, 140, 0, VOL, PAD)};
static const struct beeper_note_s hud_acc_b[] = {N(2637, 2637, 140, 0, VOL, PAD)};
static const struct beeper_note_s hud_acc_c[] = {N(3136, 3136, 140, 0, VOL, PAD)};
static const struct beeper_voice_s hud_accept[] = {V(hud_acc_a), VAT(hud_acc_b, 60),
                                                   VAT(hud_acc_c, 60)};

static const struct beeper_note_s hud_can_a[] = {N(2093, 2093, 60, 0, VOL, PAD),
                                                 N(1568, 1568, 140, 0, VOL, PAD)};
static const struct beeper_note_s hud_can_b[] = {N(2637, 2637, 60, 0, VOL, PAD)};
static const struct beeper_voice_s hud_cancel[] = {V(hud_can_a), V(hud_can_b)};

static const struct beeper_note_s hud_link_a[] = {N(1600, 2500, 170, 0, VOL, PAD)};
static const struct beeper_note_s hud_link_b[] = {N(2400, 3750, 170, 0, VOL, PAD)};
static const struct beeper_voice_s hud_link[]  = {V(hud_link_a), V(hud_link_b)};

static const struct beeper_note_s hud_back_a[] = {N(2500, 1600, 170, 0, VOL, PAD)};
static const struct beeper_note_s hud_back_b[] = {N(3750, 2400, 170, 0, VOL, PAD)};
static const struct beeper_voice_s hud_link_back[] = {V(hud_back_a), V(hud_back_b)};

/* The holographic panel materialising: a G major triad, one voice at a time. */
static const struct beeper_note_s hud_open_a[] = {N(1568, 1568, 200, 0, VOL, PAD)};
static const struct beeper_note_s hud_open_b[] = {N(1976, 1976, 160, 0, VOL, PAD)};
static const struct beeper_note_s hud_open_c[] = {N(2349, 2349, 120, 0, VOL, PAD)};
static const struct beeper_voice_s hud_screen[] = {V(hud_open_a), VAT(hud_open_b, 40),
                                                   VAT(hud_open_c, 80)};

/* And thinning back to one as it goes. */
static const struct beeper_note_s hud_shut_a[] = {N(2349, 2349, 80, 0, VOL, PAD)};
static const struct beeper_note_s hud_shut_b[] = {N(1976, 1976, 140, 0, VOL, PAD)};
static const struct beeper_note_s hud_shut_c[] = {N(1568, 1568, 200, 0, VOL, PAD)};
static const struct beeper_voice_s hud_screen_out[] = {V(hud_shut_a), V(hud_shut_b),
                                                       V(hud_shut_c)};

static const struct beeper_note_s hud_note_a[] = {N(1760, 2093, 120, 0, VOL, PAD)};
static const struct beeper_note_s hud_note_b[] = {N(2637, 3136, 120, 0, VOL, PAD)};
static const struct beeper_voice_s hud_notify[] = {V(hud_note_a), V(hud_note_b)};

/* A minor second, pulsing. Tension rather than a klaxon: this family does not
 * shout, it worries. */
static const struct beeper_note_s hud_warn_a[] = {N(1568, 1568, 120, 60, VOL, PAD),
                                                  N(1568, 1568, 120, 0, VOL, PAD)};
static const struct beeper_note_s hud_warn_b[] = {N(1661, 1661, 120, 60, VOL, PAD),
                                                  N(1661, 1661, 120, 0, VOL, PAD)};
static const struct beeper_voice_s hud_warning[] = {V(hud_warn_a), V(hud_warn_b)};

/* A tritone falling away. It stays above BEEPER_POLY_MIN_HZ so that it can
 * keep both voices -- this family's alarm is a chord, not a whoop. */
static const struct beeper_note_s hud_err_a[] = {N(1400, 1050, 320, 0, VOL, PAD)};
static const struct beeper_note_s hud_err_b[] = {N(1980, 1485, 320, 0, VOL, PAD)};
static const struct beeper_voice_s hud_error[] = {V(hud_err_a), V(hud_err_b)};

/* Coming online: a long swell that resolves into a triad. */
static const struct beeper_note_s hud_boot_a[] = {N(1100, 1568, 300, 0, VOL, PAD),
                                                  N(1568, 1568, 300, 0, VOL, PAD)};
static const struct beeper_note_s hud_boot_b[] = {N(1976, 1976, 300, 0, VOL, PAD)};
static const struct beeper_note_s hud_boot_c[] = {N(2349, 2349, 220, 0, VOL, PAD)};
static const struct beeper_voice_s hud_boot[]  = {V(hud_boot_a), VAT(hud_boot_b, 300),
                                                  VAT(hud_boot_c, 380)};

static const struct beeper_note_s hud_wake_a[] = {N(2200, 2400, 80, 0, MED, PAD)};
static const struct beeper_note_s hud_wake_b[] = {N(2933, 3200, 80, 0, MED, PAD)};
static const struct beeper_voice_s hud_wake[]  = {V(hud_wake_a), V(hud_wake_b)};

/* Somebody is at the door -- MQTT only, see ui_beep.hpp. This family does not
 * strike things, so its doorbell arrives and thins rather than ringing: a
 * third over the first tone, and another over the second as it settles. */
static const struct beeper_note_s hud_door_a[] = {N(2093, 2093, 140, 20, VOL, PAD),
                                                  N(1568, 1568, 240, 0, VOL, PAD)};
static const struct beeper_note_s hud_door_b[] = {N(2637, 2637, 140, 0, VOL, PAD)};
static const struct beeper_note_s hud_door_c[] = {N(1976, 1976, 240, 0, VOL, PAD)};
static const struct beeper_voice_s hud_door_chime[] = {V(hud_door_a), V(hud_door_b),
                                                       VAT(hud_door_c, 160)};

#define X(name, sym) CHIME(hud_##sym),
const struct ui_chime_set_s ui_chime_jarvis = {{UI_SOUND_LIST(X)}};
#undef X

/* --------------------------------------------------------------- Classic
 *
 * Not written, recovered. The blue-on-silver UI had six macros of fixed-pitch
 * square waves in ui_beep.hpp, gated abruptly on and off and shared by every
 * theme, and this is those six notes at those six pitches: C7 2093, E7 2637,
 * G7 3136, A6 1760, and the low E3 165 / C3 131 pair the error used.
 *
 * FLAT throughout, which is not laziness: a square wave switched on at full
 * duty and off again is exactly what FLAT is, and the click at both ends is
 * what made the old panel sound like a doorbell. Giving it a PLUCK would make
 * it sound better and stop it being this theme.
 *
 * Eighteen events, six figures. The original had one macro per *kind* of thing
 * and called it from wherever that kind happened -- BEEPER_EVENT_CHANGE() from
 * a value, a toggle and a settings field alike -- so the repetition below is
 * the mapping those call sites had, not a table half filled in. The figures
 * are named so it can be read at a glance.
 *
 * Two departures, and both are the panel's own policy overruling the artefact.
 * The original played everything at one level; here the same blip appears at
 * LOW when it is the glass acknowledging a finger and at VOL when it is the
 * outcome of one, with the wake blip at MED between them. A press as loud as
 * an outcome is the fastest way to make this panel unpleasant -- ui_beep.hpp
 * says so, and the chime suite enforces it. And
 * WARNING cannot have the error's low pair, because only UI_SOUND_ERROR is
 * exempt from the piezo band; it takes the falling figure instead, which is the
 * most downward thing the original owned that stays in the band. */

#define CLASSIC_BLIP  N(2093, 2093, 5, 0, LOW, FLAT)
#define CLASSIC_BEEP  N(2093, 2093, 5, 0, VOL, FLAT)
#define CLASSIC_RISE  N(2093, 2093, 20, 10, VOL, FLAT),                        \
                      N(2637, 2637, 10, 0, VOL, FLAT)
#define CLASSIC_FALL  N(2637, 2637, 10, 5, VOL, FLAT),                         \
                      N(2093, 2093, 10, 5, VOL, FLAT),                         \
                      N(1760, 1760, 20, 0, VOL, FLAT)
#define CLASSIC_OPEN  N(2093, 2093, 10, 0, VOL, FLAT),                         \
                      N(2637, 2637, 10, 0, VOL, FLAT),                         \
                      N(3136, 3136, 20, 0, VOL, FLAT)
#define CLASSIC_CLOSE N(3136, 3136, 10, 0, VOL, FLAT),                         \
                      N(2637, 2637, 10, 0, VOL, FLAT),                         \
                      N(2093, 2093, 20, 0, VOL, FLAT)
#define CLASSIC_ALERT N(165, 165, 50, 0, VOL, FLAT),                           \
                      N(131, 131, 100, 0, VOL, FLAT)

/* Contact, and the two grain sounds the original did not distinguish. */
MONO(classic_press,       CLASSIC_BLIP);
MONO(classic_tick,        CLASSIC_BLIP);
MONO(classic_tick_back,   CLASSIC_BLIP);

/* A switch, a value, a settings field: all BEEPER_EVENT_CHANGE() once, and all
 * outcomes rather than contact, so the same note at the foreground level. */
MONO(classic_toggle_on,   CLASSIC_BEEP);
MONO(classic_toggle_off,  CLASSIC_BEEP);
MONO(classic_change,      CLASSIC_BEEP);

/* Committing and dismissing both closed a window, and sounded like it. */
MONO(classic_accept,      CLASSIC_CLOSE);
MONO(classic_cancel,      CLASSIC_CLOSE);

/* The two the original was most particular about: a rising fourth to go in,
 * and a three-note fall to come back out. */
MONO(classic_link,        CLASSIC_RISE);
MONO(classic_link_back,   CLASSIC_FALL);

/* The C-E-G arpeggio a window opened with, and its retrograde. */
MONO(classic_screen,      CLASSIC_OPEN);
MONO(classic_screen_out,  CLASSIC_CLOSE);

MONO(classic_notify,      CLASSIC_OPEN);
MONO(classic_warning,     CLASSIC_FALL);
MONO(classic_error,       CLASSIC_ALERT);

MONO(classic_boot,        CLASSIC_OPEN);
MONO(classic_wake,        N(2093, 2093, 5, 0, MED, FLAT));

/* The one sound with nothing to recover: MQTT and the door chime both postdate
 * this look by years. Two struck tones a fourth apart, high then low, from the
 * only notes the original ever played -- a doorbell built out of its own
 * vocabulary rather than borrowed off the arpeggio next to it. */
MONO(classic_door_chime,  N(2093, 2093, 90, 25, VOL, FLAT),
                          N(1760, 1760, 170, 0, VOL, FLAT));

#define X(name, sym) CHIME(classic_##sym),
const struct ui_chime_set_s ui_chime_classic = {{UI_SOUND_LIST(X)}};
#undef X

#undef CLASSIC_BLIP
#undef CLASSIC_BEEP
#undef CLASSIC_RISE
#undef CLASSIC_FALL
#undef CLASSIC_OPEN
#undef CLASSIC_CLOSE
#undef CLASSIC_ALERT

/* ------------------------------------------------------------ enumeration
 *
 * For the host tests, which walk every family against every entry. The names
 * that go with it are in ui_beep.hpp, because both engines' table files would
 * otherwise define them identically and collide in the one test binary that
 * links both. Nothing on the device reads this: ui_style.cpp's theme table
 * points straight at the three objects. */

const struct ui_chime_set_s *const ui_chime_sets[UI_THEME_FAMILY_COUNT] = {
    &ui_chime_material,
    &ui_chime_lcars,
    &ui_chime_jarvis,
    &ui_chime_classic};

/* No #else, and no stub. icons/icon_set.cpp has one because it declares
 * functions somebody calls; this file declares only data, and an empty
 * translation unit is legal C++. */
#endif /* CONFIG_OHEZ_BEEPER_ENGINE_MIXER */
