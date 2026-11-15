/**
 * @file icon_night.h
 *
 * The openHAB classic icons drawn for a dark tile.
 *
 * The icons are made for a light page: dark grey outlines around white or
 * light grey fills, and colour only where it carries meaning -- a lamp that is
 * on, a window that is open. On a dark tile the outlines vanish. Recolouring
 * the whole icon to one light colour brings them back, but it also fills in
 * every white area, so the icon becomes a silhouette: a lamp becomes a blob, a
 * window becomes a box.
 *
 * icon_night_invert() flips the brightness of the grey pixels only. Outlines
 * turn light and fills turn a dark grey, and the drawing survives. A pixel with
 * colour in it keeps it; between the two, the flip fades out with the colour's
 * strength, so an antialiased edge between a grey and a coloured area does
 * not step. Alpha is not touched.
 *
 * Pure -- libc only -- so the host tests can reach it.
 */

#ifndef ICON_NIGHT_H
#define ICON_NIGHT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Chroma (max - min channel) up to which a pixel is flipped in full. */
#define ICON_NIGHT_GREY_CHROMA 32u
/** Chroma from which a pixel is left as it is. */
#define ICON_NIGHT_COLOUR_CHROMA 96u
/**
 * The flip of a grey v is 255 - v * ICON_NIGHT_FILL_SCALE / 256: black turns
 * white, the classic set's dark grey outlines turn light grey, and a white
 * fill turns a dark grey (87) rather than black. A plain 255 - v made a white
 * fill black, and an icon that is mostly fill -- a lamp that is off -- then
 * disappeared into a dark tile.
 */
#define ICON_NIGHT_FILL_SCALE 170

/**
 * Flip the brightness of the grey pixels of a row, in place.
 *
 * @param bgra  pixels as B, G, R, A bytes, as LVGL's ARGB8888 and the I4
 *   palette store them.
 * @param count number of pixels.
 */
void icon_night_invert(uint8_t *bgra, size_t count);

#ifdef __cplusplus
}
#endif

#endif /* ICON_NIGHT_H */
