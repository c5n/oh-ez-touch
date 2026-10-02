/**
 * @file json_squeeze.h
 *
 * Cut every overlong JSON string short, while the bytes are still arriving.
 *
 * An Image item's state is the whole picture, base64 in a data: URI, and
 * openHAB copies it into everything that mentions the item: a sitemap page
 * carries it twice -- "state" and "lastState" -- and so does every event the
 * sitemap stream sends when a new snapshot lands. A doorbell's snapshot is
 * 50-200 KB of that, against a 12 KB page buffer and a 2 KB event line, so one
 * Image widget on a page used to cost the whole page.
 *
 * Nothing on the panel reads such a string anyway -- a state is copied into a
 * 32 byte field and the picture is fetched on its own -- so this keeps the
 * first JSON_SQUEEZE_STRING_MAX bytes of every string, drops the rest, and
 * keeps the closing quote. What comes out is still valid JSON, only shorter.
 *
 * It filters in place and keeps its state across calls, so a string may be
 * split across any number of reads. A newline resets it: JSON cannot carry a
 * raw newline inside a string, so in an event stream every line starts
 * outside one whatever the line before it did.
 *
 * Pure -- libc only -- so the host tests can reach it.
 */
#ifndef JSON_SQUEEZE_H
#define JSON_SQUEEZE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Longer than any string the panel keeps: a URL is 256 bytes, a label or a
 * state far less. An item event's payload is itself a JSON string, and a few
 * hundred bytes for an ordinary state. */
#define JSON_SQUEEZE_STRING_MAX 512

struct json_squeeze_s
{
    bool     in_string;
    bool     dropping;
    uint8_t  escape;   /* bytes of an escape sequence still to come */
    uint16_t run;      /* bytes of the current string kept so far   */
    size_t   dropped;  /* total, for the log                        */
};

void json_squeeze_reset(struct json_squeeze_s *s);

/**
 * Filter `len` bytes of `buf` in place.
 *
 * @return how many bytes are left at the front of `buf`.
 */
size_t json_squeeze(struct json_squeeze_s *s, char *buf, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* JSON_SQUEEZE_H */
