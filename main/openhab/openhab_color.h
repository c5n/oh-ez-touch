/**
 * @file openhab_color.h
 *
 * The colours a sitemap's labelcolor, valuecolor and iconcolor rules name.
 *
 * openHAB evaluates the rule and sends the winner as a string on the widget:
 * one of the names its own UIs know ("red", "gold"), or "#rrggbb". This turns
 * that string into 0xRRGGBB, and is pure -- libc only -- so the host tests can
 * reach it.
 */
#ifndef OPENHAB_COLOR_H
#define OPENHAB_COLOR_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* No colour: the theme's own. Outside 24 bits, so no real colour is it. */
#define OPENHAB_COLOR_NONE 0xFFFFFFFFu

/**
 * Read one colour.
 *
 * Names are matched without regard to case, and "#rgb" is read as "#rrggbb"
 * is. Anything else -- NULL, "", an unknown name, "#12345" -- is
 * OPENHAB_COLOR_NONE, which draws the theme's colour: a typo in a sitemap
 * costs the colour, never the tile.
 */
uint32_t openhab_color_parse(const char *text);

#ifdef __cplusplus
}
#endif

#endif /* OPENHAB_COLOR_H */
