/**
 * @file multipart.h
 *
 * The one multipart/form-data body this firmware reads, scanned a byte at a
 * time.
 *
 * That body is the firmware upload, and it is a megabyte: neither target has
 * a megabyte to buffer it in, so it cannot be handed to a form parser and has
 * to be consumed as it arrives. The shape it has to handle is correspondingly
 * small -- one part, and the boundary is the first line of the body, so the
 * Content-Type header never has to be parsed.
 *
 * Split out of webui_ota.cpp so it can be tested. It was welded to
 * esp_ota_begin() and esp_ota_write(), which made it device-only and so
 * unreachable from test/host -- and this is the one path in the firmware that
 * can leave a panel unbootable, as well as the only one that parses attacker-
 * chosen bytes with no authentication in front of it. The bytes go to a sink
 * the caller supplies; nothing here knows what a firmware image is.
 */
#ifndef MULTIPART_H
#define MULTIPART_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Long enough for any boundary a client will generate -- RFC 2046 caps them
 * at 70 characters -- plus the two leading dashes.
 *
 * A client is not obliged to respect that cap, so the length is enforced
 * rather than assumed: an over-long first line is refused, not truncated. */
#define MULTIPART_BOUNDARY_MAX 80

/* Called once, when the part's headers have ended and its body is about to
 * start. Returning false stops the scan. */
typedef bool (*multipart_begin_fn)(void *ctx);

/* Called with each run of body bytes, in order. Returning false stops the
 * scan. */
typedef bool (*multipart_write_fn)(void *ctx, const char *data, size_t len);

enum multipart_state_e
{
    MULTIPART_BOUNDARY = 0, /* still collecting the first line              */
    MULTIPART_HEADERS,      /* the part's own headers, up to a blank line   */
    MULTIPART_DATA,         /* the payload, until the boundary comes round  */
    MULTIPART_DONE,
};

struct multipart_s
{
    enum multipart_state_e state;

    char   boundary[MULTIPART_BOUNDARY_MAX];
    size_t boundary_len;

    char   head[MULTIPART_BOUNDARY_MAX]; /* the line being collected */
    size_t head_len;
    /* Set when a line did not fit, so the end-of-line handler can tell a
     * complete line from a truncated one. A truncated boundary matches
     * nothing, so the scan would otherwise run to the end of the body and
     * fail with no reason to report. */
    bool head_too_long;

    /* The last boundary_len + 2 bytes seen are held back rather than passed
     * on: they may turn out to be the start of the terminating boundary, and
     * a firmware image with those bytes appended does not verify. */
    char   tail[MULTIPART_BOUNDARY_MAX + 2];
    size_t tail_len;

    multipart_begin_fn begin;
    multipart_write_fn write;
    void              *ctx;

    bool failed;
};

/** Set up a scan. `begin` may be NULL; `write` may not. */
void multipart_init(struct multipart_s *mp, multipart_begin_fn begin,
                    multipart_write_fn write, void *ctx);

/** Feed one byte. Cheap enough to call per byte of a megabyte: the common
 * case is two compares and a store. */
void multipart_feed(struct multipart_s *mp, char c);

/** Feed a block. */
void multipart_feed_block(struct multipart_s *mp, const char *data, size_t len);

/** Whether the terminating boundary was reached with nothing having gone
 * wrong. False while the body is still arriving, and false for good once
 * anything has failed. */
bool multipart_complete(const struct multipart_s *mp);

/** Whether the scan gave up. The reason is logged where it happens. */
bool multipart_failed(const struct multipart_s *mp);

#ifdef __cplusplus
}
#endif

#endif /* MULTIPART_H */
