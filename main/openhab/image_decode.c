/**
 * @file image_decode.c
 *
 * See image_decode.h.
 */

#include "image_decode.h"

#include <stdlib.h>
#include <string.h>

#include "tjpgd.h"

struct session_s
{
    image_decode_read_fn read;
    void *ctx;
    uint16_t *pixels;
    uint16_t width;
    uint16_t height;
    uint16_t step;
};

/* TJpgDec reads a short count as the end of the stream -- "if (jd->infunc(jd,
 * seg, 4) != 4) return JDR_INP" -- and a socket hands over whatever the last
 * segment held. So this asks again until the request is filled or the stream
 * really has ended. */
static size_t in_func(JDEC *jd, uint8_t *buf, size_t len)
{
    struct session_s *s = (struct session_s *)jd->device;
    size_t done = 0;

    while (done < len)
    {
        size_t n = s->read(s->ctx, (buf != NULL) ? buf + done : NULL, len - done);

        if (n == 0)
            break;

        done += n;
    }

    return done;
}

/* One MCU, already descaled, as RGB565: keep every step-th pixel of it.
 *
 * The rectangle is in descaled picture coordinates, so "every step-th" is
 * decided on the absolute position rather than within the block -- a block
 * edge that is not a multiple of the step would otherwise leave seams. */
static int out_func(JDEC *jd, void *bitmap, JRECT *rect)
{
    struct session_s *s = (struct session_s *)jd->device;
    const uint16_t *src = (const uint16_t *)bitmap;
    unsigned int rw = rect->right - rect->left + 1u;

    for (unsigned int y = rect->top; y <= rect->bottom; ++y)
    {
        if (y % s->step != 0)
            continue;

        unsigned int oy = y / s->step;

        if (oy >= s->height)
            continue;

        const uint16_t *row = src + (size_t)(y - rect->top) * rw;
        uint16_t *dst = s->pixels + (size_t)oy * s->width;

        /* The first column in this block that falls on the step. */
        unsigned int x = rect->left + (s->step - rect->left % s->step) % s->step;

        for (; x <= rect->right; x += s->step)
        {
            unsigned int ox = x / s->step;

            if (ox < s->width)
                dst[ox] = row[x - rect->left];
        }
    }

    return 1;
}

static uint32_t div_up(uint32_t a, uint32_t b)
{
    return (a + b - 1u) / b;
}

void image_decode_fit(uint16_t width, uint16_t height, uint16_t max_w, uint16_t max_h,
                      uint8_t *scale, uint16_t *step, uint16_t *out_w, uint16_t *out_h)
{
    if (max_w == 0)
        max_w = 1;

    if (max_h == 0)
        max_h = 1;

    /* The smallest whole factor that brings both sides inside the box. */
    uint32_t factor = div_up(width, max_w);
    uint32_t fh = div_up(height, max_h);

    if (fh > factor)
        factor = fh;

    if (factor < 1)
        factor = 1;

    /* TJpgDec descales by a power of two and the rest is pixels skipped, so
     * the factor that is reached is 2^scale * step -- which can overshoot:
     * a factor of 6 is 2 * 3, but 4 * 2 is 8, a quarter fewer pixels each
     * way. So every scale is tried and the smallest total wins, the larger
     * scale on a tie because a descaled MCU skips most of the IDCT. */
    uint8_t s = 0;
    uint32_t k = factor;

    for (uint8_t t = 1; t <= 3; ++t)
    {
        uint32_t kt = div_up(factor, 1u << t);

        if ((kt << t) <= (k << s))
        {
            s = t;
            k = kt;
        }
    }

    /* TJpgDec's descaled size: each MCU's width shifted down, and since an
     * MCU is 8 or 16 pixels that adds up to the whole width shifted down. */
    uint32_t ws = (uint32_t)width >> s;
    uint32_t hs = (uint32_t)height >> s;

    *scale = s;
    *step = (uint16_t)k;
    *out_w = (uint16_t)(ws == 0 ? 1 : div_up(ws, k));
    *out_h = (uint16_t)(hs == 0 ? 1 : div_up(hs, k));
}

static enum image_decode_e result_of(JRESULT rc)
{
    switch (rc)
    {
    case JDR_OK:
        return IMAGE_DECODE_OK;

    case JDR_MEM1:
    case JDR_MEM2:
        return IMAGE_DECODE_NO_MEMORY;

    case JDR_FMT2:
    case JDR_FMT3:
        return IMAGE_DECODE_UNSUPPORTED;

    case JDR_INP:
    case JDR_FMT1:
    default:
        return IMAGE_DECODE_BROKEN;
    }
}

enum image_decode_e image_decode_jpeg(image_decode_read_fn read, void *ctx,
                                      uint16_t max_w, uint16_t max_h,
                                      struct image_decode_s *out)
{
    memset(out, 0, sizeof(*out));

    void *work = malloc(IMAGE_DECODE_WORK_SIZE);

    if (work == NULL)
        return IMAGE_DECODE_NO_MEMORY;

    struct session_s s = {};
    JDEC jd;

    s.read = read;
    s.ctx = ctx;

    JRESULT rc = jd_prepare(&jd, in_func, work, IMAGE_DECODE_WORK_SIZE, &s);

    if (rc != JDR_OK)
    {
        free(work);
        return result_of(rc);
    }

    uint8_t scale;

    image_decode_fit(jd.width, jd.height, max_w, max_h, &scale, &s.step, &s.width, &s.height);

    /* Asked for only now that the size is known, and before the decode is:
     * a picture that does not fit the heap fails in a millisecond, without
     * having read the whole stream first. */
    s.pixels = (uint16_t *)malloc((size_t)s.width * s.height * sizeof(uint16_t));

    if (s.pixels == NULL)
    {
        free(work);
        return IMAGE_DECODE_NO_MEMORY;
    }

    /* A stream cut short leaves the bottom of the picture undecoded; black is
     * what that should look like if the caller shows it anyway. */
    memset(s.pixels, 0, (size_t)s.width * s.height * sizeof(uint16_t));

    rc = jd_decomp(&jd, out_func, scale);

    free(work);

    if (rc != JDR_OK)
    {
        free(s.pixels);
        return result_of(rc);
    }

    out->pixels = s.pixels;
    out->width = s.width;
    out->height = s.height;
    out->source_width = jd.width;
    out->source_height = jd.height;

    return IMAGE_DECODE_OK;
}
