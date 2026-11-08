/**
 * @file icon_png.c
 *
 * See icon_png.h.
 *
 * The inflate follows the structure of Mark Adler's puff.c from the zlib
 * distribution (zlib licence): canonical Huffman codes decoded one bit at a
 * time from a count and a symbol table. That is slow next to zlib, and for an
 * icon of a few kilobytes it does not matter; what matters here is that its
 * tables are small and its output is a plain byte position, which is what
 * lets it write into a chain of segments.
 */

#include "icon_png.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#define MAX_BITS  15
#define MAX_LCODES 286
#define MAX_DCODES 30
#define FIX_LCODES 288

/* The inflated scanlines, ICON_PNG_SEGMENT_SIZE bytes per segment. The
 * first ones come out of the caller's scratch area, the rest from the heap. */
struct raw_s
{
    uint8_t *seg[ICON_PNG_MAX_SEGMENTS];
    size_t   len;
    uint8_t *scratch;
    size_t   scratch_segments; /* segments 0 .. this-1 are in the scratch area */
};

static inline uint8_t raw_get(const struct raw_s *raw, size_t pos)
{
    return raw->seg[pos / ICON_PNG_SEGMENT_SIZE][pos % ICON_PNG_SEGMENT_SIZE];
}

static inline void raw_set(struct raw_s *raw, size_t pos, uint8_t value)
{
    raw->seg[pos / ICON_PNG_SEGMENT_SIZE][pos % ICON_PNG_SEGMENT_SIZE] = value;
}

static void raw_free_segment(struct raw_s *raw, size_t index)
{
    if (index >= raw->scratch_segments)
        free(raw->seg[index]);

    raw->seg[index] = NULL;
}

static void raw_free(struct raw_s *raw)
{
    for (size_t i = 0; i < ICON_PNG_MAX_SEGMENTS; i++)
        raw_free_segment(raw, i);
}

struct huffman_s
{
    short *count;  /* codes per length, 0..MAX_BITS */
    short *symbol; /* symbols in canonical order    */
};

/* About 1.4 KB, so it is allocated rather than put on the UI task's stack. */
struct inflate_s
{
    /* The IDAT chunks, read as one stream without copying them together. */
    const uint8_t *png;
    size_t         png_len;
    size_t         next_chunk;
    const uint8_t *in;
    size_t         in_left;

    uint32_t bitbuf;
    unsigned bitcnt;

    struct raw_s *raw;
    size_t        out;

    enum icon_png_e error;

    short lencnt[MAX_BITS + 1];
    short lensym[FIX_LCODES];
    short distcnt[MAX_BITS + 1];
    short distsym[MAX_DCODES];
    short lengths[FIX_LCODES + MAX_DCODES];
};

static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static void fail(struct inflate_s *s, enum icon_png_e error)
{
    if (s->error == ICON_PNG_OK)
        s->error = error;
}

/* The next byte of image data. The IDAT chunks of a PNG follow one another
 * with nothing in between, so the first chunk that is not one ends the data. */
static uint8_t next_byte(struct inflate_s *s)
{
    while (s->in_left == 0)
    {
        if (s->next_chunk + 12 > s->png_len)
        {
            fail(s, ICON_PNG_BROKEN);
            return 0;
        }

        const uint8_t *chunk = s->png + s->next_chunk;
        uint32_t length = be32(chunk);

        if (memcmp(chunk + 4, "IDAT", 4) != 0 || length > s->png_len - s->next_chunk - 12)
        {
            fail(s, ICON_PNG_BROKEN);
            return 0;
        }

        s->in = chunk + 8;
        s->in_left = length;
        s->next_chunk += 12 + length;
    }

    s->in_left--;

    return *s->in++;
}

static unsigned bits(struct inflate_s *s, unsigned need)
{
    uint32_t value = s->bitbuf;

    while (s->bitcnt < need)
    {
        value |= (uint32_t)next_byte(s) << s->bitcnt;
        s->bitcnt += 8;
    }

    s->bitbuf = value >> need;
    s->bitcnt -= need;

    return value & ((1u << need) - 1);
}

static void put(struct inflate_s *s, uint8_t value)
{
    struct raw_s *raw = s->raw;

    if (s->out >= raw->len)
    {
        fail(s, ICON_PNG_BROKEN);
        return;
    }

    size_t index = s->out / ICON_PNG_SEGMENT_SIZE;

    if (raw->seg[index] == NULL)
    {
        size_t start = index * ICON_PNG_SEGMENT_SIZE;
        size_t size = raw->len - start;

        if (size > ICON_PNG_SEGMENT_SIZE)
            size = ICON_PNG_SEGMENT_SIZE;

        raw->seg[index] = (index < raw->scratch_segments)
                              ? raw->scratch + start
                              : (uint8_t *)malloc(size);

        if (raw->seg[index] == NULL)
        {
            fail(s, ICON_PNG_NO_MEMORY);
            return;
        }
    }

    raw_set(raw, s->out++, value);
}

static int decode(struct inflate_s *s, const struct huffman_s *h)
{
    int code = 0, first = 0, index = 0;

    for (int len = 1; len <= MAX_BITS; len++)
    {
        code |= (int)bits(s, 1);

        int count = h->count[len];

        if (code - count < first)
            return h->symbol[index + (code - first)];

        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }

    fail(s, ICON_PNG_BROKEN);

    return -1;
}

/* False for an over-subscribed set of lengths. An incomplete one is accepted
 * and fails in decode() if a missing code turns up. */
static bool construct(struct huffman_s *h, const short *length, int n)
{
    short offs[MAX_BITS + 1];

    memset(h->count, 0, sizeof(short) * (MAX_BITS + 1));

    for (int symbol = 0; symbol < n; symbol++)
        h->count[length[symbol]]++;

    int left = 1;

    for (int len = 1; len <= MAX_BITS; len++)
    {
        left <<= 1;
        left -= h->count[len];

        if (left < 0)
            return false;
    }

    offs[1] = 0;

    for (int len = 1; len < MAX_BITS; len++)
        offs[len + 1] = (short)(offs[len] + h->count[len]);

    for (int symbol = 0; symbol < n; symbol++)
    {
        if (length[symbol] != 0)
            h->symbol[offs[length[symbol]]++] = (short)symbol;
    }

    return true;
}

static void codes(struct inflate_s *s, const struct huffman_s *lencode,
                  const struct huffman_s *distcode)
{
    static const short lbase[29] = {3,  4,  5,  6,  7,  8,  9,  10, 11,  13,
                                    15, 17, 19, 23, 27, 31, 35, 43, 51,  59,
                                    67, 83, 99, 115, 131, 163, 195, 227, 258};
    static const short lext[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                   2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
    static const short dbase[30] = {1,    2,    3,    4,    5,    7,     9,     13,
                                    17,   25,   33,   49,   65,   97,    129,   193,
                                    257,  385,  513,  769,  1025, 1537,  2049,  3073,
                                    4097, 6145, 8193, 12289, 16385, 24577};
    static const short dext[30] = {0, 0, 0, 0, 1, 1, 2,  2,  3,  3,  4,  4,  5,  5,  6,
                                   6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

    while (s->error == ICON_PNG_OK)
    {
        int symbol = decode(s, lencode);

        if (symbol < 0)
            return;

        if (symbol < 256)
        {
            put(s, (uint8_t)symbol);
            continue;
        }

        if (symbol == 256)
            return;

        symbol -= 257;

        if (symbol >= 29)
        {
            fail(s, ICON_PNG_BROKEN);
            return;
        }

        size_t len = (size_t)lbase[symbol] + bits(s, (unsigned)lext[symbol]);

        symbol = decode(s, distcode);

        if (symbol < 0 || symbol >= 30)
        {
            fail(s, ICON_PNG_BROKEN);
            return;
        }

        size_t dist = (size_t)dbase[symbol] + bits(s, (unsigned)dext[symbol]);

        if (dist > s->out)
        {
            fail(s, ICON_PNG_BROKEN);
            return;
        }

        while (len-- > 0 && s->error == ICON_PNG_OK)
            put(s, raw_get(s->raw, s->out - dist));
    }
}

static void stored(struct inflate_s *s)
{
    s->bitbuf = 0;
    s->bitcnt = 0;

    unsigned len = next_byte(s);
    len |= (unsigned)next_byte(s) << 8;

    unsigned nlen = next_byte(s);
    nlen |= (unsigned)next_byte(s) << 8;

    if (len != (~nlen & 0xffff))
    {
        fail(s, ICON_PNG_BROKEN);
        return;
    }

    while (len-- > 0 && s->error == ICON_PNG_OK)
        put(s, next_byte(s));
}

static void fixed(struct inflate_s *s)
{
    struct huffman_s lencode = {s->lencnt, s->lensym};
    struct huffman_s distcode = {s->distcnt, s->distsym};
    int symbol;

    for (symbol = 0; symbol < 144; symbol++)
        s->lengths[symbol] = 8;
    for (; symbol < 256; symbol++)
        s->lengths[symbol] = 9;
    for (; symbol < 280; symbol++)
        s->lengths[symbol] = 7;
    for (; symbol < FIX_LCODES; symbol++)
        s->lengths[symbol] = 8;

    construct(&lencode, s->lengths, FIX_LCODES);

    for (symbol = 0; symbol < MAX_DCODES; symbol++)
        s->lengths[symbol] = 5;

    construct(&distcode, s->lengths, MAX_DCODES);

    codes(s, &lencode, &distcode);
}

static void dynamic(struct inflate_s *s)
{
    static const uint8_t order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5,
                                      11, 4, 12, 3, 13, 2, 14, 1, 15};
    struct huffman_s lencode = {s->lencnt, s->lensym};
    struct huffman_s distcode = {s->distcnt, s->distsym};

    int nlen = (int)bits(s, 5) + 257;
    int ndist = (int)bits(s, 5) + 1;
    int ncode = (int)bits(s, 4) + 4;

    if (nlen > MAX_LCODES || ndist > MAX_DCODES)
    {
        fail(s, ICON_PNG_BROKEN);
        return;
    }

    int index;

    for (index = 0; index < ncode; index++)
        s->lengths[order[index]] = (short)bits(s, 3);
    for (; index < 19; index++)
        s->lengths[order[index]] = 0;

    if (construct(&lencode, s->lengths, 19) == false)
    {
        fail(s, ICON_PNG_BROKEN);
        return;
    }

    index = 0;

    while (index < nlen + ndist && s->error == ICON_PNG_OK)
    {
        int symbol = decode(s, &lencode);

        if (symbol < 0)
            return;

        if (symbol < 16)
        {
            s->lengths[index++] = (short)symbol;
            continue;
        }

        short len = 0;
        int repeat;

        if (symbol == 16)
        {
            if (index == 0)
            {
                fail(s, ICON_PNG_BROKEN);
                return;
            }

            len = s->lengths[index - 1];
            repeat = 3 + (int)bits(s, 2);
        }
        else if (symbol == 17)
        {
            repeat = 3 + (int)bits(s, 3);
        }
        else
        {
            repeat = 11 + (int)bits(s, 7);
        }

        if (index + repeat > nlen + ndist)
        {
            fail(s, ICON_PNG_BROKEN);
            return;
        }

        while (repeat-- > 0)
            s->lengths[index++] = len;
    }

    if (s->error != ICON_PNG_OK)
        return;

    /* Without a code for end-of-block the block could never end. */
    if (s->lengths[256] == 0
        || construct(&lencode, s->lengths, nlen) == false
        || construct(&distcode, s->lengths + nlen, ndist) == false)
    {
        fail(s, ICON_PNG_BROKEN);
        return;
    }

    codes(s, &lencode, &distcode);
}

static void inflate_zlib(struct inflate_s *s)
{
    unsigned cmf = next_byte(s);
    unsigned flg = next_byte(s);

    /* Deflate, a window of at most 32 KB, no preset dictionary. */
    if ((cmf & 0x0f) != 8 || (cmf >> 4) > 7 || ((cmf << 8) | flg) % 31 != 0 || (flg & 0x20) != 0)
    {
        fail(s, ICON_PNG_BROKEN);
        return;
    }

    unsigned last;

    do
    {
        last = bits(s, 1);

        switch (bits(s, 2))
        {
        case 0:
            stored(s);
            break;
        case 1:
            fixed(s);
            break;
        case 2:
            dynamic(s);
            break;
        default:
            fail(s, ICON_PNG_BROKEN);
            break;
        }
    } while (last == 0 && s->error == ICON_PNG_OK);

    /* The Adler-32 after the data is not checked: a damaged stream fails the
     * Huffman decode or the size check far more often than not, and an icon
     * that slips through is a wrong picture, not a wrong state. */
    if (s->error == ICON_PNG_OK && s->out != s->raw->len)
        fail(s, ICON_PNG_BROKEN);
}

static uint8_t paeth(uint8_t a, uint8_t b, uint8_t c)
{
    int p = (int)a + b - c;
    int pa = abs(p - a), pb = abs(p - b), pc = abs(p - c);

    if (pa <= pb && pa <= pc)
        return a;

    return (pb <= pc) ? b : c;
}

/* In place, a row at a time: each row's filter refers to the row above,
 * which by then is unfiltered already. */
static bool unfilter(struct raw_s *raw, unsigned height, size_t line, unsigned bpp)
{
    for (unsigned y = 0; y < height; y++)
    {
        size_t row = (size_t)y * (line + 1);
        size_t prev = row - (line + 1);
        uint8_t filter = raw_get(raw, row);

        if (filter > 4)
            return false;

        if (filter == 0)
            continue;

        for (size_t i = 0; i < line; i++)
        {
            size_t pos = row + 1 + i;
            uint8_t a = (i >= bpp) ? raw_get(raw, pos - bpp) : 0;
            uint8_t b = (y > 0) ? raw_get(raw, prev + 1 + i) : 0;
            uint8_t c = (y > 0 && i >= bpp) ? raw_get(raw, prev + 1 + i - bpp) : 0;
            uint8_t x = raw_get(raw, pos);

            switch (filter)
            {
            case 1:
                x = (uint8_t)(x + a);
                break;
            case 2:
                x = (uint8_t)(x + b);
                break;
            case 3:
                x = (uint8_t)(x + ((a + b) >> 1));
                break;
            default:
                x = (uint8_t)(x + paeth(a, b, c));
                break;
            }

            raw_set(raw, pos, x);
        }
    }

    return true;
}

struct format_s
{
    unsigned       width;
    unsigned       height;
    uint8_t        color_type;
    unsigned       bpp;
    const uint8_t *palette; /* PLTE, 3 bytes an entry */
    unsigned       palette_size;
    const uint8_t *trns;    /* tRNS as the colour type defines it */
    unsigned       trns_size;
};

/* One source pixel as R, G, B, A. */
static void pixel(const struct raw_s *raw, const struct format_s *f, size_t pos, uint8_t rgba[4])
{
    switch (f->color_type)
    {
    case 0: /* grey */
        rgba[0] = rgba[1] = rgba[2] = raw_get(raw, pos);
        rgba[3] = (f->trns_size == 2 && f->trns[0] == 0 && f->trns[1] == rgba[0]) ? 0 : 255;
        break;
    case 2: /* RGB */
        rgba[0] = raw_get(raw, pos);
        rgba[1] = raw_get(raw, pos + 1);
        rgba[2] = raw_get(raw, pos + 2);
        rgba[3] = (f->trns_size == 6 && f->trns[0] == 0 && f->trns[1] == rgba[0]
                   && f->trns[2] == 0 && f->trns[3] == rgba[1]
                   && f->trns[4] == 0 && f->trns[5] == rgba[2]) ? 0 : 255;
        break;
    case 3: /* palette */
    {
        unsigned index = raw_get(raw, pos);

        if (index < f->palette_size)
        {
            rgba[0] = f->palette[index * 3];
            rgba[1] = f->palette[index * 3 + 1];
            rgba[2] = f->palette[index * 3 + 2];
        }
        else
        {
            rgba[0] = rgba[1] = rgba[2] = 0;
        }

        rgba[3] = (index < f->trns_size) ? f->trns[index] : 255;
        break;
    }
    case 4: /* grey and alpha */
        rgba[0] = rgba[1] = rgba[2] = raw_get(raw, pos);
        rgba[3] = raw_get(raw, pos + 1);
        break;
    default: /* RGBA */
        rgba[0] = raw_get(raw, pos);
        rgba[1] = raw_get(raw, pos + 1);
        rgba[2] = raw_get(raw, pos + 2);
        rgba[3] = raw_get(raw, pos + 3);
        break;
    }
}

/* Read the chunks before the image data. */
static enum icon_png_e header(const uint8_t *png, size_t len, struct format_s *f, size_t *idat)
{
    static const uint8_t signature[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};

    if (len < 8 + 25 || memcmp(png, signature, 8) != 0 || memcmp(png + 12, "IHDR", 4) != 0
        || be32(png + 8) != 13)
        return ICON_PNG_BROKEN;

    const uint8_t *ihdr = png + 16;

    memset(f, 0, sizeof(*f));
    f->width = be32(ihdr);
    f->height = be32(ihdr + 4);
    f->color_type = ihdr[9];

    if (f->width == 0 || f->height == 0 || ihdr[10] != 0 || ihdr[11] != 0)
        return ICON_PNG_BROKEN;

    static const uint8_t channels[7] = {1, 0, 3, 1, 2, 0, 4};

    if (f->color_type > 6 || channels[f->color_type] == 0)
        return ICON_PNG_BROKEN;

    /* 16 bits, fewer than 8, or Adam7: valid, and left to lodepng. */
    if (ihdr[8] != 8 || ihdr[12] != 0)
        return ICON_PNG_UNSUPPORTED;

    f->bpp = channels[f->color_type];

    for (size_t pos = 8 + 25; pos + 12 <= len;)
    {
        uint32_t length = be32(png + pos);
        const uint8_t *type = png + pos + 4;
        const uint8_t *data = png + pos + 8;

        if (length > len - pos - 12)
            return ICON_PNG_BROKEN;

        if (memcmp(type, "IDAT", 4) == 0)
        {
            if (f->color_type == 3 && f->palette == NULL)
                return ICON_PNG_BROKEN;

            *idat = pos;
            return ICON_PNG_OK;
        }

        if (memcmp(type, "PLTE", 4) == 0)
        {
            if (length % 3 != 0 || length > 3 * 256)
                return ICON_PNG_BROKEN;

            f->palette = data;
            f->palette_size = length / 3;
        }
        else if (memcmp(type, "tRNS", 4) == 0)
        {
            f->trns = data;
            f->trns_size = length;
        }

        pos += 12 + length;
    }

    return ICON_PNG_BROKEN;
}

enum icon_png_e icon_png_decode(const uint8_t *png, size_t len, unsigned min_size,
                                void *scratch, size_t scratch_len,
                                uint8_t **pixels, unsigned *width, unsigned *height)
{
    struct format_s f;
    size_t idat;
    enum icon_png_e result = header(png, len, &f, &idat);

    if (result != ICON_PNG_OK)
        return result;

    /* A width this large would overflow the line below long before the
     * segment limit refused it. */
    if (f.width > 4096 || f.height > 4096)
        return ICON_PNG_UNSUPPORTED;

    size_t line = (size_t)f.width * f.bpp;
    struct raw_s raw = {0};

    raw.len = (line + 1) * f.height;
    raw.scratch = (uint8_t *)scratch;
    raw.scratch_segments = (scratch != NULL) ? scratch_len / ICON_PNG_SEGMENT_SIZE : 0;

    if (raw.len > (size_t)ICON_PNG_SEGMENT_SIZE * ICON_PNG_MAX_SEGMENTS)
        return ICON_PNG_UNSUPPORTED;

    struct inflate_s *s = (struct inflate_s *)calloc(1, sizeof(*s));

    if (s == NULL)
        return ICON_PNG_NO_MEMORY;

    s->png = png;
    s->png_len = len;
    s->next_chunk = idat;
    s->raw = &raw;

    inflate_zlib(s);
    result = s->error;
    free(s);

    if (result == ICON_PNG_OK && unfilter(&raw, f.height, line, f.bpp) == false)
        result = ICON_PNG_BROKEN;

    if (result != ICON_PNG_OK)
    {
        raw_free(&raw);
        return result;
    }

    /* The halvings, as one block average of `factor` x `factor` pixels. */
    unsigned factor = 1;

    while ((f.width / factor) % 2 == 0 && (f.height / factor) % 2 == 0
           && f.width / factor / 2 >= min_size && f.height / factor / 2 >= min_size)
        factor *= 2;

    unsigned out_w = f.width / factor;
    unsigned out_h = f.height / factor;
    uint8_t *out = (uint8_t *)malloc((size_t)out_w * out_h * 4);

    if (out == NULL)
    {
        raw_free(&raw);
        return ICON_PNG_NO_MEMORY;
    }

    for (unsigned y = 0; y < out_h; y++)
    {
        for (unsigned x = 0; x < out_w; x++)
        {
            uint32_t r = 0, g = 0, b = 0, a = 0;

            for (unsigned dy = 0; dy < factor; dy++)
            {
                size_t row = (size_t)(y * factor + dy) * (line + 1) + 1;

                for (unsigned dx = 0; dx < factor; dx++)
                {
                    uint8_t rgba[4];

                    pixel(&raw, &f, row + (size_t)(x * factor + dx) * f.bpp, rgba);

                    a += rgba[3];
                    r += (uint32_t)rgba[0] * rgba[3];
                    g += (uint32_t)rgba[1] * rgba[3];
                    b += (uint32_t)rgba[2] * rgba[3];
                }
            }

            uint8_t *d = out + ((size_t)y * out_w + x) * 4;

            if (factor == 1)
            {
                /* No average to take: a fully transparent pixel keeps its
                 * colour, as lodepng's result does. */
                uint8_t rgba[4];

                pixel(&raw, &f, (size_t)y * (line + 1) + 1 + (size_t)x * f.bpp, rgba);
                d[0] = rgba[2];
                d[1] = rgba[1];
                d[2] = rgba[0];
                d[3] = rgba[3];
            }
            else if (a == 0)
            {
                d[0] = d[1] = d[2] = d[3] = 0;
            }
            else
            {
                d[0] = (uint8_t)(b / a);
                d[1] = (uint8_t)(g / a);
                d[2] = (uint8_t)(r / a);
                d[3] = (uint8_t)(a / (factor * factor));
            }
        }

        /* The rows of this output row are read for the last time. */
        size_t done = (size_t)(y + 1) * factor * (line + 1);

        for (size_t i = 0; (i + 1) * ICON_PNG_SEGMENT_SIZE <= done; i++)
            raw_free_segment(&raw, i);
    }

    raw_free(&raw);

    *pixels = out;
    *width = out_w;
    *height = out_h;

    return ICON_PNG_OK;
}
