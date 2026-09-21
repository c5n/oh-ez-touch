/**
 * @file openhab_http.hpp
 *
 * The two HTTP requests this firmware makes, over esp_http_client.
 *
 * Everything it asks of openHAB is either "GET this and give me the body" or
 * "POST this string and tell me it worked", in three places: an item's state, a
 * sitemap page, and a widget icon. So the transport lives here once rather than
 * three times, and -- this is the point of moving off
 * HTTPClient -- it is the *same* transport on both targets. esp_http_client
 * builds for the linux target against the host's own socket API, so the
 * simulator makes real requests to a real openHAB instead of reading canned
 * fixtures.
 *
 * Both calls block, and both are called from exactly one place: the openHAB
 * client task in openhab_client.cpp. That used to be the task that also drives
 * LVGL, so a slow or unreachable openHAB stalled the screen for up to the
 * timeout below; it no longer is, and nothing here has to be thread safe
 * because nothing else calls it. The single client handle these two share
 * depends on that.
 */
#ifndef OPENHAB_HTTP_HPP
#define OPENHAB_HTTP_HPP

#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>

/* Matches what HTTPClient defaulted to. It no longer stalls anything the user
 * can see -- it is how long the client task waits before reporting a failure,
 * which the connection-error statistics then count. */
#define OPENHAB_HTTP_TIMEOUT_MS 5000

/* "scheme://host:port". The configured hostname is 32 bytes and the port five
 * digits, so this is roomy; it only has to hold enough to tell one server from
 * another. */
#define STR_AUTHORITY_LEN 64

/**
 * GET `url` and copy the body into `buf`.
 *
 * The body is the *decoded* body: esp_http_client undoes chunked transfer
 * encoding, so callers get bytes and not framing. Item::getIcon() used to have
 * to strip that framing by hand.
 *
 * @param truncate what a body larger than `buf_size` means, and nothing else. A
 *   plain-text item state passes true: it is copied into a fixed-width field
 *   either way, and truncating it is what HTTPClient::getString() plus
 *   strlcpy() did. JSON and PNG pass false, because half of either is worse
 *   than none -- it would fail to parse or decode somewhere far from the cause.
 *   A body cut short by the *network* is a failure for every caller, whatever
 *   this says: see the classification in http_get_attempt().
 * @return bytes copied, or -1 on a transport error, a status other than 200, an
 *         incomplete body, or an over-long body with `truncate` false. Failures
 *         are logged here with the method, URL and status; callers add their
 *         own context.
 */
ssize_t openhab_http_get(const char *url, void *buf, size_t buf_size, bool truncate);

/**
 * POST `body` to `url` as text/plain, which is how openHAB's REST API takes a
 * command or a state update.
 *
 * @return 0, or -1 on a transport error or a status outside 2xx. openHAB
 *         answers 200 for a command and 202 for an accepted state update.
 */
int openhab_http_post_text(const char *url, const char *body);

/**
 * Drop the connection before the next request, wherever it is in its life.
 *
 * For the one failure the retry above cannot see coming: a reassociation. The
 * panel knows the link went down and came back, and it knows the socket that
 * spanned that gap is dead -- but the client does not, so the first request
 * afterwards writes into the dead socket, succeeds, and then spends the whole
 * of OPENHAB_HTTP_TIMEOUT_MS waiting for an answer that cannot come. Telling
 * it here costs a handshake and saves that wait.
 *
 * The only call that may be made from another task. It sets a flag; the
 * connection is closed by the client task itself, at the top of the next
 * request, because every esp_http_client call in this file has to stay on the
 * one task that owns the handle -- closing it from under a request in flight
 * is exactly the race the single-task contract exists to prevent.
 */
void openhab_http_reset(void);

#endif /* OPENHAB_HTTP_HPP */
