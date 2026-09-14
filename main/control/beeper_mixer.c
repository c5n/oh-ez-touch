/**
 * @file beeper_mixer.c
 *
 * See beeper_mixer.h. Pure arithmetic, no clock and no hardware.
 */
#include "beeper_mixer.h"

#include <stddef.h>

uint8_t beeper_envelope(uint8_t shape, uint32_t elapsed, uint32_t duration)
{
    if (duration == 0)
        return 255;

    uint32_t t = (elapsed >= duration) ? 255u : ((elapsed * 255u) / duration);

    switch (shape)
    {
    case BEEPER_SHAPE_PLUCK:
        /* Straight up, then away: what a struck thing does. */
        return (uint8_t)(255u - t);

    case BEEPER_SHAPE_PAD:
        /* Up and down again, so neither end is a click. The peak is 254 and
         * not 255 -- an artefact of the halves meeting at 128 -- and it is
         * pinned that way by a test rather than rounded away, because nothing
         * audible turns on one part in 255. */
        return (uint8_t)((t < 128u) ? (t * 2u) : ((255u - t) * 2u));

    case BEEPER_SHAPE_FLAT:
    default:
        return 255;
    }
}

bool beeper_voice_sample(const struct beeper_voice_s *v, uint32_t t_ms,
                         uint16_t *freq_out, uint8_t *level_out)
{
    *freq_out  = 0;
    *level_out = 0;

    if (v == NULL || v->notes == NULL || v->count == 0)
        return false;

    if (t_ms < v->start_ms)
        return true; /* has not come in yet, but is not finished either */

    uint32_t t = t_ms - v->start_ms;

    for (uint8_t i = 0; i < v->count; i++)
    {
        const struct beeper_note_s *n = &v->notes[i];
        uint32_t span = (uint32_t)n->duration_ms + (uint32_t)n->pause_ms;

        if (t >= span)
        {
            t -= span;
            continue;
        }

        if (t >= n->duration_ms)
            return true; /* inside this note's pause: alive, silent */

        uint16_t freq = n->f_start;

        if (n->f_end != n->f_start && n->duration_ms != 0)
        {
            /* int32_t, and that cast is load-bearing: a falling sweep has a
             * negative span, and unsigned arithmetic would wrap it into a
             * frequency somewhere around 65 kHz. */
            int32_t sweep = (int32_t)n->f_end - (int32_t)n->f_start;

            freq = (uint16_t)((int32_t)n->f_start +
                              (sweep * (int32_t)t) / (int32_t)n->duration_ms);
        }

        *freq_out = freq;
        /* volume is 0..100 and the envelope 0..255; the product, back in
         * 0..255, is this line's amplitude before the master. */
        *level_out = (uint8_t)(((uint32_t)n->volume *
                                beeper_envelope(n->shape, t, n->duration_ms)) / 100u);

        return true;
    }

    return false;
}

uint16_t beeper_level_permille(uint8_t level, uint8_t master)
{
    if (master > 100)
        master = 100;

    return (uint16_t)(((uint32_t)level * (uint32_t)master * 10u) / 255u);
}

uint16_t beeper_slot_gain(uint16_t level, uint8_t voices)
{
    /* 256 * sqrt(n), in Q8. */
    static const uint16_t gain_q8[BEEPER_VOICES_MAX + 1] = {256, 256, 362, 443};

    if (voices == 0 || voices > BEEPER_VOICES_MAX)
        return level;

    uint32_t scaled = ((uint32_t)level * gain_q8[voices]) >> 8;

    return (scaled > BEEPER_LEVEL_MAX) ? BEEPER_LEVEL_MAX : (uint16_t)scaled;
}

uint16_t beeper_mixer_frame(const struct beeper_chime_s *chime, uint32_t t_ms,
                            uint8_t master, struct beeper_slot_s slots[BEEPER_VOICES_MAX],
                            uint8_t *count)
{
    uint8_t sounding = 0;
    bool    alive    = false;

    *count = 0;

    if (chime == NULL || chime->voices == NULL || chime->count == 0)
        return 0;

    for (uint8_t i = 0; i < chime->count; i++)
    {
        uint16_t freq;
        uint8_t  level;

        if (beeper_voice_sample(&chime->voices[i], t_ms, &freq, &level) == false)
            continue;

        alive = true;

        if (freq == 0 || level == 0)
            continue; /* resting: occupies no slot, so the singers keep the frame */

        if (sounding == BEEPER_VOICES_MAX)
            continue; /* the first three win, in table order */

        slots[sounding].freq  = freq;
        slots[sounding].level = beeper_level_permille(level, master);
        sounding++;
    }

    if (sounding == 0)
        return alive ? BEEPER_STEP_MS : 0;

    for (uint8_t i = 0; i < sounding; i++)
        slots[i].level = beeper_slot_gain(slots[i].level, sounding);

    *count = sounding;

    /* One voice is not multiplexed at all: the same step and the same
     * uninterrupted carrier the monophonic driver had. */
    return (sounding == 1) ? BEEPER_STEP_MS : (uint16_t)(BEEPER_SLOT_MS * sounding);
}

uint32_t beeper_chime_duration_ms(const struct beeper_chime_s *chime)
{
    uint32_t longest = 0;

    if (chime == NULL || chime->voices == NULL)
        return 0;

    for (uint8_t i = 0; i < chime->count; i++)
    {
        const struct beeper_voice_s *v = &chime->voices[i];
        uint32_t end = v->start_ms;

        if (v->notes == NULL)
            continue;

        for (uint8_t j = 0; j < v->count; j++)
            end += (uint32_t)v->notes[j].duration_ms + (uint32_t)v->notes[j].pause_ms;

        if (end > longest)
            longest = end;
    }

    return longest;
}

uint8_t beeper_chime_peak_voices(const struct beeper_chime_s *chime)
{
    uint32_t total = beeper_chime_duration_ms(chime);
    uint8_t  peak  = 0;

    /* Sampled rather than solved. A chime is a few hundred milliseconds and
     * this is called by the tests and by nothing on the device, so a
     * millisecond-by-millisecond walk is the cheapest correct answer -- and it
     * counts what the mixer would actually have sounded, not what the table
     * looks like it says. */
    for (uint32_t t = 0; t < total; t++)
    {
        uint8_t here = 0;

        for (uint8_t i = 0; i < chime->count; i++)
        {
            uint16_t freq;
            uint8_t  level;

            if (beeper_voice_sample(&chime->voices[i], t, &freq, &level) == false)
                continue;

            if (freq != 0 && level != 0)
                here++;
        }

        if (here > peak)
            peak = here;
    }

    return peak;
}

void beeper_chime_freq_range(const struct beeper_chime_s *chime, uint16_t *lo,
                             uint16_t *hi)
{
    bool     seen    = false;
    uint16_t lowest  = 0;
    uint16_t highest = 0;

    if (chime != NULL && chime->voices != NULL)
    {
        for (uint8_t i = 0; i < chime->count; i++)
        {
            const struct beeper_voice_s *v = &chime->voices[i];

            if (v->notes == NULL)
                continue;

            for (uint8_t j = 0; j < v->count; j++)
            {
                uint16_t ends[2] = {v->notes[j].f_start, v->notes[j].f_end};

                for (uint8_t k = 0; k < 2; k++)
                {
                    if (seen == false || ends[k] < lowest)
                        lowest = ends[k];

                    if (seen == false || ends[k] > highest)
                        highest = ends[k];

                    seen = true;
                }
            }
        }
    }

    *lo = lowest;
    *hi = highest;
}
