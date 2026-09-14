/**
 * @file ui_beep_tables.cpp
 *
 * Three families, three voices -- and now, where it earns itself, more than
 * one at a time.
 *
 * Separate from ui_beep.cpp so that the tables can be linked by the host tests,
 * which have no LVGL and no buzzer: everything here includes ui_beep.hpp and
 * <stdint.h> and nothing else, while ui_beep_play() needs ui_style.hpp and
 * therefore <lvgl.h>. Forty-five hand-written chimes on a target that cannot
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
 * That is why the LCARS and Slate alerts are single voices: they are the only
 * chimes that live down there. test_ui_beep.cpp enforces it.
 *
 * ---------------------------------------------------------------- the voices
 *
 * LCARS uses chords everywhere, because the panel blips in the show are
 * stacked intervals rather than tones -- a fourth or a fifth, struck and gone
 * -- and because the grain interleaving leaves is exactly right for a machine
 * acknowledging an instruction.
 *
 * Reticle uses them for triads and lets slow envelopes hide the grain.
 * Everything is a PAD and everything sweeps; affirmative rises.
 *
 * Slate uses them hardly at all, and that is the design rather than a
 * shortfall. It is the family that must not draw attention, single voices have
 * no grain to hide, and a dyad is saved for the two moments where something
 * arrives or leaves.
 */
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

/* ------------------------------------------------------------------ Slate
 *
 * Restrained, consonant, and mostly one voice. */

MONO(slate_press,     N(2093, 2093, 12, 0, LOW, PLUCK));
MONO(slate_tick,      N(2349, 2349, 10, 0, LOW, PLUCK));
MONO(slate_tick_back, N(1976, 1976, 10, 0, LOW, PLUCK));
MONO(slate_change,    N(2093, 2093, 25, 0, VOL, PLUCK));
MONO(slate_wake,      N(2093, 2093, 40, 0, MED, PLUCK));

/* Two notes a fourth apart, in sequence rather than together: going somewhere
 * is a move, and a move is two things one after the other. */
MONO(slate_link,      N(2093, 2093, 30, 10, VOL, PLUCK),
                      N(2794, 2794, 40, 0, VOL, PLUCK));
MONO(slate_link_back, N(2794, 2794, 30, 10, VOL, PLUCK),
                      N(2093, 2093, 40, 0, VOL, PLUCK));

MONO(slate_notify,    N(2093, 2093, 30, 8, VOL, PLUCK),
                      N(2637, 2637, 45, 0, VOL, PLUCK));
MONO(slate_warning,   N(1568, 1568, 80, 60, VOL, FLAT),
                      N(1568, 1568, 80, 0, VOL, FLAT));

/* The one Slate chime that is deliberately outside the piezo's good band, and
 * the one that has to stay a single voice because of it. */
MONO(slate_error,     N(660, 660, 90, 40, VOL, FLAT),
                      N(440, 440, 180, 0, VOL, FLAT));

/* Arrival and departure: the two places a second voice is worth its grain. */
static const struct beeper_note_s slate_on_lead[]  = {N(2093, 2093, 20, 5, VOL, PLUCK),
                                                      N(2637, 2637, 35, 0, VOL, PLUCK)};
static const struct beeper_note_s slate_on_pad[]   = {N(3136, 3136, 35, 0, VOL, PLUCK)};
static const struct beeper_voice_s slate_toggle_on[] = {V(slate_on_lead),
                                                        VAT(slate_on_pad, 25)};

static const struct beeper_note_s slate_off_lead[] = {N(2637, 2637, 20, 5, VOL, PLUCK),
                                                      N(2093, 2093, 35, 0, VOL, PLUCK)};
static const struct beeper_note_s slate_off_pad[]  = {N(1568, 1568, 35, 0, VOL, PLUCK)};
static const struct beeper_voice_s slate_toggle_off[] = {V(slate_off_lead),
                                                         VAT(slate_off_pad, 25)};

/* A major third, arriving a moment late: "done". */
static const struct beeper_note_s slate_acc_lead[] = {N(2093, 2093, 50, 0, VOL, PLUCK)};
static const struct beeper_note_s slate_acc_pad[]  = {N(2637, 2637, 30, 0, VOL, PLUCK)};
static const struct beeper_voice_s slate_accept[]  = {V(slate_acc_lead),
                                                      VAT(slate_acc_pad, 20)};

static const struct beeper_note_s slate_can_lead[] = {N(2637, 2637, 50, 0, VOL, PLUCK)};
static const struct beeper_note_s slate_can_pad[]  = {N(2093, 2093, 30, 0, VOL, PLUCK)};
static const struct beeper_voice_s slate_cancel[]  = {V(slate_can_lead),
                                                      VAT(slate_can_pad, 20)};

static const struct beeper_note_s slate_open_lead[] = {N(2093, 2093, 25, 5, VOL, PLUCK),
                                                       N(2637, 2637, 25, 5, VOL, PLUCK),
                                                       N(3136, 3136, 45, 0, VOL, PLUCK)};
static const struct beeper_note_s slate_open_pad[]  = {N(2093, 2093, 45, 0, VOL, PLUCK)};
static const struct beeper_voice_s slate_screen[]   = {V(slate_open_lead),
                                                       VAT(slate_open_pad, 60)};

static const struct beeper_note_s slate_shut_lead[] = {N(3136, 3136, 25, 5, VOL, PLUCK),
                                                       N(2637, 2637, 25, 5, VOL, PLUCK),
                                                       N(2093, 2093, 45, 0, VOL, PLUCK)};
static const struct beeper_note_s slate_shut_pad[]  = {N(1568, 1568, 45, 0, VOL, PLUCK)};
static const struct beeper_voice_s slate_screen_out[] = {V(slate_shut_lead),
                                                         VAT(slate_shut_pad, 60)};

static const struct beeper_note_s slate_boot_lead[] = {N(2093, 2093, 45, 10, VOL, PLUCK),
                                                       N(2637, 2637, 45, 10, VOL, PLUCK),
                                                       N(3136, 3136, 120, 0, VOL, PLUCK)};
static const struct beeper_note_s slate_boot_pad[]  = {N(2093, 2093, 120, 0, VOL, PLUCK)};
static const struct beeper_voice_s slate_boot[]     = {V(slate_boot_lead),
                                                       VAT(slate_boot_pad, 110)};

#define X(name, sym) CHIME(slate_##sym),
const struct ui_sound_s ui_sound_default = {{UI_SOUND_LIST(X)}};
#undef X

/* ------------------------------------------------------------------ LCARS
 *
 * Stacked fourths and fifths, struck and gone, plus the chirps that are the
 * whole sound of the thing: a note swept a long way in under a tenth of a
 * second. Nothing eases -- these are machines acknowledging an instruction. */

/* The panel blip. A fifth, because the ones in the show are intervals rather
 * than tones, and that is the single thing this driver could not do before. */
static const struct beeper_note_s lcars_press_a[] = {N(1976, 1976, 18, 0, LOW, PLUCK)};
static const struct beeper_note_s lcars_press_b[] = {N(2960, 2960, 18, 0, LOW, PLUCK)};
static const struct beeper_voice_s lcars_press[]  = {V(lcars_press_a), V(lcars_press_b)};

/* Keypads in the show are dry single blips, so these stay one voice. */
MONO(lcars_tick,      N(2400, 2400, 18, 0, LOW, PLUCK));
MONO(lcars_tick_back, N(2000, 2000, 18, 0, LOW, PLUCK));

static const struct beeper_note_s lcars_on_a[]  = {N(1976, 1976, 20, 8, VOL, PLUCK),
                                                   N(2349, 2349, 28, 0, VOL, PLUCK)};
static const struct beeper_note_s lcars_on_b[]  = {N(2960, 2960, 20, 8, VOL, PLUCK),
                                                   N(3520, 3520, 28, 0, VOL, PLUCK)};
static const struct beeper_voice_s lcars_toggle_on[] = {V(lcars_on_a), V(lcars_on_b)};

static const struct beeper_note_s lcars_off_a[] = {N(2349, 2349, 20, 8, VOL, PLUCK),
                                                   N(1976, 1976, 28, 0, VOL, PLUCK)};
static const struct beeper_note_s lcars_off_b[] = {N(3520, 3520, 20, 8, VOL, PLUCK),
                                                   N(2960, 2960, 28, 0, VOL, PLUCK)};
static const struct beeper_voice_s lcars_toggle_off[] = {V(lcars_off_a), V(lcars_off_b)};

static const struct beeper_note_s lcars_chg_a[] = {N(1800, 1800, 20, 8, VOL, PLUCK),
                                                   N(2600, 2600, 20, 0, VOL, PLUCK)};
static const struct beeper_note_s lcars_chg_b[] = {N(2700, 2700, 20, 8, VOL, PLUCK),
                                                   N(3900, 3900, 20, 0, VOL, PLUCK)};
static const struct beeper_voice_s lcars_change[] = {V(lcars_chg_a), V(lcars_chg_b)};

/* The computer acknowledging: two tones, rising, in parallel fourths. */
static const struct beeper_note_s lcars_acc_a[] = {N(1976, 1976, 30, 10, VOL, PLUCK),
                                                   N(2637, 2637, 50, 0, VOL, PLUCK)};
static const struct beeper_note_s lcars_acc_b[] = {N(2637, 2637, 30, 10, VOL, PLUCK),
                                                   N(3520, 3520, 50, 0, VOL, PLUCK)};
static const struct beeper_voice_s lcars_accept[] = {V(lcars_acc_a), V(lcars_acc_b)};

static const struct beeper_note_s lcars_can_a[] = {N(2637, 2637, 30, 10, VOL, PLUCK),
                                                   N(1976, 1976, 50, 0, VOL, PLUCK)};
static const struct beeper_note_s lcars_can_b[] = {N(3520, 3520, 30, 10, VOL, PLUCK),
                                                   N(2637, 2637, 50, 0, VOL, PLUCK)};
static const struct beeper_voice_s lcars_cancel[] = {V(lcars_can_a), V(lcars_can_b)};

/* The chirp, doubled a fourth up: one note swept most of the band in ninety
 * milliseconds, which is the "working" sound. */
static const struct beeper_note_s lcars_link_a[] = {N(1200, 2800, 90, 0, VOL, PLUCK)};
static const struct beeper_note_s lcars_link_b[] = {N(1600, 3730, 90, 0, VOL, PLUCK)};
static const struct beeper_voice_s lcars_link[]  = {V(lcars_link_a), V(lcars_link_b)};

static const struct beeper_note_s lcars_back_a[] = {N(2800, 1200, 90, 0, VOL, PLUCK)};
static const struct beeper_note_s lcars_back_b[] = {N(3730, 1600, 90, 0, VOL, PLUCK)};
static const struct beeper_voice_s lcars_link_back[] = {V(lcars_back_a), V(lcars_back_b)};

static const struct beeper_note_s lcars_open_a[] = {N(1400, 1400, 25, 8, VOL, PLUCK),
                                                    N(1900, 1900, 25, 8, VOL, PLUCK),
                                                    N(2500, 2900, 55, 0, VOL, PLUCK)};
static const struct beeper_note_s lcars_open_b[] = {N(2100, 2100, 25, 8, VOL, PLUCK),
                                                    N(2850, 2850, 25, 8, VOL, PLUCK),
                                                    N(3750, 3900, 55, 0, VOL, PLUCK)};
static const struct beeper_voice_s lcars_screen[] = {V(lcars_open_a), V(lcars_open_b)};

static const struct beeper_note_s lcars_shut_a[] = {N(2500, 2500, 25, 8, VOL, PLUCK),
                                                    N(1900, 1900, 25, 8, VOL, PLUCK),
                                                    N(1400, 1100, 55, 0, VOL, PLUCK)};
static const struct beeper_note_s lcars_shut_b[] = {N(3750, 3750, 25, 8, VOL, PLUCK),
                                                    N(2850, 2850, 25, 8, VOL, PLUCK),
                                                    N(2100, 1650, 55, 0, VOL, PLUCK)};
static const struct beeper_voice_s lcars_screen_out[] = {V(lcars_shut_a), V(lcars_shut_b)};

static const struct beeper_note_s lcars_note_a[] = {N(1760, 2093, 60, 0, VOL, PLUCK)};
static const struct beeper_note_s lcars_note_b[] = {N(2637, 3136, 60, 0, VOL, PLUCK)};
static const struct beeper_voice_s lcars_notify[] = {V(lcars_note_a), V(lcars_note_b)};

/* A tritone, held: the interval that never sounds like good news. */
static const struct beeper_note_s lcars_warn_a[] = {N(1400, 1400, 90, 50, VOL, FLAT),
                                                    N(1400, 1400, 90, 0, VOL, FLAT)};
static const struct beeper_note_s lcars_warn_b[] = {N(1980, 1980, 90, 50, VOL, FLAT),
                                                    N(1980, 1980, 90, 0, VOL, FLAT)};
static const struct beeper_voice_s lcars_warning[] = {V(lcars_warn_a), V(lcars_warn_b)};

/* The red alert cadence, near enough: two low whoops, evenly spaced. A single
 * voice, and it has to be -- everything here is below BEEPER_POLY_MIN_HZ. */
MONO(lcars_error, N(520, 380, 200, 60, VOL, FLAT),
                  N(520, 380, 200, 0, VOL, FLAT));

/* Four blips up onto a held fourth: the computer coming online, which is the
 * one chime long enough to be a statement rather than an acknowledgement. */
static const struct beeper_note_s lcars_boot_a[] = {N(1400, 1400, 45, 10, VOL, PLUCK),
                                                    N(1760, 1760, 45, 10, VOL, PLUCK),
                                                    N(2093, 2093, 45, 10, VOL, PLUCK),
                                                    N(2637, 2637, 140, 0, VOL, PLUCK)};
static const struct beeper_note_s lcars_boot_b[] = {N(3520, 3520, 140, 0, VOL, PLUCK)};
static const struct beeper_voice_s lcars_boot[]  = {V(lcars_boot_a),
                                                    VAT(lcars_boot_b, 165)};

static const struct beeper_note_s lcars_wake_a[] = {N(1976, 1976, 35, 0, MED, PLUCK)};
static const struct beeper_note_s lcars_wake_b[] = {N(2960, 2960, 35, 0, MED, PLUCK)};
static const struct beeper_voice_s lcars_wake[]  = {V(lcars_wake_a), V(lcars_wake_b)};

#define X(name, sym) CHIME(lcars_##sym),
const struct ui_sound_s ui_sound_lcars = {{UI_SOUND_LIST(X)}};
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

#define X(name, sym) CHIME(hud_##sym),
const struct ui_sound_s ui_sound_jarvis = {{UI_SOUND_LIST(X)}};
#undef X

/* ------------------------------------------------------------ enumeration
 *
 * For the host tests, which walk every family against every entry and need
 * something to put in the failure message. Nothing on the device reads either
 * of these: ui_style.cpp's theme table points straight at the three objects. */

const char *const ui_sound_names[UI_SOUND_COUNT] = {
#define X(name, sym) #sym,
    UI_SOUND_LIST(X)
#undef X
};

const struct ui_sound_s *const ui_sound_sets[UI_THEME_FAMILY_COUNT] = {
    &ui_sound_default,
    &ui_sound_lcars,
    &ui_sound_jarvis};
