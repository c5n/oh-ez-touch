/**
 * @file openhab_color.c
 *
 * See openhab_color.h.
 */
#include "openhab_color.h"

#include <ctype.h>
#include <stddef.h>
#include <string.h>
#include <strings.h>

/* The names the openHAB documentation lists for labelcolor, valuecolor and
 * iconcolor, at the values the CSS colours of the same names have -- which is
 * what Basic UI draws them as. "grey" because people write it. */
static const struct
{
    const char *name;
    uint32_t    rgb;
} color_names[] = {
    { "maroon",  0x800000 },
    { "red",     0xFF0000 },
    { "orange",  0xFFA500 },
    { "olive",   0x808000 },
    { "yellow",  0xFFFF00 },
    { "purple",  0x800080 },
    { "fuchsia", 0xFF00FF },
    { "pink",    0xFFC0CB },
    { "white",   0xFFFFFF },
    { "lime",    0x00FF00 },
    { "green",   0x008000 },
    { "navy",    0x000080 },
    { "blue",    0x0000FF },
    { "teal",    0x008080 },
    { "aqua",    0x00FFFF },
    { "black",   0x000000 },
    { "silver",  0xC0C0C0 },
    { "gray",    0x808080 },
    { "grey",    0x808080 },
    { "gold",    0xFFD700 },
};

static int hex_digit(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';

    c = (char)tolower((unsigned char)c);

    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;

    return -1;
}

uint32_t openhab_color_parse(const char *text)
{
    if (text == NULL || text[0] == '\0')
        return OPENHAB_COLOR_NONE;

    if (text[0] == '#')
    {
        size_t   len = strlen(text + 1);
        uint32_t rgb = 0;

        if (len != 6 && len != 3)
            return OPENHAB_COLOR_NONE;

        for (size_t i = 0; i < len; i++)
        {
            int digit = hex_digit(text[1 + i]);

            if (digit < 0)
                return OPENHAB_COLOR_NONE;

            /* "#rgb" is "#rrggbb": every digit twice. */
            rgb = (len == 3) ? ((rgb << 8) | (uint32_t)(digit * 0x11))
                             : ((rgb << 4) | (uint32_t)digit);
        }

        return rgb;
    }

    for (size_t i = 0; i < sizeof(color_names) / sizeof(color_names[0]); i++)
    {
        if (strcasecmp(text, color_names[i].name) == 0)
            return color_names[i].rgb;
    }

    return OPENHAB_COLOR_NONE;
}
