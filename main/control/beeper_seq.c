/**
 * @file beeper_seq.c
 *
 * See beeper_seq.h. Pure arithmetic, no clock and no hardware.
 *
 * The two tables at the top are the sound design's vocabulary, and putting them
 * here rather than in the UI tables is deliberate: "the pad's attack is too
 * slow" should be a one-row edit rather than a forty-note one, and both the
 * RTOS task and the SDL renderer need them anyway.
 */
#include "beeper_seq.h"

#include <stddef.h>

/* attack%, decay%, release%, sustain, attack cap ms, release cap ms.
 *
 * FLAT, PLUCK and PAD are the mixer's three shapes, spelled as rows. Everything
 * below them is what the fourth stage and the caps bought. */
static const struct beeper_seq_env_s env_table[BEEPER_SEQ_ENV_COUNT] = {
    [BEEPER_SEQ_ENV_FLAT]  = {  0,   0,   0, 255,   0,   0},
    [BEEPER_SEQ_ENV_CLICK] = {  0,   0,  12, 255,   0,   3},
    [BEEPER_SEQ_ENV_PLUCK] = {  0, 100,   0,   0,   0,   0},
    [BEEPER_SEQ_ENV_PAD]   = { 50,   0,  50, 255, 250, 250},
    [BEEPER_SEQ_ENV_STAB]  = {  4,  30,  20, 140,   6,  40},
    [BEEPER_SEQ_ENV_BELL]  = {  2,  96,   2,   0,   4,  10},
    [BEEPER_SEQ_ENV_SWELL] = { 60,   0,  40, 255, 400, 200},
    [BEEPER_SEQ_ENV_BLOOM] = { 25,  15,  30, 200,  60, 120},
};

/* vibrato cHz, tremolo cHz, vibrato depth (per mille), tremolo depth (/255),
 * sweep, reserved.
 *
 * Every rate is at or under BEEPER_SEQ_LFO_MAX_CHZ, and a host test says so:
 * above it the wobble aliases against BEEPER_SEQ_STEP_MS and the panel stops
 * sounding like the simulator. */
static const struct beeper_seq_fx_s fx_table[BEEPER_SEQ_FX_COUNT] = {
    [BEEPER_SEQ_FX_NONE]    = {   0,    0,   0,   0, BEEPER_SEQ_SWEEP_LINEAR, 0},
    [BEEPER_SEQ_FX_GLIDE]   = {   0,    0,   0,   0, BEEPER_SEQ_SWEEP_GLIDE,  0},
    [BEEPER_SEQ_FX_SHIMMER] = { 550,    0,  12,   0, BEEPER_SEQ_SWEEP_LINEAR, 0},
    [BEEPER_SEQ_FX_WOBBLE]  = {1200,    0,  45,   0, BEEPER_SEQ_SWEEP_LINEAR, 0},
    [BEEPER_SEQ_FX_SIREN]   = { 200,    0, 120,   0, BEEPER_SEQ_SWEEP_LINEAR, 0},
    [BEEPER_SEQ_FX_BREATHE] = {   0,  500,   0,  90, BEEPER_SEQ_SWEEP_LINEAR, 0},
    [BEEPER_SEQ_FX_PULSE]   = {   0, 1600,   0, 200, BEEPER_SEQ_SWEEP_LINEAR, 0},
    [BEEPER_SEQ_FX_CHIRP]   = {   0, 2000,   0, 120, BEEPER_SEQ_SWEEP_GLIDE,  0},
};

const struct beeper_seq_env_s *beeper_seq_env_preset(uint8_t env)
{
    return &env_table[(env < BEEPER_SEQ_ENV_COUNT) ? env : 0];
}

const struct beeper_seq_fx_s *beeper_seq_fx_preset(uint8_t fx)
{
    return &fx_table[(fx < BEEPER_SEQ_FX_COUNT) ? fx : 0];
}

int8_t beeper_seq_lfo(uint32_t elapsed_ms, uint16_t rate_chz)
{
    /* One period is 100000 / rate_chz milliseconds. Kept as a numerator so
     * there is one division at the end rather than a rounding error that
     * accumulates over a four-hundred-millisecond note.
     *
     * The multiply is bounded by BEEPER_SEQ_LFO_MAX_CHZ against an elapsed that
     * callers here clamp to a note's duration, i.e. a uint16_t: 65535 * 2500 is
     * 164 million, and a host test keeps every preset under the cap. */
    if (rate_chz == 0)
        return 0;

    uint32_t ph = (elapsed_ms * (uint32_t)rate_chz) % 100000u; /* 0..99999 */

    /* A triangle that starts at zero and rises, so a note's vibrato begins at
     * the pitch the table wrote rather than off to one side of it. */
    if (ph < 25000u)
        return (int8_t)((int32_t)ph * 127 / 25000);

    if (ph < 75000u)
        return (int8_t)(((int32_t)50000 - (int32_t)ph) * 127 / 25000);

    return (int8_t)(((int32_t)ph - 100000) * 127 / 25000);
}

uint8_t beeper_seq_envelope(uint8_t env, uint32_t elapsed, uint32_t duration)
{
    const struct beeper_seq_env_s *e = beeper_seq_env_preset(env);
    uint32_t a;
    uint32_t d;
    uint32_t r;

    if (duration == 0)
        return 255;

    /* At or past the end is silence, not whatever it was holding. */
    if (elapsed >= duration)
        return 0;

    a = (duration * e->attack_pct) / 100u;
    if (e->attack_max_ms != 0 && a > e->attack_max_ms)
        a = e->attack_max_ms;

    r = (duration * e->release_pct) / 100u;
    if (e->release_max_ms != 0 && r > e->release_max_ms)
        r = e->release_max_ms;

    d = (duration * e->decay_pct) / 100u;

    /* The note is the budget, and when the stages ask for more than there is,
     * the release is protected and the attack gives way: a note that stops
     * abruptly clicks, and a note that starts abruptly is a pluck. Only a badly
     * written preset row gets here, so this is a guard rather than a policy --
     * but it is a guard that decides how that failure sounds. */
    if (r > duration)
        r = duration;
    if (d > duration - r)
        d = duration - r;
    if (a > duration - r - d)
        a = duration - r - d;

    /* Every division below is guarded by the comparison in front of it: a stage
     * of length zero can never be entered, because `elapsed < 0` is false. That
     * is why there is no divisor test anywhere in here. */
    if (elapsed < a)
        return (uint8_t)((elapsed * 255u) / a);

    if (elapsed < a + d)
        return (uint8_t)(255u - (((255u - e->sustain) * (elapsed - a)) / d));

    if (elapsed < duration - r)
        return e->sustain;

    return (uint8_t)(e->sustain -
                     ((e->sustain * (elapsed - (duration - r))) / r));
}

uint16_t beeper_seq_pitch(const struct beeper_seq_note_s *n, uint32_t elapsed)
{
    const struct beeper_seq_fx_s *fx;
    uint32_t                      carrier;

    if (n == NULL)
        return 0;

    fx      = beeper_seq_fx_preset(n->fx);
    carrier = n->f_start;

    if (elapsed > n->duration_ms)
        elapsed = n->duration_ms;

    if (n->f_end != n->f_start && n->duration_ms != 0)
    {
        if (fx->sweep == BEEPER_SEQ_SWEEP_GLIDE && n->f_start != 0 &&
            n->f_end != 0)
        {
            /* Interpolate the period, in microseconds. At the bottom of the
             * band a period is 2.6 ms and at the top 250 us -- three digits
             * either way, which is finer than LEDC can be set, so a wider fixed
             * point would be spurious precision.
             *
             * The widest term is (p1 - p0) * elapsed: under 2600 times under
             * 65536, which is 170 million. No overflow, and no float. */
            int32_t p0 = 1000000 / (int32_t)n->f_start;
            int32_t p1 = 1000000 / (int32_t)n->f_end;
            int32_t p  = p0 + ((p1 - p0) * (int32_t)elapsed) /
                                  (int32_t)n->duration_ms;

            carrier = (p > 0) ? (uint32_t)(1000000 / p) : n->f_end;
        }
        else
        {
            /* int32_t, and the cast is load-bearing: a falling sweep has a
             * negative span and unsigned arithmetic wraps it into a frequency
             * somewhere around 65 kHz. Same bug and same fix as
             * beeper_mixer.c's beeper_voice_sample(). */
            int32_t span = (int32_t)n->f_end - (int32_t)n->f_start;

            carrier = (uint32_t)((int32_t)n->f_start +
                                 (span * (int32_t)elapsed) /
                                     (int32_t)n->duration_ms);
        }
    }

    if (carrier != 0 && fx->vib_rate_chz != 0 && fx->vib_depth != 0)
    {
        /* Depth first, then the LFO. The other order is carrier * depth * lfo
         * -- 4000 * 255 * 127, which fits an int32_t today and would not if the
         * band ever moved, and there is no reason to leave that standing for
         * one saved instruction. */
        int32_t swing = ((int32_t)carrier * (int32_t)fx->vib_depth) / 1000;
        int32_t lfo   = beeper_seq_lfo(elapsed, fx->vib_rate_chz);
        int32_t wob   = (int32_t)carrier + (swing * lfo) / 127;

        /* A sounding note stays sounding: port_beeper_tone() reads 0 as
         * silence, and a vibrato is an ornament rather than a gate. */
        carrier = (wob < 1) ? 1u : (uint32_t)wob;
    }

    return (carrier > 65535u) ? 65535u : (uint16_t)carrier;
}

uint8_t beeper_seq_amplitude(const struct beeper_seq_note_s *n, uint32_t elapsed)
{
    const struct beeper_seq_fx_s *fx;
    uint32_t                      a;

    if (n == NULL)
        return 0;

    fx = beeper_seq_fx_preset(n->fx);

    /* volume is 0..100 and the envelope 0..255; the product, back in 0..255, is
     * this note's amplitude before the master. */
    a = ((uint32_t)n->volume *
         beeper_seq_envelope(n->env, elapsed, n->duration_ms)) / 100u;

    if (fx->trem_rate_chz != 0 && fx->trem_depth != 0)
    {
        /* Downward only -- see the header. (127 - lfo) is 0..254, so the dip is
         * 0..trem_depth and the note's peak is never exceeded. */
        int32_t  lfo = beeper_seq_lfo(elapsed, fx->trem_rate_chz);
        uint32_t dip = ((uint32_t)fx->trem_depth *
                        (uint32_t)(127 - lfo)) / 254u;

        a = (a * (255u - dip)) / 255u;
    }

    return (uint8_t)((a > 255u) ? 255u : a);
}

uint16_t beeper_seq_frame(const struct beeper_seq_s *seq, uint32_t t_ms,
                          uint8_t master, struct beeper_slot_s *slot)
{
    uint32_t t = t_ms;

    slot->freq  = 0;
    slot->level = 0;

    if (seq == NULL || seq->notes == NULL || seq->count == 0)
        return 0;

    for (uint8_t i = 0; i < seq->count; i++)
    {
        const struct beeper_seq_note_s *n = &seq->notes[i];
        uint32_t once  = (uint32_t)n->duration_ms + (uint32_t)n->pause_ms;
        uint32_t times = (n->repeat == 0) ? 1u : (uint32_t)n->repeat;
        uint32_t span  = once * times;

        /* A note of zero length and zero pause has span 0, so it is stepped
         * over here and never reaches the modulo below. That is the whole of
         * the divide-by-zero guard, and it is why this comparison is `>=`. */
        if (t >= span)
        {
            t -= span;
            continue;
        }

        /* Which pass, and how far into it. The envelope and both LFOs measure
         * from here, so a repeated note is a repeated note. */
        t %= once;

        if (t >= n->duration_ms)
            return BEEPER_SEQ_STEP_MS; /* inside the pause: a rest, not the end */

        slot->freq  = beeper_seq_pitch(n, t);
        slot->level = beeper_level_permille(beeper_seq_amplitude(n, t), master);

        return BEEPER_SEQ_STEP_MS;
    }

    return 0;
}

uint32_t beeper_seq_duration_ms(const struct beeper_seq_s *seq)
{
    uint32_t total = 0;

    if (seq == NULL || seq->notes == NULL)
        return 0;

    for (uint8_t i = 0; i < seq->count; i++)
    {
        const struct beeper_seq_note_s *n = &seq->notes[i];
        uint32_t once  = (uint32_t)n->duration_ms + (uint32_t)n->pause_ms;
        uint32_t times = (n->repeat == 0) ? 1u : (uint32_t)n->repeat;

        total += once * times;
    }

    return total;
}

void beeper_seq_freq_range(const struct beeper_seq_s *seq, uint16_t *lo,
                           uint16_t *hi)
{
    bool     seen    = false;
    uint32_t lowest  = 0;
    uint32_t highest = 0;

    if (seq != NULL && seq->notes != NULL)
    {
        for (uint8_t i = 0; i < seq->count; i++)
        {
            const struct beeper_seq_note_s *n  = &seq->notes[i];
            const struct beeper_seq_fx_s   *fx = beeper_seq_fx_preset(n->fx);
            uint16_t ends[2] = {n->f_start, n->f_end};

            for (uint8_t k = 0; k < 2; k++)
            {
                uint32_t f     = ends[k];
                uint32_t swing = 0;

                /* The excursion counts. A note the table wrote inside the band
                 * can still be swung out of it by its effect row, and the
                 * failure would otherwise only show up on a panel. */
                if (f != 0 && fx->vib_rate_chz != 0 && fx->vib_depth != 0)
                    swing = (f * (uint32_t)fx->vib_depth) / 1000u;

                uint32_t down = (f > swing) ? (f - swing) : ((f == 0) ? 0u : 1u);
                uint32_t up   = f + swing;

                if (seen == false || down < lowest)
                    lowest = down;

                if (seen == false || up > highest)
                    highest = up;

                seen = true;
            }
        }
    }

    *lo = (uint16_t)((lowest > 65535u) ? 65535u : lowest);
    *hi = (uint16_t)((highest > 65535u) ? 65535u : highest);
}

uint32_t beeper_seq_note_count(const struct beeper_seq_s *seq)
{
    uint32_t total = 0;

    if (seq == NULL || seq->notes == NULL)
        return 0;

    for (uint8_t i = 0; i < seq->count; i++)
        total += (seq->notes[i].repeat == 0) ? 1u : (uint32_t)seq->notes[i].repeat;

    return total;
}
