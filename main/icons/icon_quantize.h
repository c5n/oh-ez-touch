/**
 * @file icon_quantize.h
 *
 * A decoded server icon, reduced to the built-in set's format.
 *
 * icon_png_decode() gives LVGL's ARGB8888: 4096 bytes for a 32x32 icon, held
 * for as long as the tile shows it. The built-in set holds the same picture
 * as an LVGL I4 image in 576 bytes: a 16-entry palette of {blue, green, red,
 * alpha} and the pixels packed two per byte, high nibble first. This reduces
 * a decoded icon to that format, with the median cut that
 * tools/build_icon_set.py uses for the set:
 *
 * - Every fully transparent pixel is one palette entry, index 0. The cut sees
 *   only the visible pixels, so an antialiased edge gets no halo of
 *   transparent black and the other 15 entries go to colours that show.
 * - The box to cut next is the one with the largest extent on its widest
 *   channel times its pixel count. It is cut at the median pixel.
 * - Each entry is the pixel-weighted mean of its box. An icon with no more
 *   colours than entries therefore comes through unchanged.
 * - Each visible pixel takes the nearest entry that is not the transparent
 *   one, so the faint outer ring of an edge stays visible.
 *
 * LVGL draws an I4 image line by line (LV_BIN_DECODER_RAM_LOAD is 0), so the
 * smaller format costs no conversion buffer at draw time.
 *
 * Pure -- libc only -- so the host tests can reach it.
 */

#ifndef ICON_QUANTIZE_H
#define ICON_QUANTIZE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Bytes of the I4 image for a w x h picture: palette, then pixels. */
#define ICON_QUANTIZE_I4_SIZE(w, h) (16u * 4u + (size_t)(w) * (size_t)(h) / 2u)

/** Bytes of work memory icon_quantize_i4() needs, 2-byte aligned. */
#define ICON_QUANTIZE_WORK_SIZE(w, h) ((size_t)(w) * (size_t)(h) * sizeof(uint16_t))

/**
 * Reduce an ARGB8888 picture (bytes B, G, R, A per pixel, no row padding) to
 * an LVGL I4 image with a stride of w / 2.
 *
 * @param argb   the picture, w * h * 4 bytes.
 * @param width  its width; must be even.
 * @param height its height. width * height must not exceed 65535,
 *   the largest end position a uint16_t box can hold.
 * @param work   ICON_QUANTIZE_WORK_SIZE(width, height) bytes, 2-byte aligned.
 *   Its content is undefined on return.
 * @param out    receives ICON_QUANTIZE_I4_SIZE(width, height) bytes. Palette
 *   entries the picture does not need are zero.
 * @return false, and out untouched, for an odd width or a picture too large
 *   for the index type.
 */
bool icon_quantize_i4(const uint8_t *argb, unsigned width, unsigned height,
                      uint16_t *work, uint8_t *out);

#ifdef __cplusplus
}
#endif

#endif /* ICON_QUANTIZE_H */
