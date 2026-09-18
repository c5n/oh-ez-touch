/**
 * @file ui_beep_tables_seq.cpp
 *
 * Three families, one voice -- and everything that would have gone on a second
 * one spent on the note instead.
 *
 * The expressive engine's tables -- see CONFIG_OHEZ_BEEPER_ENGINE. The whole
 * file is guarded, because the polyphonic engine has its own fifty-four in
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
 * already used for Material's link sound.
 *
 * ---------------------------------------------------------------- the voices
 *
 * Material is restrained, consonant, and short. It must not draw attention,
 * so it gains the least from the new engine and is written to gain the least.
 *
 * LCARS is struck and gone: nothing eases, everything is dry, and the figures
 * are stepped rather than swept -- see the note above that family for what
 * changed and why.
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

/* --------------------------------------------------------------- Material
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
 * Nothing here sweeps, nothing wobbles. Material has no use for either. */

TUNE(material_press,     T(2093, 12, 0, LOW, PLUCK));
TUNE(material_tick,      T(2349, 10, 0, LOW, PLUCK));
TUNE(material_tick_back, T(1976, 10, 0, LOW, PLUCK));
TUNE(material_change,    T(2093, 25, 0, VOL, PLUCK));
TUNE(material_wake,      T(2093, 40, 0, MED, BELL));

/* Two notes a fourth apart, in sequence: going somewhere is a move, and a move
 * is two things one after the other. The second one lands rather than strikes. */
TUNE(material_link,      T(2093, 30, 10, VOL, PLUCK),
                         T(2794, 40,  0, VOL, BELL));
TUNE(material_link_back, T(2794, 30, 10, VOL, PLUCK),
                         T(2093, 40,  0, VOL, BELL));

TUNE(material_notify,    T(2093, 30, 8, VOL, PLUCK),
                         T(2637, 45, 0, VOL, BELL));

/* Two strikes, said once. The mixer needed two notes written out for this;
 * a repeat is what a cadence is, and the envelope restarts on the second. */
TUNE(material_warning,   NR(1568, 1568, 80, 60, VOL, CLICK, NONE, 2));

/* The one Material tune deliberately outside the piezo's good band. Being hard
 * to ignore is the point, and it costs loudness to get it. FLAT on purpose: this
 * is the one sound that is allowed to click at both ends. */
TUNE(material_error,     T(660,  90, 40, VOL, FLAT),
                         T(440, 180,  0, VOL, FLAT));

/* Arrival and departure. The mixer stacked a third over these; three notes
 * rising through the same interval say the same thing in sequence. */
TUNE(material_toggle_on,  T(2093, 20, 5, VOL, PLUCK),
                          T(2637, 18, 0, VOL, PLUCK),
                          T(3136, 17, 0, VOL, BELL));
TUNE(material_toggle_off, T(2637, 20, 5, VOL, PLUCK),
                          T(2093, 18, 0, VOL, PLUCK),
                          T(1568, 17, 0, VOL, BELL));

/* A major third, the upper note arriving a moment later: "done". */
TUNE(material_accept,    T(2093, 20, 0, VOL, PLUCK),
                         T(2637, 30, 0, VOL, BELL));
TUNE(material_cancel,    T(2637, 20, 0, VOL, PLUCK),
                         T(2093, 30, 0, VOL, BELL));

TUNE(material_screen,     T(2093, 25, 5, VOL, PLUCK),
                          T(2637, 25, 5, VOL, PLUCK),
                          T(3136, 45, 0, VOL, BELL));
TUNE(material_screen_out, T(3136, 25, 5, VOL, PLUCK),
                          T(2637, 25, 5, VOL, PLUCK),
                          T(2093, 45, 0, VOL, BELL));

/* The one Material sound long enough to settle rather than stop. */
TUNE(material_boot,      T(2093,  45, 10, VOL, PLUCK),
                         T(2637,  45, 10, VOL, PLUCK),
                         T(3136, 120,  0, VOL, BELL));

/* Somebody is at the door -- MQTT only, see ui_beep.hpp. A doorbell in this
 * family is a doorbell: two struck tones a fourth apart, high then low, left
 * to ring. The only thing that makes it Material rather than generic is that it
 * does not do anything else. */
TUNE(material_door_chime, T(2637,  90, 25, VOL, BELL),
                          T(2093, 170,  0, VOL, BELL));

#define X(name, sym) SEQ(material_##sym##_n),
const struct ui_tune_set_s ui_tune_material = {{UI_SOUND_LIST(X)}};
#undef X

/* ------------------------------------------------------------------ LCARS
 *
 * Reworked to sit as close to a TNG console as one pin can get, and the first
 * thing that meant was taking the sweeps out.
 *
 * Read the caveat in ui_beep.hpp first: this is a square wave from a piezo and
 * the sounds it is imitating are sampled, layered and reverberant. What can be
 * carried across is *structure* -- contour, rhythm, register and interval --
 * and structure is most of what makes a sound recognisable. What cannot is
 * timbre. Nobody should read this table expecting a recording, and every
 * description below is a characterisation of what those sounds do rather than
 * a measurement of one.
 *
 * Four properties are doing the work:
 *
 *   STEPPED, NOT SWEPT. This is the big one and it is what most of this
 *   rework is. A console blip in the show is two to four discrete tones, each
 *   fifteen to forty milliseconds, butted together with a few milliseconds
 *   between -- not a glide. The previous table spent its two most-used sounds
 *   (link, link_back) on a note swept most of the band in ninety
 *   milliseconds, which is a science-fiction scanning noise and is not what a
 *   panel does when somebody touches it. Two sweeps survive, and both earn it:
 *   the hail rises because a hail rises, and the klaxon whoops because a
 *   klaxon whoops.
 *
 *   FALLING AS OFTEN AS RISING. The single most identifiable console sound is
 *   a two-tone that drops -- "bee-doo". The old table rose almost everywhere,
 *   because rising reads as affirmative and the vocabulary is mostly
 *   affirmative. Here the drop is given to the two gestures that are not
 *   going anywhere: switching something off, and waking the panel.
 *
 *   SHORT. A console acknowledges in under a tenth of a second. Everything
 *   here except the three alerts and the door is inside 150 ms, which is well
 *   under what the policy allows -- the policy is a ceiling, and this family
 *   sits a long way below it on purpose.
 *
 *   ONE REGISTER. The show's panel sounds are bright and narrow, so these live
 *   between about 1.5 and 3.5 kHz and lean on rhythm and contour to be told
 *   apart rather than on range. The alerts break out of it downwards, which is
 *   exactly what the alerts do.
 *
 * STAB is still the envelope of the thing: an edge, a drop, a short hold. On a
 * fifteen-millisecond blip that is under a millisecond of attack onto a
 * plateau, which is a machine acknowledging an instruction rather than a bell
 * being hit. BELL appears once, on the door, because a door is not a console.
 *
 * The effect rows that survive are the ones with somewhere real to be: GLIDE
 * on the hail, CHIRP on the klaxon, WOBBLE on the standing alert, PULSE on the
 * held note the boot sequence lands on. */

/* The contact tap, layer one -- see the policy in ui_beep.hpp. One dry blip and
 * nothing else.
 *
 * It was a rolled fifth, which is a small figure, and a small figure is a
 * statement: layer one is not allowed to say anything except "the glass felt
 * you". Every two-tone in this family now belongs to an outcome, so that a
 * gesture reads as "tap ... answer" rather than as two answers. */
TUNE(lcars_press,     T(2349, 12, 0, LOW, STAB));

/* Keypads are dry single blips, and stay that way. Direction is the whole of
 * the difference between them, which is all a key needs to carry. */
TUNE(lcars_tick,      T(2794, 16, 0, LOW, STAB));
TUNE(lcars_tick_back, T(2093, 16, 0, LOW, STAB));

/* A control actuated. Two tones a fourth apart, and the falling one is the
 * console sound everybody can hum -- so it goes on the gesture that is *not*
 * affirmative, which is the one that reaches for it. */
TUNE(lcars_toggle_on,  T(1976, 22, 6, VOL, STAB),
                       T(2637, 30, 0, VOL, STAB));
TUNE(lcars_toggle_off, T(2637, 22, 6, VOL, STAB),
                       T(1976, 30, 0, VOL, STAB));

/* Input registered: three narrow steps, tight, over before the finger is. Steps
 * of a tone rather than of a fourth, which is what keeps it from being heard as
 * the toggle. */
TUNE(lcars_change,    T(2093, 16, 5, VOL, STAB),
                      T(2349, 16, 5, VOL, STAB),
                      T(2794, 22, 0, VOL, STAB));

/* The computer acknowledging an instruction: two blips and a held third. The
 * hold is what separates an acknowledgement from a keystroke -- and it is a
 * hold rather than a ring, because STAB's release is capped at forty
 * milliseconds however long the note is. */
TUNE(lcars_accept,    T(2093, 20, 6, VOL, STAB),
                      T(2637, 20, 6, VOL, STAB),
                      T(3136, 70, 0, VOL, STAB));
TUNE(lcars_cancel,    T(2637, 20, 6, VOL, STAB),
                      T(2093, 20, 6, VOL, STAB),
                      T(1568, 70, 0, VOL, STAB));

/* Moving about inside a panel: the stutter. Two strikes on one pitch and then
 * a step away from it, which is the rhythm the show's panels make when a
 * selection lands, and which nothing else in this family has -- so link is told
 * from toggle by its rhythm rather than by its pitches.
 *
 * `repeat` is what spells the stutter: the envelope restarts on the second
 * pass, so it is two strikes rather than one note with a gap cut in it. */
TUNE(lcars_link,      NR(2093, 2093, 12, 8, VOL, STAB, NONE, 2),
                      T(3136, 26, 0, VOL, STAB));
TUNE(lcars_link_back, NR(3136, 3136, 12, 8, VOL, STAB, NONE, 2),
                      T(2093, 26, 0, VOL, STAB));

/* A surface arriving over the one you were on: two rising pairs, the second
 * starting above the first and landing held. Four blips in under a tenth of a
 * second is the panel-reconfiguring cluster, and the pairing is what makes it
 * a cluster rather than a scale. */
TUNE(lcars_screen,     T(1760, 14,  5, VOL, STAB),
                       T(2637, 14, 12, VOL, STAB),
                       T(2093, 14,  5, VOL, STAB),
                       T(3136, 34,  0, VOL, STAB));
TUNE(lcars_screen_out, T(3136, 14,  5, VOL, STAB),
                       T(2093, 14, 12, VOL, STAB),
                       T(2637, 14,  5, VOL, STAB),
                       T(1760, 34,  0, VOL, STAB));

/* The hail. An unsolicited banner is the panel getting somebody's attention
 * from across a room, which is the one thing in this vocabulary the incoming
 * hail chime is for -- so this is the one place a rise is worth a sweep rather
 * than steps, and it lands on a held tone above it.
 *
 * GLIDE rather than a linear sweep: walking the period is geometric, so the
 * octave and a fifth here is heard as an even rise instead of as a rush to the
 * top and a crawl. */
TUNE(lcars_notify,    N(2093, 3136, 50, 10, VOL, STAB, GLIDE),
                      T(3520, 60, 0, VOL, STAB));

/* The standing alert: an alternating two-tone cadence, the lower of the pair
 * warbling. Not a klaxon -- a klaxon is what the next one is -- but a thing
 * that will not stop until somebody deals with it, which is what an alert
 * condition short of the red one sounds like.
 *
 * CLICK rather than STAB, because the point of these is that they are gated
 * tones rather than struck ones. The WOBBLE is only on the lower note: on both
 * it is a siren, and on one it is unease. */
TUNE(lcars_warning,   T(1568, 95, 45, VOL, CLICK),
                      N(1319, 1319, 95, 45, VOL, CLICK, WOBBLE),
                      T(1568, 95, 45, VOL, CLICK),
                      N(1319, 1319, 110, 0, VOL, CLICK, WOBBLE));

/* The klaxon. A whoop that falls an octave, twice, with a hard grain over it --
 * and the whole of it below the piezo's good band, which is the exemption
 * may_go_low() in the tests exists for and the only sound in this family that
 * takes it. Being impossible to ignore is worth more here than being loud.
 *
 * CHIRP is a glide plus a hard tremolo, and both halves are the point: the
 * glide is what makes it a whoop rather than a slide, and the tremolo at 20 Hz
 * is the grain that keeps a square wave from sounding like a test tone. */
TUNE(lcars_error,     NR(660, 330, 230, 90, VOL, FLAT, CHIRP, 2));

/* Coming online. Three steps up and a held note, and the held note pulses --
 * the one sustained tone in this family, and a sustained tone that is doing
 * something is the sound of a machine still working rather than one that has
 * finished. The only tune here long enough to be a statement. */
TUNE(lcars_boot,      T(1568, 40, 10, VOL, STAB),
                      T(1976, 40, 10, VOL, STAB),
                      T(2349, 40, 10, VOL, STAB),
                      N(2794, 2794, 150, 0, VOL, STAB, PULSE));

/* The tap that woke the display, and the whole of the feedback for it -- the
 * pointer is suppressed for 200 ms afterwards, so nothing else sounds. The
 * falling two-tone, at the level that is audible in a dark room without being
 * an announcement. */
TUNE(lcars_wake,      T(2794, 14, 0, MED, STAB),
                      T(2093, 20, 0, MED, STAB));

/* Somebody is at the door -- MQTT only, see ui_beep.hpp.
 *
 * The one sound in this family that is not a console, and it does not pretend
 * to be one: two tones a fourth apart, high then low, struck and left to ring.
 * BELL rather than STAB is the whole difference -- an instant attack and a long
 * decay, where every other sound here is an attack onto a plateau and a cut.
 * A door announces a person; a console answers a finger, and they should not
 * sound alike across a room. */
TUNE(lcars_door_chime, T(2637, 160, 30, VOL, BELL),
                       T(1976, 260,  0, VOL, BELL));

#define X(name, sym) SEQ(lcars_##sym##_n),
const struct ui_tune_set_s ui_tune_lcars = {{UI_SOUND_LIST(X)}};
#undef X

/* ---------------------------------------------------------------- Reticle
 *
 * Swells rather than blips: everything eases, everything sweeps, affirmative
 * rises and dismissal falls. It is the family with the most to gain here,
 * because a slow synthetic voice is exactly what an ornament is for -- and a
 * vibrato is the one thing the mixer explicitly could not do, its own header
 * saying that two voices a few hertz apart will not beat because phase is not
 * carried across a slot.
 *
 * So SHIMMER is this family's default on anything sustained, SWELL and BLOOM
 * replace the one PAD shape it had, and the two alerts stop borrowing the other
 * families' vocabulary: the warning is a deep slow siren rather than a stacked
 * second, and the error breathes rather than whooping. */

TUNE(hud_press,     N(2900, 3100, 20, 0, LOW, PAD, NONE));
TUNE(hud_tick,      N(3000, 3100, 18, 0, LOW, PAD, NONE));
TUNE(hud_tick_back, N(2600, 2500, 18, 0, LOW, PAD, NONE));

TUNE(hud_toggle_on,  N(2200, 2400, 90, 0, VOL, SWELL, SHIMMER));
TUNE(hud_toggle_off, N(2400, 2200, 90, 0, VOL, SWELL, SHIMMER));

TUNE(hud_change,    N(2200, 2800, 110, 0, VOL, SWELL, NONE));

/* A rising figure that arrives on a C major triad, rolled -- each note blooming
 * in rather than starting, which is what the mixer's staggered entries were
 * imitating with three voices. */
TUNE(hud_accept,    T(1568, 60, 0, VOL, BLOOM),
                    T(2093, 50, 0, VOL, BLOOM),
                    T(2637, 45, 0, VOL, BLOOM),
                    T(3136, 45, 0, VOL, BLOOM));
TUNE(hud_cancel,    T(2637,  30, 0, VOL, BLOOM),
                    T(2093,  30, 0, VOL, BLOOM),
                    T(1568, 140, 0, VOL, SWELL));

/* The long slides, with the wobble on them that this family was always
 * reaching for. */
TUNE(hud_link,      N(1600, 2500, 170, 0, VOL, SWELL, SHIMMER));
TUNE(hud_link_back, N(2500, 1600, 170, 0, VOL, SWELL, SHIMMER));

/* The holographic panel materialising: a G major triad, one note at a time. */
/* No vibrato on these: a sixty-millisecond note is shorter than one period of
 * the slowest LFO here, and an ornament that does not complete is a bend. The
 * bloom is what carries them. */
TUNE(hud_screen,     T(1568, 80, 0, VOL, BLOOM),
                     T(1976, 60, 0, VOL, BLOOM),
                     T(2349, 60, 0, VOL, BLOOM));
TUNE(hud_screen_out, T(2349, 60, 0, VOL, BLOOM),
                     T(1976, 60, 0, VOL, BLOOM),
                     T(1568, 80, 0, VOL, SWELL));

TUNE(hud_notify,    N(1760, 2093, 120, 0, VOL, SWELL, SHIMMER));

/* Slow and deep. Tension rather than a klaxon: this family does not shout, it
 * worries -- and a siren on one voice worries better than a minor second on
 * two did. */
TUNE(hud_warning,   NR(1568, 1568, 250, 60, VOL, SWELL, SIREN, 2));

/* Falling away and breathing, and staying inside the band: this family's alarm
 * is unease rather than a whoop, so it does not take the exemption the other
 * two do. */
TUNE(hud_error,     N(1400, 1050, 320, 0, VOL, SWELL, BREATHE));

/* Coming online: a long swell that resolves into a triad. */
TUNE(hud_boot,      N(1100, 1568, 300, 0, VOL, SWELL, NONE),
                    N(1568, 1568, 100, 0, VOL, BLOOM, SHIMMER),
                    N(1976, 1976, 100, 0, VOL, BLOOM, SHIMMER),
                    N(2349, 2349, 100, 0, VOL, BLOOM, SHIMMER));

TUNE(hud_wake,      N(2200, 2400, 80, 0, MED, SWELL, NONE));

/* Somebody is at the door -- MQTT only, see ui_beep.hpp. This family does not
 * strike things, so its doorbell blooms in and falls away rather than ringing:
 * a soft arrival, and a slower one under it. Both long enough to carry the
 * shimmer, which is the floor the LFO test enforces. */
TUNE(hud_door_chime, N(2093, 2093, 140, 20, VOL, BLOOM, SHIMMER),
                     N(1568, 1568, 240,  0, VOL, SWELL, SHIMMER));

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
    &ui_tune_material,
    &ui_tune_lcars,
    &ui_tune_jarvis};

/* No #else, and no stub. icons/icon_set.cpp has one because it declares
 * functions somebody calls; this file declares only data, and an empty
 * translation unit is legal C++. */
#endif /* CONFIG_OHEZ_BEEPER_ENGINE_SEQ */
