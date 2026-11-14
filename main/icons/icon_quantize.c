/**
 * @file icon_quantize.c
 *
 * See icon_quantize.h.
 *
 * The cut works on pixel positions, not on a table of distinct colours: the
 * work array holds the index of every visible pixel, and a box is a range of
 * it. That needs 2 bytes per pixel and no allocation. A box is cut where the
 * value on its widest channel changes, at or after the median, so that two
 * pixels of one colour never end up in two boxes -- which a table of
 * distinct colours, as the generator uses, rules out by construction.
 */

#include "icon_quantize.h"

#include <string.h>

#define PALETTE_SIZE 16

struct box_s
{
    uint16_t lo; /* first position in work[] */
    uint16_t hi; /* one past the last        */
};

static inline uint8_t channel_of(const uint8_t *argb, uint16_t pixel, unsigned channel)
{
    return argb[(size_t)pixel * 4u + channel];
}

/* The channel the pixels of `box` are furthest apart on, and that distance. */
static unsigned widest_channel(const uint8_t *argb, const uint16_t *work,
                               const struct box_s *box, unsigned *extent)
{
    unsigned widest = 0;
    unsigned widest_extent = 0;

    for (unsigned channel = 0; channel < 4; channel++)
    {
        uint8_t lo = 255, hi = 0;

        for (unsigned i = box->lo; i < box->hi; i++)
        {
            uint8_t v = channel_of(argb, work[i], channel);

            if (v < lo)
                lo = v;
            if (v > hi)
                hi = v;
        }

        if ((unsigned)(hi - lo) > widest_extent)
        {
            widest_extent = (unsigned)(hi - lo);
            widest = channel;
        }
    }

    *extent = widest_extent;
    return widest;
}

/* Shell sort of work[lo, hi) by one channel. At most 1024 entries for a
 * 32x32 icon; no recursion and no comparator context. */
static void sort_by_channel(const uint8_t *argb, uint16_t *work, unsigned lo,
                            unsigned hi, unsigned channel)
{
    static const unsigned gaps[] = {701, 301, 132, 57, 23, 10, 4, 1};
    unsigned n = hi - lo;

    for (size_t g = 0; g < sizeof(gaps) / sizeof(gaps[0]); g++)
    {
        unsigned gap = gaps[g];

        for (unsigned i = gap; i < n; i++)
        {
            uint16_t item = work[lo + i];
            uint8_t key = channel_of(argb, item, channel);
            unsigned j = i;

            while (j >= gap && channel_of(argb, work[lo + j - gap], channel) > key)
            {
                work[lo + j] = work[lo + j - gap];
                j -= gap;
            }

            work[lo + j] = item;
        }
    }
}

/* Cut `box` in two on `channel`. False if every pixel has the same value
 * there, which the caller rules out by asking only for an extent above 0. */
static bool split_box(const uint8_t *argb, uint16_t *work, struct box_s *box,
                      unsigned channel, struct box_s *upper)
{
    sort_by_channel(argb, work, box->lo, box->hi, channel);

    unsigned mid = box->lo + (box->hi - box->lo) / 2u;
    unsigned cut = mid;

    /* Forward to the first change of value at or after the median... */
    while (cut < box->hi && cut > box->lo
           && channel_of(argb, work[cut], channel) == channel_of(argb, work[cut - 1], channel))
        cut++;

    /* ...or, when the median sits in the last run of values, back to the
     * start of that run. */
    if (cut >= box->hi)
    {
        cut = mid;

        while (cut > box->lo
               && channel_of(argb, work[cut], channel) == channel_of(argb, work[cut - 1], channel))
            cut--;
    }

    if (cut <= box->lo || cut >= box->hi)
        return false;

    upper->lo = (uint16_t)cut;
    upper->hi = box->hi;
    box->hi = (uint16_t)cut;
    return true;
}

bool icon_quantize_i4(const uint8_t *argb, unsigned width, unsigned height,
                      uint16_t *work, uint8_t *out)
{
    size_t pixels = (size_t)width * height;

    if (width % 2u != 0 || pixels > 65535u || pixels == 0)
        return false;

    /* The visible pixels. */
    unsigned visible = 0;

    for (size_t p = 0; p < pixels; p++)
    {
        if (argb[p * 4u + 3u] != 0)
            work[visible++] = (uint16_t)p;
    }

    bool has_transparent = (visible < pixels);
    unsigned budget = PALETTE_SIZE - (has_transparent ? 1u : 0u);

    struct box_s boxes[PALETTE_SIZE];
    unsigned box_count = 0;

    if (visible > 0)
    {
        boxes[0].lo = 0;
        boxes[0].hi = (uint16_t)visible;
        box_count = 1;
    }

    while (box_count < budget)
    {
        int target = -1;
        unsigned target_channel = 0;
        uint32_t target_score = 0;

        for (unsigned b = 0; b < box_count; b++)
        {
            if (boxes[b].hi - boxes[b].lo < 2)
                continue;

            unsigned extent;
            unsigned channel = widest_channel(argb, work, &boxes[b], &extent);
            uint32_t score = (uint32_t)extent * (uint32_t)(boxes[b].hi - boxes[b].lo);

            if (score > target_score)
            {
                target_score = score;
                target = (int)b;
                target_channel = channel;
            }
        }

        if (target < 0)
            break;

        if (split_box(argb, work, &boxes[target], target_channel, &boxes[box_count]) == false)
            break;

        box_count++;
    }

    /* The palette: transparent first, then each box's mean. */
    uint8_t palette[PALETTE_SIZE][4];
    unsigned first_opaque = has_transparent ? 1u : 0u;
    unsigned entries = first_opaque;

    memset(palette, 0, sizeof(palette));

    for (unsigned b = 0; b < box_count; b++)
    {
        uint32_t sum[4] = {0, 0, 0, 0};
        uint32_t count = (uint32_t)(boxes[b].hi - boxes[b].lo);

        for (unsigned i = boxes[b].lo; i < boxes[b].hi; i++)
        {
            for (unsigned c = 0; c < 4; c++)
                sum[c] += channel_of(argb, work[i], c);
        }

        for (unsigned c = 0; c < 4; c++)
            palette[entries][c] = (uint8_t)((sum[c] + count / 2u) / count);

        entries++;
    }

    memcpy(out, palette, sizeof(palette));

    /* The pixels: the nearest opaque entry, never the transparent one. */
    uint8_t *packed = out + sizeof(palette);

    for (size_t p = 0; p < pixels; p++)
    {
        const uint8_t *px = argb + p * 4u;
        unsigned index = 0;

        if (px[3] != 0)
        {
            uint32_t best_distance = UINT32_MAX;

            index = first_opaque;

            for (unsigned e = first_opaque; e < entries; e++)
            {
                uint32_t distance = 0;

                for (unsigned c = 0; c < 4; c++)
                {
                    int d = (int)px[c] - (int)palette[e][c];

                    distance += (uint32_t)(d * d);
                }

                if (distance < best_distance)
                {
                    best_distance = distance;
                    index = e;
                }
            }
        }

        if (p % 2u == 0)
            packed[p / 2u] = (uint8_t)(index << 4);
        else
            packed[p / 2u] |= (uint8_t)index;
    }

    return true;
}
