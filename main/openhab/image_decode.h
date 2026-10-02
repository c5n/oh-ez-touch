/**
 * @file image_decode.h
 *
 * A camera snapshot, decoded as it arrives and shrunk while it is decoded.
 *
 * The panel has no PSRAM. A doorbell's snapshot is 640x480 to 1920x1080 and
 * 50-200 KB of JPEG; even the screen's own 320x240 at RGB565 is 150 KB, more
 * than the heap has in one piece. So neither the JPEG nor the full-size
 * picture is ever held: TJpgDec pulls the stream through a 512 byte buffer,
 * decodes one MCU at a time into a few kilobytes of work area, descales by
 * 1/2, 1/4 or 1/8 on the way, and every further reduction is a pixel skipped
 * as it is written. The one allocation that grows with the picture is the
 * result, and that is sized by the caller's box, not by the camera.
 *
 * Pure -- TJpgDec and libc -- so the host tests can reach it.
 */
#ifndef IMAGE_DECODE_H
#define IMAGE_DECODE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* TJpgDec's own figure is 3100 bytes "for most JPEG images" plus 320 for
 * JD_FASTDECODE 1; this is that with room for a file with unusual tables. */
#define IMAGE_DECODE_WORK_SIZE 3600

enum image_decode_e
{
    IMAGE_DECODE_OK = 0,
    IMAGE_DECODE_BROKEN,      /* not a JPEG, or one cut short          */
    IMAGE_DECODE_UNSUPPORTED, /* progressive or otherwise exotic JPEG  */
    IMAGE_DECODE_NO_MEMORY,   /* the work area or the result           */
};

/**
 * Where the bytes come from.
 *
 * @param buf where to put them, or NULL to skip `len` bytes.
 * @return how many were read or skipped; less than `len` only at the end of
 *   the stream or on an error, which TJpgDec treats alike.
 */
typedef size_t (*image_decode_read_fn)(void *ctx, uint8_t *buf, size_t len);

struct image_decode_s
{
    uint16_t *pixels; /* RGB565, width * height, malloc()ed; free() it */
    uint16_t  width;
    uint16_t  height;
    uint16_t  source_width;
    uint16_t  source_height;
};

/**
 * How a `width` x `height` picture is brought inside `max_w` x `max_h`:
 * TJpgDec's descale (0..3, a factor of 1 << scale) and then every
 * `step`-th pixel of what that gives. Never enlarges. Exposed for the tests.
 */
void image_decode_fit(uint16_t width, uint16_t height, uint16_t max_w, uint16_t max_h,
                      uint8_t *scale, uint16_t *step, uint16_t *out_w, uint16_t *out_h);

/**
 * Decode one JPEG to at most `max_w` x `max_h`, keeping its aspect ratio.
 *
 * Reads only as far as the last MCU; the caller drains or drops the rest of
 * the stream. On anything but IMAGE_DECODE_OK, `out->pixels` is NULL and
 * nothing is left allocated.
 */
enum image_decode_e image_decode_jpeg(image_decode_read_fn read, void *ctx,
                                      uint16_t max_w, uint16_t max_h,
                                      struct image_decode_s *out);

#ifdef __cplusplus
}
#endif

#endif /* IMAGE_DECODE_H */
