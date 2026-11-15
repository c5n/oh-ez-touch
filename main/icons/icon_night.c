/**
 * @file icon_night.c
 *
 * See icon_night.h.
 */

#include "icon_night.h"

void icon_night_invert(uint8_t *bgra, size_t count)
{
    for (size_t i = 0; i < count; i++, bgra += 4)
    {
        if (bgra[3] == 0)
            continue;

        uint8_t max = bgra[0];
        uint8_t min = bgra[0];

        for (unsigned c = 1; c < 3; c++)
        {
            if (bgra[c] > max)
                max = bgra[c];
            if (bgra[c] < min)
                min = bgra[c];
        }

        unsigned chroma = (unsigned)(max - min);

        if (chroma >= ICON_NIGHT_COLOUR_CHROMA)
            continue;

        /* Weight of the flip, 0..256: full for grey, none for colour. */
        unsigned k = 256u;

        if (chroma > ICON_NIGHT_GREY_CHROMA)
            k = (ICON_NIGHT_COLOUR_CHROMA - chroma) * 256u
                / (ICON_NIGHT_COLOUR_CHROMA - ICON_NIGHT_GREY_CHROMA);

        for (unsigned c = 0; c < 3; c++)
        {
            int v = bgra[c];
            int flipped = 255 - (v * ICON_NIGHT_FILL_SCALE) / 256;

            bgra[c] = (uint8_t)(v + ((flipped - v) * (int)k) / 256);
        }
    }
}
