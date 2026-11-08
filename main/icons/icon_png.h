/**
 * @file icon_png.h
 *
 * A server icon, decoded without asking the heap for one large block.
 *
 * lodepng decodes a PNG into two blocks the size of the picture: the inflated
 * scanlines and the RGBA result. For a 64x64 RGBA icon that is 16448 and 16384
 * bytes, each in one piece, and a panel that has been up for a while often has
 * the 16 KB free but not in one block. The icon then stays blank.
 *
 * Here the scanlines are inflated into a chain of ICON_PNG_SEGMENT_SIZE blocks
 * instead -- taken from a scratch area the caller lends first, and from the
 * heap only for the rest -- the row filters are undone in that chain, and the picture is
 * downscaled from it straight into the result. The result is the only block
 * that grows with the picture, and it has the size of the downscaled icon:
 * 4 KB for 32x32. Deflate may refer back to any byte decoded so far, so the
 * chain has to hold the whole inflated picture; it just does not have to be
 * contiguous.
 *
 * Only the formats the icons come in: 8 bits per channel, grey, grey with
 * alpha, RGB, RGBA and palette, not interlaced. Anything else is
 * ICON_PNG_UNSUPPORTED, and the caller decodes it as before.
 *
 * Pure -- libc only -- so the host tests can reach it.
 */
#ifndef ICON_PNG_H
#define ICON_PNG_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The largest block the decode asks for, apart from the result. */
#define ICON_PNG_SEGMENT_SIZE 2048

/* An icon whose inflated scanlines need more segments than this is refused as
 * unsupported: 80 KB, a 128x128 RGBA icon with room to spare. */
#define ICON_PNG_MAX_SEGMENTS 40

enum icon_png_e
{
    ICON_PNG_OK = 0,
    ICON_PNG_BROKEN,      /* not a PNG, or one with damaged image data */
    ICON_PNG_UNSUPPORTED, /* a valid PNG in a format not handled here  */
    ICON_PNG_NO_MEMORY,   /* a segment, the work area or the result    */
};

/**
 * Decode a PNG to LVGL's ARGB8888 (bytes B, G, R, A), halved in both
 * directions for as long as both sides are even and the halves are not
 * smaller than `min_size`. The halvings are taken in one step: each result
 * pixel is the average of its block, weighted by alpha, so transparent pixels
 * do not smear their colour into the strokes.
 *
 * @param scratch     memory the decode may use as segments until it returns,
 *   or NULL. The panel lends the unused tail of the client's receive buffer,
 *   which holds the PNG itself in front of it.
 * @param scratch_len its size; whole segments are taken from it.
 * @param pixels receives the malloc()ed result on ICON_PNG_OK; free() it.
 * @param width  receives the result's width.
 * @param height receives the result's height.
 */
enum icon_png_e icon_png_decode(const uint8_t *png, size_t len, unsigned min_size,
                                void *scratch, size_t scratch_len,
                                uint8_t **pixels, unsigned *width, unsigned *height);

#ifdef __cplusplus
}
#endif

#endif /* ICON_PNG_H */
