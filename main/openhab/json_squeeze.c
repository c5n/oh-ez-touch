/**
 * @file json_squeeze.c
 *
 * See json_squeeze.h.
 */

#include "json_squeeze.h"

#include <string.h>

void json_squeeze_reset(struct json_squeeze_s *s)
{
    memset(s, 0, sizeof(*s));
}

/* Where a string may be cut: not inside an escape -- "\" followed by the
 * closing quote would escape it, and "\u12" is not JSON -- and not inside a
 * UTF-8 sequence, whose continuation bytes are 10xxxxxx. */
static bool may_cut_before(const struct json_squeeze_s *s, unsigned char c)
{
    return s->escape == 0 && c != '\\' && (c & 0xC0u) != 0x80u;
}

size_t json_squeeze(struct json_squeeze_s *s, char *buf, size_t len)
{
    size_t out = 0;

    for (size_t i = 0; i < len; ++i)
    {
        unsigned char c = (unsigned char)buf[i];

        if (c == '\n')
        {
            size_t dropped = s->dropped;

            json_squeeze_reset(s);
            s->dropped = dropped;
            buf[out++] = (char)c;
            continue;
        }

        if (s->in_string == false)
        {
            if (c == '"')
            {
                s->in_string = true;
                s->run = 0;
            }

            buf[out++] = (char)c;
            continue;
        }

        if (s->escape == 0 && c == '"')
        {
            s->in_string = false;
            s->dropping = false;
            buf[out++] = (char)c;
            continue;
        }

        if (s->dropping == false && s->run >= JSON_SQUEEZE_STRING_MAX
            && may_cut_before(s, c) == true)
            s->dropping = true;

        /* Tracked whether the byte is kept or not: it is what tells an
         * escaped quote from the one that ends the string. "\u" asks for four
         * hex digits after it, every other escape for the one byte. */
        if (s->escape > 0)
            s->escape = (s->escape == 1 && c == 'u') ? 4 : (uint8_t)(s->escape - 1);
        else if (c == '\\')
            s->escape = 1;

        if (s->dropping == true)
        {
            ++s->dropped;
            continue;
        }

        ++s->run;
        buf[out++] = (char)c;
    }

    return out;
}
