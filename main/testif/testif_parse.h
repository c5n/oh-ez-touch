/**
 * @file testif_parse.h
 *
 * The request tokeniser for the test interface.
 *
 * Split out from testif.cpp for the same reason web/multipart.c was split out
 * of webui_ota.cpp: this is the part that reads bytes somebody else chose, and
 * it touches neither LVGL nor a socket, so it is reachable from test/host/.
 * Everything that could be got wrong here -- a token count, a length, a quote
 * that is never closed -- is got wrong on input from outside the process.
 */
#ifndef TESTIF_PARSE_H
#define TESTIF_PARSE_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * The most tokens a request can carry, the command word included.
 *
 * Five is the longest real request (`swipe x1 y1 x2 y2 ms`); eight leaves room
 * without making the struct worth allocating.
 */
#define TESTIF_ARGV_MAX 8

/** The longest request accepted, terminator included. */
#define TESTIF_LINE_MAX 512

struct testif_cmd_s
{
    const char *id;                   /* the @<id> token without its '@', or NULL */
    const char *argv[TESTIF_ARGV_MAX];
    unsigned    argc;                 /* argv[0] is the command word */
    bool        truncated;            /* there were more tokens than argv holds */
};

typedef struct testif_cmd_s testif_cmd_t;

/**
 * Tokenise one request, in place.
 *
 * `line` is modified: separators become terminators, so every `argv[]` entry is
 * an ordinary C string and callers can use strcmp() and strtol() on them. That
 * is safe because the only caller owns the datagram buffer it parses.
 *
 * Tokens are separated by spaces and tabs; a trailing CR or LF is dropped, so a
 * request typed into `nc -u` parses the same as one sent by the CLI. A token may
 * be double-quoted to carry spaces -- there is no escape syntax, because the one
 * thing that needs it is a setting value and a quote is never part of one.
 *
 * A leading token of the form `@<id>` is taken as the correlation id and does
 * not become argv[0]. `@` alone is not an id; it is a token like any other.
 *
 * @return false when there is no command word at all -- an empty datagram, or
 *   one that is nothing but an id. `out` is cleared either way, so a caller
 *   that ignores the result still sees argc == 0 rather than stale tokens.
 */
bool testif_parse(char *line, testif_cmd_t *out);

#ifdef __cplusplus
}
#endif

#endif /* TESTIF_PARSE_H */
