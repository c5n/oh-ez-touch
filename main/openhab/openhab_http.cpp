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

ssize_t openhab_http_get(const char *url, void *buf, size_t buf_size, bool truncate)
{
    esp_http_client_config_t cfg = {};

    cfg.url = url;
    cfg.method = HTTP_METHOD_GET;
    cfg.timeout_ms = OPENHAB_HTTP_TIMEOUT_MS;

    esp_http_client_handle_t client = esp_http_client_init(&cfg);

    if (client == NULL)
    {
        ESP_LOGE(TAG, "GET %s: cannot create a client", url);
        return -1;
    }

    ssize_t retval = -1;

    /* open() + fetch_headers() + read_response() rather than perform(), because
     * the body has to end up in the caller's buffer and perform() would need an
     * event handler to get it there. */
    esp_err_t err = esp_http_client_open(client, 0);

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "GET %s: %s", url, esp_err_to_name(err));
        goto out;
    }

    /* Returns the Content-Length, or -1 for a chunked response -- which is not
     * an error and needs no special handling below. */
    if (esp_http_client_fetch_headers(client) < 0)
    {
        ESP_LOGE(TAG, "GET %s: no response headers", url);
        goto out;
    }

    {
        int status = esp_http_client_get_status_code(client);

        if (status != 200)
        {
            ESP_LOGE(TAG, "GET %s: HTTP %d", url, status);
            goto out;
        }

        int read = esp_http_client_read_response(client, (char *)buf, (int)buf_size);

        if (read < 0)
        {
            ESP_LOGE(TAG, "GET %s: read failed", url);
            goto out;
        }

        /* read_response() stops when the buffer is full, so a body that does
         * not fit looks like a complete short read; asking the client settles
         * it. */
        if (esp_http_client_is_complete_data_received(client) == false)
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
    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    return retval;
}

int openhab_http_post_text(const char *url, const char *body)
{
    esp_http_client_config_t cfg = {};

    cfg.url = url;
    cfg.method = HTTP_METHOD_POST;
    cfg.timeout_ms = OPENHAB_HTTP_TIMEOUT_MS;

    esp_http_client_handle_t client = esp_http_client_init(&cfg);

    if (client == NULL)
    {
        ESP_LOGE(TAG, "POST %s: cannot create a client", url);
        return -1;
    }

    int retval = -1;

    esp_http_client_set_header(client, "Content-Type", "text/plain");
    esp_http_client_set_post_field(client, body, (int)strlen(body));

    /* perform() sends the request and reads the whole response, discarding the
     * body -- which is all these two POSTs ever wanted. */
    esp_err_t err = esp_http_client_perform(client);

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "POST %s: %s", url, esp_err_to_name(err));
    }
    else
    {
        int status = esp_http_client_get_status_code(client);

        if (status >= 200 && status < 300)
            retval = 0;
        else
            ESP_LOGE(TAG, "POST %s: HTTP %d", url, status);
    }

    esp_http_client_cleanup(client);

    return retval;
}
