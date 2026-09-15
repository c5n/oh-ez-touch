/**
 * @file ui_beep_tables_seq.cpp
 *
 * Three families, one voice -- and everything that would have gone on a second
 * one spent on the note instead.
 *
 * The expressive engine's tables -- see CONFIG_OHEZ_BEEPER_ENGINE. The whole
 * file is guarded, because the polyphonic engine has its own fifty-one in
 * ui_beep_tables.cpp and a panel carries only the set it plays. Both are linked
 * at once by the host tests, which is why the two export different names.
 *
 * Separate from ui_beep.cpp so that the tables can be linked by those tests,
 * which have no LVGL and no buzzer: everything here includes ui_beep.hpp and
 * nothing else, while ui_beep_play() needs ui_style.hpp and therefore <lvgl.h>.
 *
 * ------------------------------------------------------------------ the band
 *
 * Frequencies sit between about 1.1 and 3.9 kHz because that is where a small
 * piezo is loudest -- a note an octave lower is not quieter on paper and is
 * much quieter in a room. The error sounds deliberately break that: being hard
 * to ignore matters more than being loud, and a klaxon that sounds like the
 * rest of the interface is not a klaxon.
 *
 * There is no polyphony floor here, because there is no polyphony. What
 * replaces it is the vibrato excursion: a note written at 3.9 kHz with a deep
 * enough effect row swings out of the band on its own, and
 * beeper_seq_freq_range() counts that so the band test catches it.
 *
 * ------------------------------------------------------------- what was lost
 *
 * The mixer said a chord. This engine says an arpeggio: two notes four to ten
 * milliseconds apart, the second landing on the first's decay. That reads as an
 * interval to most ears and it is not the same thing, and LCARS is where the
 * difference is real -- the blips in the show are stacked fourths and fifths.
 * That family is the reason the mixer is still one menuconfig entry away.
 *
 * Where the mixer had a voice enter late -- a fifth joining a root twenty-five
 * milliseconds in -- the note simply follows, which is a gesture this panel
 * already used for Slate's link sound.
 *
 * ---------------------------------------------------------------- the voices
 *
 * Slate is restrained, consonant, and short. It must not draw attention, so it
 * gains the least from the new engine and is written to gain the least.
 *
 * LCARS is struck and gone: nothing eases, everything is dry, and the chirps --
 * a note swept most of the band in under a tenth of a second -- are the whole
 * sound of the thing.
 *
 * Reticle swells: everything eases, everything sweeps, affirmative rises and
 * dismissal falls. It is the family with the most to gain here, because a slow
 * synthetic voice is exactly what a vibrato is an ornament for.
 */
#include "sdkconfig.h"

#if CONFIG_OHEZ_BEEPER_ENGINE_SEQ

#include "ui_beep.hpp"

/* f_start, f_end, duration, pause, volume, envelope, effect */
#define N(fs, fe, d, p, v, e, x)                                               \
    {                                                                          \
        (uint16_t)(fs), (uint16_t)(fe), (uint16_t)(d), (uint16_t)(p),          \
            (uint8_t)(v), (uint8_t)(e), (uint8_t)(x), 1                        \
    }

/* The same, struck `r` times. The envelope and both LFOs restart on every
 * pass, so this is a trill and not one long note with gaps cut into it. */
#define NR(fs, fe, d, p, v, e, x, r)                                           \
    {                                                                          \
        (uint16_t)(fs), (uint16_t)(fe), (uint16_t)(d), (uint16_t)(p),          \
            (uint8_t)(v), (uint8_t)(e), (uint8_t)(x), (uint8_t)(r)             \
    }

/* A steady note with no effect, which is most of them: spelled without
 * repeating the frequency and without naming NONE. */
#define T(f, d, p, v, e) N(f, f, d, p, v, e, NONE)

/* A tune, in one declaration. `name##_n` is what the X-macro at the foot of
 * each family picks up. */
#define TUNE(name, ...)                                                        \
    static const struct beeper_seq_note_s name##_n[] = {__VA_ARGS__}

#define SEQ(a) {(a), (uint8_t)(sizeof(a) / sizeof((a)[0]))}

/* The level a note carries is its balance against the other notes of its tune,
 * not its loudness in the room -- the config's master volume is what decides
 * that. Deliberately the same three numbers the chime tables use: the balance
 * between the contact layer and everything else is a property of the interface,
 * not of the engine underneath it. */
#define VOL  50 /* a note in the foreground                                */
#define MED  30 /* the wake blip: it goes off in a dark room               */
#define LOW  17 /* the contact layer -- see the policy in ui_beep.hpp      */

/* Short names for the preset rows. These tables are read as music, and
 * BEEPER_SEQ_ENV_ in front of each of two hundred notes is noise. */
#define FLAT    BEEPER_SEQ_ENV_FLAT
#define CLICK   BEEPER_SEQ_ENV_CLICK
#define PLUCK   BEEPER_SEQ_ENV_PLUCK
#define PAD     BEEPER_SEQ_ENV_PAD
#define STAB    BEEPER_SEQ_ENV_STAB
#define BELL    BEEPER_SEQ_ENV_BELL
#define SWELL   BEEPER_SEQ_ENV_SWELL
#define BLOOM   BEEPER_SEQ_ENV_BLOOM

#define NONE    BEEPER_SEQ_FX_NONE
#define GLIDE   BEEPER_SEQ_FX_GLIDE
#define SHIMMER BEEPER_SEQ_FX_SHIMMER
#define WOBBLE  BEEPER_SEQ_FX_WOBBLE
#define SIREN   BEEPER_SEQ_FX_SIREN
#define BREATHE BEEPER_SEQ_FX_BREATHE
#define PULSE   BEEPER_SEQ_FX_PULSE
#define CHIRP   BEEPER_SEQ_FX_CHIRP

/* ------------------------------------------------------------------ Slate
 *
 * Restrained and consonant, and that is the design rather than a shortfall: it
 * is the family that must not draw attention, so it is also the one with the
 * least to gain here and is written to take the least.
 *
 * It takes two things. The notes that *resolve* -- the second of a pair, the
 * last of a rise -- become BELL rather than PLUCK: an instant attack is a click
 * at the onset, and four milliseconds of attack is the difference between a
 * struck thing and a tapped one. And the warning is a repeat rather than two
 * notes, which is what `repeat` is for.
 *
 * Nothing here sweeps, nothing wobbles. Slate has no use for either. */

TUNE(slate_press,     T(2093, 12, 0, LOW, PLUCK));
TUNE(slate_tick,      T(2349, 10, 0, LOW, PLUCK));
TUNE(slate_tick_back, T(1976, 10, 0, LOW, PLUCK));
TUNE(slate_change,    T(2093, 25, 0, VOL, PLUCK));
TUNE(slate_wake,      T(2093, 40, 0, MED, BELL));

/* Two notes a fourth apart, in sequence: going somewhere is a move, and a move
 * is two things one after the other. The second one lands rather than strikes. */
TUNE(slate_link,      T(2093, 30, 10, VOL, PLUCK),
                      T(2794, 40,  0, VOL, BELL));
TUNE(slate_link_back, T(2794, 30, 10, VOL, PLUCK),
                      T(2093, 40,  0, VOL, BELL));

TUNE(slate_notify,    T(2093, 30, 8, VOL, PLUCK),
                      T(2637, 45, 0, VOL, BELL));

/* Two strikes, said once. The mixer needed two notes written out for this;
 * a repeat is what a cadence is, and the envelope restarts on the second. */
TUNE(slate_warning,   NR(1568, 1568, 80, 60, VOL, FLAT, NONE, 2));

/* The one Slate tune deliberately outside the piezo's good band. Being hard to
 * ignore is the point, and it costs loudness to get it. FLAT on purpose: this
 * is the one sound that is allowed to click at both ends. */
TUNE(slate_error,     T(660,  90, 40, VOL, FLAT),
                      T(440, 180,  0, VOL, FLAT));

/* Arrival and departure. The mixer stacked a third over these; three notes
 * rising through the same interval say the same thing in sequence. */
TUNE(slate_toggle_on,  T(2093, 20, 5, VOL, PLUCK),
                       T(2637, 18, 0, VOL, PLUCK),
                       T(3136, 17, 0, VOL, BELL));
TUNE(slate_toggle_off, T(2637, 20, 5, VOL, PLUCK),
                       T(2093, 18, 0, VOL, PLUCK),
                       T(1568, 17, 0, VOL, BELL));

/* A major third, the upper note arriving a moment later: "done". */
TUNE(slate_accept,    T(2093, 20, 0, VOL, PLUCK),
                      T(2637, 30, 0, VOL, BELL));
TUNE(slate_cancel,    T(2637, 20, 0, VOL, PLUCK),
                      T(2093, 30, 0, VOL, BELL));

TUNE(slate_screen,     T(2093, 25, 5, VOL, PLUCK),
                       T(2637, 25, 5, VOL, PLUCK),
                       T(3136, 45, 0, VOL, BELL));
TUNE(slate_screen_out, T(3136, 25, 5, VOL, PLUCK),
                       T(2637, 25, 5, VOL, PLUCK),
                       T(2093, 45, 0, VOL, BELL));

/* The one Slate sound long enough to settle rather than stop. */
TUNE(slate_boot,      T(2093,  45, 10, VOL, PLUCK),
                      T(2637,  45, 10, VOL, PLUCK),
                      T(3136, 120,  0, VOL, BELL));

#define X(name, sym) SEQ(slate_##sym##_n),
const struct ui_tune_set_s ui_tune_default = {{UI_SOUND_LIST(X)}};
#undef X

/* ------------------------------------------------------------------ LCARS
 *
 * Struck and gone. This is the family that loses the most -- the blips in the
 * show are stacked fourths and fifths, and an arpeggio is not a chord -- so it
 * is also the one that takes the most back.
 *
 * STAB is the envelope of the thing: an edge, a drop, a short hold. On a
 * ten-millisecond blip that is three milliseconds of fall onto a plateau and
 * two of release, which is a machine acknowledging an instruction rather than
 * a bell being hit.
 *
 * The chirps were always one voice doing the work, and they get what the mixer
 * could not give them. GLIDE walks the period instead of the frequency: a
 * linear sweep in hertz crosses its first octave in a third of the note and
 * then crawls, which is why the old ones sounded top-heavy. CHIRP is that plus
 * a hard tremolo, which is most of what a "working" sound is.
 *
 * And the two alert cadences are repeats now, with the effect rows carrying
 * what the second voice used to: a wobble under the warning where a tritone
 * was, and a hard pulse under the whoops. */

/* The panel blip: a fifth, rolled rather than stacked. */
TUNE(lcars_press,     T(1976, 9, 0, LOW, STAB),
                      T(2960, 9, 0, LOW, STAB));

/* Keypads in the show are dry single blips, so these stay struck and dry. */
TUNE(lcars_tick,      T(2400, 18, 0, LOW, PLUCK));
TUNE(lcars_tick_back, T(2000, 18, 0, LOW, PLUCK));

TUNE(lcars_toggle_on,  T(1976, 10, 0, VOL, STAB),
                       T(2960, 10, 8, VOL, STAB),
                       T(2349, 14, 0, VOL, STAB),
                       T(3520, 14, 0, VOL, STAB));
TUNE(lcars_toggle_off, T(2349, 10, 0, VOL, STAB),
                       T(3520, 10, 8, VOL, STAB),
                       T(1976, 14, 0, VOL, STAB),
                       T(2960, 14, 0, VOL, STAB));

TUNE(lcars_change,    T(1800, 10, 0, VOL, STAB),
                      T(2700, 10, 8, VOL, STAB),
                      T(2600, 10, 0, VOL, STAB),
                      T(3900, 10, 0, VOL, STAB));

/* The computer acknowledging: rising, in fourths, the last one held. */
TUNE(lcars_accept,    T(1976, 15, 0, VOL, STAB),
                      T(2637, 15, 10, VOL, STAB),
                      T(2637, 25, 0, VOL, STAB),
                      T(3520, 25, 0, VOL, BELL));
TUNE(lcars_cancel,    T(2637, 15, 0, VOL, STAB),
                      T(3520, 15, 10, VOL, STAB),
                      T(1976, 25, 0, VOL, STAB),
                      T(2637, 25, 0, VOL, BELL));

/* The chirp: one note swept most of the band in ninety milliseconds, which is
 * the "working" sound. The mixer doubled it a fourth up and swept it linearly;
 * one note glided and pulsed is closer to the show than two were. */
TUNE(lcars_link,      N(1200, 2800, 90, 0, VOL, STAB, CHIRP));
TUNE(lcars_link_back, N(2800, 1200, 90, 0, VOL, STAB, CHIRP));

TUNE(lcars_screen,     T(1400, 25, 8, VOL, STAB),
                       T(1900, 25, 8, VOL, STAB),
                       N(2500, 2900, 55, 0, VOL, STAB, GLIDE));
TUNE(lcars_screen_out, T(2500, 25, 8, VOL, STAB),
                       T(1900, 25, 8, VOL, STAB),
                       N(1400, 1100, 55, 0, VOL, STAB, GLIDE));

TUNE(lcars_notify,    N(1760, 2093, 60, 0, VOL, STAB, GLIDE));

/* Held and repeated, with a wobble where the mixer stacked a tritone. Neither
 * is a pleasant interval, which is the point of both. */
TUNE(lcars_warning,   NR(1400, 1400, 90, 50, VOL, FLAT, WOBBLE, 2));

/* The red alert cadence, near enough: two low whoops, evenly spaced, with a
 * hard tremolo putting the grain on them. Everything in it is below the band
 * and that is deliberate -- see may_go_low() in the tests. */
TUNE(lcars_error,     NR(520, 380, 200, 60, VOL, FLAT, PULSE, 2));

/* Four blips up onto a held note: the computer coming online, and the one tune
 * long enough to be a statement rather than an acknowledgement. */
TUNE(lcars_boot,      T(1400,  45, 10, VOL, STAB),
                      T(1760,  45, 10, VOL, STAB),
                      T(2093,  45, 10, VOL, STAB),
                      T(2637, 140,  0, VOL, BELL));

TUNE(lcars_wake,      T(1976, 17, 0, MED, STAB),
                      T(2960, 18, 0, MED, STAB));

#define X(name, sym) SEQ(lcars_##sym##_n),
const struct ui_tune_set_s ui_tune_lcars = {{UI_SOUND_LIST(X)}};
#undef X

/* ---------------------------------------------------------------- Reticle
 *
 * Swells rather than blips: everything is a PAD, everything sweeps, and nothing
 * has a hard edge. Affirmative rises, dismissal falls, and the triads the mixer
 * stacked arrive one note at a time. */

TUNE(hud_press,     N(2900, 3100, 20, 0, LOW, PAD, NONE));
TUNE(hud_tick,      N(3000, 3100, 18, 0, LOW, PAD, NONE));
TUNE(hud_tick_back, N(2600, 2500, 18, 0, LOW, PAD, NONE));

TUNE(hud_toggle_on,  N(2200, 2400, 90, 0, VOL, PAD, NONE));
TUNE(hud_toggle_off, N(2400, 2200, 90, 0, VOL, PAD, NONE));

TUNE(hud_change,    N(2200, 2800, 110, 0, VOL, PAD, NONE));

/* A rising figure that arrives on a C major triad, rolled. */
TUNE(hud_accept,    T(1568, 60, 0, VOL, PAD),
                    T(2093, 50, 0, VOL, PAD),
                    T(2637, 45, 0, VOL, PAD),
                    T(3136, 45, 0, VOL, PAD));
TUNE(hud_cancel,    T(2637,  30, 0, VOL, PAD),
                    T(2093,  30, 0, VOL, PAD),
                    T(1568, 140, 0, VOL, PAD));

TUNE(hud_link,      N(1600, 2500, 170, 0, VOL, PAD, NONE));
TUNE(hud_link_back, N(2500, 1600, 170, 0, VOL, PAD, NONE));

/* The holographic panel materialising: a G major triad, one note at a time --
 * which is what the mixer's staggered entries were imitating anyway. */
TUNE(hud_screen,     T(1568, 80, 0, VOL, PAD),
                     T(1976, 60, 0, VOL, PAD),
                     T(2349, 60, 0, VOL, PAD));
TUNE(hud_screen_out, T(2349, 60, 0, VOL, PAD),
                     T(1976, 60, 0, VOL, PAD),
                     T(1568, 80, 0, VOL, PAD));

TUNE(hud_notify,    N(1760, 2093, 120, 0, VOL, PAD, NONE));

/* Pulsing. Tension rather than a klaxon: this family does not shout, it
 * worries. */
TUNE(hud_warning,   T(1568, 120, 60, VOL, PAD),
                    T(1568, 120,  0, VOL, PAD));

/* Falling away, and staying inside the band: this family's alarm is unease
 * rather than a whoop, so it does not need the exemption the others take. */
TUNE(hud_error,     N(1400, 1050, 320, 0, VOL, PAD, NONE));

/* Coming online: a long swell that resolves into a triad. */
TUNE(hud_boot,      N(1100, 1568, 300, 0, VOL, PAD, NONE),
                    T(1568, 100, 0, VOL, PAD),
                    T(1976, 100, 0, VOL, PAD),
                    T(2349, 100, 0, VOL, PAD));

TUNE(hud_wake,      N(2200, 2400, 80, 0, MED, PAD, NONE));

#define X(name, sym) SEQ(hud_##sym##_n),
const struct ui_tune_set_s ui_tune_jarvis = {{UI_SOUND_LIST(X)}};
#undef X

/* ------------------------------------------------------------ enumeration
 *
 * For the host tests, which walk every family against every entry. The names
 * that go with it are in ui_beep.hpp, because both engines' table files would
 * otherwise define them identically and collide in the one test binary that
 * links both. Nothing on the device reads this: ui_style.cpp's theme table
 * points straight at the three objects. */

const struct ui_tune_set_s *const ui_tune_sets[UI_THEME_FAMILY_COUNT] = {
    &ui_tune_default,
    &ui_tune_lcars,
    &ui_tune_jarvis};

/* No #else, and no stub. icons/icon_set.cpp has one because it declares
 * functions somebody calls; this file declares only data, and an empty
 * translation unit is legal C++. */
#endif /* CONFIG_OHEZ_BEEPER_ENGINE_SEQ */
