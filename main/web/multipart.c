/**
 * @file multipart.c
 *
 * See multipart.h.
 */

#include "multipart.h"

#include <string.h>

#include "esp_log.h"

static const char *TAG = "multipart";

/* tail holds CRLF plus a whole boundary, and feed() writes at index `hold`
 * before it spills. boundary_len is bounded by line_feed() below, so this is
 * the one place the two sizes have to agree. */
_Static_assert(sizeof(((struct multipart_s *)0)->tail)
                   >= sizeof(((struct multipart_s *)0)->boundary) + 2,
               "tail cannot hold CRLF and a full-length boundary");

void multipart_init(struct multipart_s *mp, multipart_begin_fn begin,
                    multipart_write_fn write, void *ctx)
{
    memset(mp, 0, sizeof(*mp));

    mp->state = MULTIPART_BOUNDARY;
    mp->begin = begin;
    mp->write = write;
    mp->ctx = ctx;
}

static void fail(struct multipart_s *mp)
{
    mp->failed = true;
    mp->state = MULTIPART_DONE;
}

/* Collect one line, dropping the CR of a CRLF.
 *
 * @return true when `c` ended the line, in which case head is terminated and
 *   head_len is its length.
 */
static bool line_feed(struct multipart_s *mp, char c)
{
    if (c == '\r')
        return false;

    if (c == '\n')
    {
        mp->head[mp->head_len] = '\0';
        return true;
    }

    /* One byte reserved for the terminator above. */
    if (mp->head_len + 1 < sizeof(mp->head))
        mp->head[mp->head_len++] = c;
    else
        mp->head_too_long = true;

    return false;
}

void multipart_feed(struct multipart_s *mp, char c)
{
    switch (mp->state)
    {
    case MULTIPART_BOUNDARY:
        /* The first line is "--BOUNDARY". Keeping it verbatim means the
         * Content-Type header never has to be read. */
        if (line_feed(mp, c) == false)
            break;

        /* This used to copy head_len + 1 bytes into a boundary eight bytes
         * narrower than head, so a first line of 80 characters or more --
         * which the sender chooses, and nothing checked -- wrote past the end
         * of boundary and over the fields behind it.
         *
         * Measured under ASAN on the host: boundary_len came out as 0x2d2d2d,
         * so `hold` below became millions and tail_len then ran off the end
         * of tail and over *itself*, where it oscillated instead of ever
         * spilling. The image was silently discarded and the partition left
         * half written. Which field the overrun lands on is a question of
         * layout, and the panel's is not the host's, so this is a bound to
         * enforce rather than a behaviour to reason about. */
        if (mp->head_too_long == true)
        {
            ESP_LOGE(TAG, "boundary longer than %u bytes",
                     (unsigned)sizeof(mp->boundary) - 1);
            fail(mp);
            break;
        }

        /* An empty first line is not a boundary, and "" matches at every
         * position -- the scan would end on the first CRLF of the payload. */
        if (mp->head_len == 0)
        {
            ESP_LOGE(TAG, "no boundary on the first line");
            fail(mp);
            break;
        }

        mp->boundary_len = mp->head_len;
        memcpy(mp->boundary, mp->head, mp->head_len + 1);
        mp->head_len = 0;
        mp->state = MULTIPART_HEADERS;
        break;

    case MULTIPART_HEADERS:
        /* The part's own headers, ended by a blank line. Nothing in them is
         * needed -- the field name is ignored and so is the filename -- so an
         * over-long one is truncated rather than refused. */
        if (line_feed(mp, c) == false)
            break;

        if (mp->head_len == 0)
        {
            mp->state = MULTIPART_DATA;

            if (mp->begin != NULL && mp->begin(mp->ctx) == false)
                fail(mp);
        }

        mp->head_len = 0;
        mp->head_too_long = false;
        break;

    case MULTIPART_DATA:
    {
        /* Hold back as much as the terminator could be: CRLF plus the
         * boundary. Anything older than that cannot be part of it, so it is
         * safe to pass on. */
        size_t hold = mp->boundary_len + 2;

        mp->tail[mp->tail_len++] = c;

        if (mp->tail_len > hold)
        {
            size_t spill = mp->tail_len - hold;

            if (mp->write(mp->ctx, mp->tail, spill) == false)
            {
                fail(mp);
                break;
            }

            memmove(mp->tail, mp->tail + spill, hold);
            mp->tail_len = hold;
        }

        /* "\r\n--BOUNDARY" means the payload has ended; what is held back is
         * exactly that and is discarded. */
        if (   mp->tail_len == hold
            && mp->tail[0] == '\r' && mp->tail[1] == '\n'
            && memcmp(mp->tail + 2, mp->boundary, mp->boundary_len) == 0)
        {
            mp->tail_len = 0;
            mp->state = MULTIPART_DONE;
        }
        break;
    }

    case MULTIPART_DONE:
    default:
        break;
    }
}

/* The byte at position p of tail+data, without copying them together. */
static char at(const struct multipart_s *mp, const char *data, size_t p)
{
    return (p < mp->tail_len) ? mp->tail[p] : data[p - mp->tail_len];
}

/* Pass combined[start..end) of tail+data on to the sink. */
static bool emit(struct multipart_s *mp, const char *data,
                 size_t start, size_t end)
{
    if (start < mp->tail_len)
    {
        size_t n = mp->tail_len - start;

        if (n > end - start)
            n = end - start;

        if (n > 0 && mp->write(mp->ctx, mp->tail + start, n) == false)
            return false;

        start += n;
    }

    if (start < end
        && mp->write(mp->ctx, data + (start - mp->tail_len),
                     end - start) == false)
        return false;

    return true;
}

void multipart_feed_block(struct multipart_s *mp, const char *data, size_t len)
{
    size_t i = 0;

    while (i < len && mp->failed == false)
    {
        /* Boundary, headers and everything after the end stay byte-wise: they
         * are a hundred bytes at most. The payload is the megabyte, and
         * feeding it a byte at a time meant one sink call -- one flash write
         * -- and one memmove of the tail per byte, which is what made uploads
         * slow. */
        if (mp->state != MULTIPART_DATA)
        {
            multipart_feed(mp, data[i++]);
            continue;
        }

        size_t hold  = mp->boundary_len + 2;
        size_t avail = mp->tail_len + (len - i);

        /* The terminator, "\r\n--BOUNDARY", is exactly hold bytes; find its
         * first occurrence across tail+data, if there is one. */
        size_t end_at = avail;
        bool   found  = false;

        if (avail >= hold)
        {
            for (size_t k = 0; k + hold <= avail; k++)
            {
                if (at(mp, data + i, k) != '\r'
                    || at(mp, data + i, k + 1) != '\n')
                    continue;

                size_t b;

                for (b = 0; b < mp->boundary_len; b++)
                {
                    if (at(mp, data + i, k + 2 + b) != mp->boundary[b])
                        break;
                }

                if (b == mp->boundary_len)
                {
                    end_at = k;
                    found  = true;
                    break;
                }
            }
        }

        if (found == true)
        {
            /* Everything before the terminator is payload; the rest of the
             * body is ignored, as the byte-wise scan did in MULTIPART_DONE. */
            if (emit(mp, data + i, 0, end_at) == false)
                fail(mp);

            mp->tail_len = 0;
            mp->state = MULTIPART_DONE;
            return;
        }

        /* No terminator in sight: everything but the last hold bytes -- which
         * may still turn out to be its start -- is safe to pass on. */
        size_t safe = (avail > hold) ? avail - hold : 0;

        if (safe > 0 && emit(mp, data + i, 0, safe) == false)
        {
            fail(mp);
            return;
        }

        /* What is kept becomes the new tail. */
        size_t keep = avail - safe;

        for (size_t k = 0; k < keep; k++)
            mp->tail[k] = at(mp, data + i, safe + k);

        mp->tail_len = keep;
        i = len;
    }
}

bool multipart_complete(const struct multipart_s *mp)
{
    return (mp->failed == false && mp->state == MULTIPART_DONE);
}

bool multipart_failed(const struct multipart_s *mp)
{
    return mp->failed;
}
