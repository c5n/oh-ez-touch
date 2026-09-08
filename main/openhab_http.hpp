/**
 * @file openhab_http.hpp
 *
 * The two HTTP requests this firmware makes, over esp_http_client.
 *
 * Everything it asks of openHAB is either "GET this and give me the body" or
 * "POST this string and tell me it worked", in four places: an item's state, a
 * sitemap page, a widget icon, and a sensor reading. So the transport lives
 * here once rather than four times, and -- this is the point of moving off
 * HTTPClient -- it is the *same* transport on both targets. esp_http_client
 * builds for the linux target against the host's own socket API, so the
 * simulator makes real requests to a real openHAB instead of reading canned
 * fixtures.
 *
 * Both calls block on the task that makes them, which today is the one task
 * that also drives LVGL: a slow or unreachable openHAB stalls the UI for up to
 * the timeout below. That is not new -- HTTPClient did exactly the same on the
 * loop task -- but it is worth fixing by giving the openHAB client a task and
 * a queue of its own, which is a change to how the UI is structured rather
 * than to how it talks.
 */
#ifndef OPENHAB_HTTP_HPP
#define OPENHAB_HTTP_HPP

#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>

/* Matches what HTTPClient defaulted to, so an unresponsive server stalls the
 * UI for no longer than it used to. */
#define OPENHAB_HTTP_TIMEOUT_MS 5000

/**
 * GET `url` and copy the body into `buf`.
 *
 * The body is the *decoded* body: esp_http_client undoes chunked transfer
 * encoding, so callers get bytes and not framing. Item::getIcon() used to have
 * to strip that framing by hand.
 *
 * @param truncate what a body larger than `buf_size` means. A plain-text item
 *   state passes true: it is copied into a fixed-width field either way, and
 *   truncating it is what HTTPClient::getString() plus strlcpy() did. JSON and
 *   PNG pass false, because half of either is worse than none -- it would fail
 *   to parse or decode somewhere far from the cause.
 * @return bytes copied, or -1 on a transport error, a status other than 200, or
 *         an over-long body with `truncate` false. Failures are logged here
 *         with the method, URL and status; callers add their own context.
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

#endif /* OPENHAB_HTTP_HPP */
