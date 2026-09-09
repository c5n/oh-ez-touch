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
static char session_authority[STR_AUTHORITY_LEN];

/* Everything before the path: "http://openhabian:8080".
 *
 * esp_http_client_set_url() closes the connection itself when it sees the host
 * or the port change, which is correct -- but it does not tell the caller, and
 * session_connected would then be a lie. So the comparison is made here too. */
static void url_authority(const char *url, char *out, size_t out_size)
{
    const char *scheme_end = strstr(url, "://");
    size_t len;

    if (scheme_end == NULL)
    {
        strlcpy(out, url, out_size);
        return;
    }

    const char *path = strchr(scheme_end + 3, '/');

    len = (path != NULL) ? (size_t)(path - url) : strlen(url);

    if (len >= out_size)
        len = out_size - 1;

    memcpy(out, url, len);
    out[len] = '\0';
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
    char authority[STR_AUTHORITY_LEN];

    url_authority(url, authority, sizeof(authority));

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
        strlcpy(session_authority, authority, sizeof(session_authority));

        return true;
    }

    if (strcmp(authority, session_authority) != 0)
    {
        session_disconnect();
        strlcpy(session_authority, authority, sizeof(session_authority));
    }

    if (esp_http_client_set_url(session, url) != ESP_OK)
    {
        ESP_LOGE(TAG, "cannot set the URL: %s", url);
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

    /* Returns the Content-Length, or -1 for a chunked response -- which is not
     * an error and needs no special handling below. */
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

static int http_post_attempt(const char *url, const char *body)
{
    if (session_prepare(url, HTTP_METHOD_POST) == false)
        return -1;

    int retval = -1;

    esp_http_client_set_header(session, "Content-Type", "text/plain");
    esp_http_client_set_post_field(session, body, (int)strlen(body));

    /* perform() sends the request and reads the whole response, discarding the
     * body -- which is all these two POSTs ever wanted. Unlike the GET above it
     * needs no help with the connection: perform() checks keep-alive itself and
     * closes when the server asked for it. */
    esp_err_t err = esp_http_client_perform(session);

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "POST %s: %s", url, esp_err_to_name(err));
        session_disconnect();
        return -1;
    }

    int status = esp_http_client_get_status_code(session);

    if (status >= 200 && status < 300)
        retval = 0;
    else
        ESP_LOGE(TAG, "POST %s: HTTP %d", url, status);

    session_connected = esp_http_client_is_persistent_connection(session);

    return retval;
}

int openhab_http_post_text(const char *url, const char *body)
{
    bool reused = session_connected;
    int retval = http_post_attempt(url, body);

    if (retval == 0 || reused == false)
        return retval;

    ESP_LOGD(TAG, "POST %s: retrying on a new connection", url);

    return http_post_attempt(url, body);
}
