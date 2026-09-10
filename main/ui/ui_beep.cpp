/**
 * @file ui_beep.cpp
 *
 * Three families, three voices.
 *
 * The frequencies sit between about 1.2 and 3 kHz because that is where a
 * small piezo is loudest -- a note an octave lower is not quieter on paper and
 * is much quieter in a room. The error sounds deliberately break that rule:
 * being hard to ignore matters more than being loud.
 */
#include "ui_beep.hpp"

#include "ui_style.hpp"

/* f_start, f_end, duration, pause, volume, shape */
#define N(fs, fe, d, p, v, sh)                                                 \
    {                                                                          \
        (uint16_t)(fs), (uint16_t)(fe), (uint16_t)(d), (uint16_t)(p),          \
            (uint8_t)(v), (uint8_t)(sh)                                        \
    }

#define CHIME(a) {(a), (uint8_t)(sizeof(a) / sizeof((a)[0]))}

#define VOL 50

/* ------------------------------------------------------------------ Slate
 *
 * Restrained. Close to the arpeggios the panel always used, because they were
 * fine -- what they lacked was an envelope, so every one of them began and
 * ended with a click. */
static const struct beeper_note_s slate_touch[]  = {N(2093, 2093, 15, 0, VOL, BEEPER_SHAPE_PLUCK)};
static const struct beeper_note_s slate_change[] = {N(2093, 2093, 25, 0, VOL, BEEPER_SHAPE_PLUCK)};
static const struct beeper_note_s slate_link[]   = {N(2093, 2093, 30, 10, VOL, BEEPER_SHAPE_PLUCK),
                                                    N(2637, 2637, 40, 0, VOL, BEEPER_SHAPE_PLUCK)};
static const struct beeper_note_s slate_back[]   = {N(2637, 2637, 30, 10, VOL, BEEPER_SHAPE_PLUCK),
                                                    N(2093, 2093, 40, 0, VOL, BEEPER_SHAPE_PLUCK)};
static const struct beeper_note_s slate_open[]   = {N(2093, 2093, 25, 5, VOL, BEEPER_SHAPE_PLUCK),
                                                    N(2637, 2637, 25, 5, VOL, BEEPER_SHAPE_PLUCK),
                                                    N(3136, 3136, 45, 0, VOL, BEEPER_SHAPE_PLUCK)};
static const struct beeper_note_s slate_close[]  = {N(3136, 3136, 25, 5, VOL, BEEPER_SHAPE_PLUCK),
                                                    N(2637, 2637, 25, 5, VOL, BEEPER_SHAPE_PLUCK),
                                                    N(2093, 2093, 45, 0, VOL, BEEPER_SHAPE_PLUCK)};
static const struct beeper_note_s slate_error[]  = {N(660, 660, 90, 40, VOL, BEEPER_SHAPE_FLAT),
                                                    N(440, 440, 180, 0, VOL, BEEPER_SHAPE_FLAT)};

const struct ui_sound_s ui_sound_default = {{
    CHIME(slate_touch), CHIME(slate_change), CHIME(slate_link), CHIME(slate_back),
    CHIME(slate_open), CHIME(slate_close), CHIME(slate_error),
}};

/* ------------------------------------------------------------------ LCARS
 *
 * Rapid blips across wide intervals, and the chirps that are the whole sound
 * of the thing: a single note swept a long way in under a tenth of a second.
 * Nothing here eases -- these are machines acknowledging an instruction. */
static const struct beeper_note_s lcars_touch[]  = {N(2400, 2400, 18, 0, VOL, BEEPER_SHAPE_PLUCK)};
static const struct beeper_note_s lcars_change[] = {N(1800, 1800, 20, 8, VOL, BEEPER_SHAPE_PLUCK),
                                                    N(2600, 2600, 20, 0, VOL, BEEPER_SHAPE_PLUCK)};
static const struct beeper_note_s lcars_link[]   = {N(1200, 2800, 90, 0, VOL, BEEPER_SHAPE_PLUCK)};
static const struct beeper_note_s lcars_back[]   = {N(2800, 1200, 90, 0, VOL, BEEPER_SHAPE_PLUCK)};
static const struct beeper_note_s lcars_open[]   = {N(1400, 1400, 25, 8, VOL, BEEPER_SHAPE_PLUCK),
                                                    N(1900, 1900, 25, 8, VOL, BEEPER_SHAPE_PLUCK),
                                                    N(2500, 2900, 55, 0, VOL, BEEPER_SHAPE_PLUCK)};
static const struct beeper_note_s lcars_close[]  = {N(2500, 2500, 25, 8, VOL, BEEPER_SHAPE_PLUCK),
                                                    N(1900, 1900, 25, 8, VOL, BEEPER_SHAPE_PLUCK),
                                                    N(1400, 1000, 55, 0, VOL, BEEPER_SHAPE_PLUCK)};
/* The red alert cadence, near enough: two low pulses, evenly spaced. */
static const struct beeper_note_s lcars_error[]  = {N(520, 380, 200, 60, VOL, BEEPER_SHAPE_FLAT),
                                                    N(520, 380, 200, 0, VOL, BEEPER_SHAPE_FLAT)};

const struct ui_sound_s ui_sound_lcars = {{
    CHIME(lcars_touch), CHIME(lcars_change), CHIME(lcars_link), CHIME(lcars_back),
    CHIME(lcars_open), CHIME(lcars_close), CHIME(lcars_error),
}};

/* ----------------------------------------------------------------- Reticle
 *
 * Swells rather than blips: everything is a PAD, everything is a sweep, and
 * nothing has a hard edge. Affirmative rises, dismissal falls. */
static const struct beeper_note_s hud_touch[]  = {N(2900, 3100, 30, 0, VOL, BEEPER_SHAPE_PAD)};
static const struct beeper_note_s hud_change[] = {N(2200, 2800, 110, 0, VOL, BEEPER_SHAPE_PAD)};
static const struct beeper_note_s hud_link[]   = {N(1600, 2500, 170, 0, VOL, BEEPER_SHAPE_PAD)};
static const struct beeper_note_s hud_back[]   = {N(2500, 1600, 170, 0, VOL, BEEPER_SHAPE_PAD)};
static const struct beeper_note_s hud_open[]   = {N(1500, 2100, 90, 0, VOL, BEEPER_SHAPE_PAD),
                                                  N(2100, 2700, 120, 0, VOL, BEEPER_SHAPE_PAD)};
static const struct beeper_note_s hud_close[]  = {N(2700, 2100, 90, 0, VOL, BEEPER_SHAPE_PAD),
                                                  N(2100, 1500, 120, 0, VOL, BEEPER_SHAPE_PAD)};
static const struct beeper_note_s hud_error[]  = {N(700, 420, 320, 0, VOL, BEEPER_SHAPE_PAD)};

const struct ui_sound_s ui_sound_jarvis = {{
    CHIME(hud_touch), CHIME(hud_change), CHIME(hud_link), CHIME(hud_back),
    CHIME(hud_open), CHIME(hud_close), CHIME(hud_error),
}};

void ui_beep_play(enum ui_sound_e sound)
{
    if ((unsigned)sound >= UI_SOUND_COUNT)
        return;

    const struct ui_sound_s *set = ui_style_theme()->sound;

    if (set == NULL)
        return;

    beeper_play(&set->chime[sound]);
}
