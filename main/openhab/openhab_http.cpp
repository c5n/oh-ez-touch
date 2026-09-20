/**
 * @file openhab_http.cpp
 *
 * See openhab_http.hpp.
 */

#include "openhab_http.hpp"

#include <string.h>

#include "esp_http_client.h"
#include "esp_log.h"

static const char *TAG = "openhab_http";

/* The one client handle, reused across requests.
 *
 * Two savings of quite different sizes. The small, certain one is the
 * init/cleanup pair: esp_http_client_init() allocates the handle, the request
 * and response structures and their buffers -- something over a kilobyte --
 * and it was doing that and freeing it again on every request. The larger one
 * is the TCP connection: esp_http_client_connect() skips the transport connect
 * when the handle is already past HTTP_STATE_CONNECTED, so a page's six icons
 * cost one handshake between them instead of six.
 *
 * Reusing the handle is only safe because only the openHAB client task calls
 * in here. That is a change of contract rather than of code, and it is why the
 * header no longer says anything about the task that draws.
 */
static esp_http_client_handle_t session;

/* Whether the last request left the connection open.
 *
 * esp_http_client has no public accessor for "am I connected", and this file
 * needs one: a request that fails on a connection it did not just open is the
 * one kind of failure a retry fixes. */
static bool session_connected;

/* The "scheme://host:port" the open connection goes to. */
static char session_origin[STR_AUTHORITY_LEN];

/* Everything before the path: "http://openhabian:8080".
 *
 * Two uses. It is what tells one server from another:
 * esp_http_client_set_url() closes the connection itself when it sees the host
 * or the port change, which is correct -- but it does not tell the caller, and
 * session_connected would then be a lie, so the comparison is made here too.
 * And the authority inside it is what the Host header has to carry.
 *
 * @return false for a URL with no scheme or no host, and for one that did not
 *   fit. Every URL this client is given is absolute -- the two the panel builds
 *   itself start with the configured "http://<host>:<port>", and the ones that
 *   come out of a sitemap page are absolute because openHAB writes them that
 *   way -- so anything else is a URL that could not be fetched anyway, and
 *   saying so beats requesting whatever esp_http_client makes of it. */
static bool url_origin(const char *url, char *out, size_t out_size)
{
    const char *scheme_end = strstr(url, "://");

    if (scheme_end == NULL)
        return false;

    const char *path = strchr(scheme_end + 3, '/');
    size_t len = (path != NULL) ? (size_t)(path - url) : strlen(url);

    /* A scheme and nothing else -- "http://" or "http:///rest/..." -- which is
     * what a configured hostname left blank builds into. There is no server in
     * it to name in a Host header, so it is refused with the rest. */
    if (len <= (size_t)(scheme_end + 3 - url))
        return false;

    if (len >= out_size)
        return false;

    memcpy(out, url, len);
    out[len] = '\0';

    return true;
}

/* The "host:port" inside an origin, which is what a Host header is. */
static const char *origin_authority(const char *origin)
{
    const char *scheme_end = strstr(origin, "://");

    return (scheme_end != NULL) ? scheme_end + 3 : origin;
}

static void session_disconnect(void)
{
    if (session == NULL)
        return;

    esp_http_client_close(session);
    session_connected = false;
}

/* Get the handle ready for one request, reconnecting if it has to.
 *
 * @return false only if there is no handle to be had at all. */
static bool session_prepare(const char *url, esp_http_client_method_t method)
{
    char origin[STR_AUTHORITY_LEN];

    /* Whatever the last response left behind, this one does not inherit it.
     *
     * esp_http_client caches a body that arrives in the same read as the
     * headers, and hands it to the next esp_http_client_read() from that cache
     * before it touches the socket. The cache belongs to the *handle*, not to
     * the request: esp_http_client_close() does not free it, and this file
     * keeps one handle for the life of the panel.
     *
     * So a body this file never reads stays on the handle and is served as the
     * beginning of the next response. It does not read one whenever the status
     * is not 200 -- and openHAB answers a missing icon with an 11 KB HTML error
     * page, which is both larger than the 5000 byte icon buffer and larger than
     * anything a short read can drain. The next sitemap page then parses that
     * HTML as its JSON, and the read after that trips
     * `assert(orig_raw_data == raw_data)` inside http_on_body() and aborts the
     * firmware. A panel pointed at a stock openHAB 5 -- whose icon sets are
     * SVG-only, so every icon 404s -- died within a second of its first fetch.
     *
     * Clearing it here rather than after the failure is deliberate: it holds
     * whether the last request bailed on a status, was truncated on purpose,
     * or succeeded, and it is the invariant the handle cannot provide itself. */
    if (session != NULL)
        esp_http_client_clear_response_buffer(session);

    if (url_origin(url, origin, sizeof(origin)) == false)
    {
        ESP_LOGE(TAG, "no server in the URL: %s", url);
        return false;
    }

    if (session == NULL)
    {
        esp_http_client_config_t cfg = {};

        cfg.url = url;
        cfg.method = method;
        cfg.timeout_ms = OPENHAB_HTTP_TIMEOUT_MS;

        session = esp_http_client_init(&cfg);

        if (session == NULL)
        {
            ESP_LOGE(TAG, "cannot create a client");
            return false;
        }

        session_connected = false;
    }
    else
    {
        if (strcmp(origin, session_origin) != 0)
            session_disconnect();

        if (esp_http_client_set_url(session, url) != ESP_OK)
        {
            ESP_LOGE(TAG, "cannot set the URL: %s", url);
            session_disconnect();
            return false;
        }
    }

    strlcpy(session_origin, origin, sizeof(session_origin));

    /* Host, rebuilt from this request's own URL.
     *
     * esp_http_client sets this header once, from the URL handed to
     * esp_http_client_init(), and esp_http_client_set_url() touches it again
     * only when the host *name* changes -- and then sets it to the bare host,
     * dropping the port. So a handle that outlives a change of server, which
     * this one is built to do, goes on announcing an authority the URL no
     * longer agrees with.
     *
     * It matters more than a header usually does, because openHAB builds the
     * "link" and "linkedPage" URLs of a sitemap page out of the authority the
     * request announced. The panel then polls item states at "<link>/state",
     * follows sub-page links and posts commands to exactly those URLs -- so a
     * Host header without the port comes back as a page full of portless
     * links, and every one of those three fails while the two URLs the panel
     * builds for itself, the sitemap page and the icons, keep working.
     *
     * HTTPClient wrote this header from the URL on every request, which is why
     * none of it was visible before; so does this. */
    if (esp_http_client_set_header(session, "Host", origin_authority(origin)) != ESP_OK)
    {
        ESP_LOGE(TAG, "cannot set the Host header: %s", origin);
        session_disconnect();
        return false;
    }

    esp_http_client_set_method(session, method);

    return true;
}

/* Whether the connection may be left open for the next request.
 *
 * Two conditions, each guarding against a different way of corrupting the next
 * response. A body that was not read to the end leaves bytes in the socket
 * that the next response would parse as its status line. And a server that
 * answered "Connection: close" is going to hang up whatever this thinks.
 *
 * esp_http_client_perform() makes exactly this second check for itself, at a
 * point in its state machine that the open()/fetch_headers()/read_response()
 * path below never reaches -- which is why it has to be made here by hand. */
static bool session_may_persist(void)
{
    if (esp_http_client_is_complete_data_received(session) == false)
        return false;

    return esp_http_client_is_persistent_connection(session);
}

static ssize_t http_get_attempt(const char *url, void *buf, size_t buf_size, bool truncate)
{
    if (session_prepare(url, HTTP_METHOD_GET) == false)
        return -1;

    /* A POST leaves its body on the handle, and the handle outlives the
     * request now. Without this the next GET would send the last command's
     * payload along with it. */
    esp_http_client_set_post_field(session, NULL, 0);

    ssize_t retval = -1;

    /* open() + fetch_headers() + read_response() rather than perform(), because
     * the body has to end up in the caller's buffer and perform() would need an
     * event handler to get it there. */
    esp_err_t err = esp_http_client_open(session, 0);

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "GET %s: %s", url, esp_err_to_name(err));
        goto out;
    }

    /* Returns the Content-Length, or 0 for a chunked response -- which is not
     * an error, and needs no special handling below because
     * esp_http_client_read_response() reads a chunked body to its end without
     * being told how long it is. */
    if (esp_http_client_fetch_headers(session) < 0)
    {
        ESP_LOGE(TAG, "GET %s: no response headers", url);
        goto out;
    }

    {
        int status = esp_http_client_get_status_code(session);

        if (status != 200)
        {
            ESP_LOGE(TAG, "GET %s: HTTP %d", url, status);
            goto out;
        }

        int read = esp_http_client_read_response(session, (char *)buf, (int)buf_size);

        if (read < 0)
        {
            ESP_LOGE(TAG, "GET %s: read failed", url);
            goto out;
        }

        /* read_response() stops when the buffer is full, so a body that does
         * not fit looks like a complete short read; asking the client settles
         * it. */
        if (esp_http_client_is_complete_data_received(session) == false)
        {
            if (truncate == false)
            {
                ESP_LOGE(TAG, "GET %s: body larger than the %u byte buffer",
                         url, (unsigned)buf_size);
                goto out;
            }

            ESP_LOGD(TAG, "GET %s: body truncated to %u bytes",
                     url, (unsigned)buf_size);
        }

        retval = read;
    }

out:
    /* A failed request always hangs up: whatever went wrong, the socket's
     * state is no longer something the next request should inherit. */
    if (retval < 0 || session_may_persist() == false)
        session_disconnect();
    else
        session_connected = true;

    return retval;
}

ssize_t openhab_http_get(const char *url, void *buf, size_t buf_size, bool truncate)
{
    /* A connection that was already open can be one the server has since hung
     * up, and it does not say so: the request is written into the local send
     * buffer and succeeds, and the failure only appears on the read.
     * perform() never has this problem, because it re-checks keep-alive after
     * every response; the manual path cannot, so a failure on a connection
     * this did not just open is given one more go on a fresh one.
     *
     * Once, and only when the connection was reused. A request that failed on
     * a connection opened for it has failed for a reason that trying again
     * immediately will not change -- and doubling a five second timeout for an
     * unreachable server is exactly what the panel does not need. */
    bool reused = session_connected;
    ssize_t read = http_get_attempt(url, buf, buf_size, truncate);

    if (read >= 0 || reused == false)
        return read;

    ESP_LOGD(TAG, "GET %s: retrying on a new connection", url);

    return http_get_attempt(url, buf, buf_size, truncate);
}

/* How a POST attempt ended, which is not the same question as whether it
 * succeeded: only a transport failure is worth another go, and the two used
 * to be the same -1. */
enum post_result_e
{
    POST_OK = 0,
    POST_TRANSPORT_FAILED, /* nothing was answered; a retry may work */
    POST_REFUSED,          /* the server answered, and said no       */
};

static enum post_result_e http_post_attempt(const char *url, const char *body)
{
    if (session_prepare(url, HTTP_METHOD_POST) == false)
        return POST_REFUSED;

    esp_http_client_set_header(session, "Content-Type", "text/plain");
    esp_http_client_set_post_field(session, body, (int)strlen(body));

    /* perform() resumes the handle's state machine; the GET above does not
     * leave it anywhere perform() can resume from.
     *
     * esp_http_client_open() sends unconditionally -- connect, then write the
     * request line -- and never looks at client->state, which is why the GET
     * path works on a handle in any state at all. perform() is the opposite: it
     * switches on client->state and only sends a request from INIT, CONNECTING
     * or CONNECTED. A finished manual GET leaves the handle in
     * RES_ON_DATA_START, and that arm of the switch *reads a response*. So
     * perform() drained an already-drained body, dispatched ON_FINISH and
     * returned ESP_OK without a byte on the wire -- and
     * esp_http_client_get_status_code() then answered with the previous GET's
     * 200, so the command was reported as delivered.
     *
     * Every command after the first successful poll went this way: tapping a
     * switch toggled the tile, logged nothing, sent nothing, and let the next
     * poll quietly put the tile back. Only a command that happened to follow a
     * *failed* GET worked, because that path closes the connection and leaves
     * the handle at INIT -- which is why icons 404ing made switches work.
     *
     * Closing first is what puts it at INIT deliberately. It costs this request
     * a TCP handshake; a command is a keypress, and there is no public API to
     * hand the handle back to perform() in any other state. */
    session_disconnect();

    esp_err_t err = esp_http_client_perform(session);

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "POST %s: %s", url, esp_err_to_name(err));
        session_disconnect();
        return POST_TRANSPORT_FAILED;
    }

    int status = esp_http_client_get_status_code(session);

    session_connected = esp_http_client_is_persistent_connection(session);

    if (status >= 200 && status < 300)
        return POST_OK;

    ESP_LOGE(TAG, "POST %s: HTTP %d", url, status);

    return POST_REFUSED;
}

int openhab_http_post_text(const char *url, const char *body)
{
    /* No retry, because there is no longer anything a second attempt could
     * fix. The retry existed for one failure the GET path still has: a
     * keep-alive connection the server has since hung up accepts the request
     * into the send buffer and only fails on the read, and opening a fresh one
     * gets the command through. http_post_attempt() now always opens a fresh
     * one -- it has to, to give perform() a state machine it can start from --
     * so a POST that failed has failed on a connection made for it, and
     * trying again immediately would only spend a second timeout on a server
     * that is not there.
     *
     * A refusal was never worth retrying either: openHAB answering 404 for a
     * renamed item, or 400 for a command the item does not accept, answers the
     * same way the second time. */
    return (http_post_attempt(url, body) == POST_OK) ? 0 : -1;
}
