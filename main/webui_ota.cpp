/**
 * @file webui_ota.cpp
 *
 * The firmware upload, replacing AutoConnect's HTTPUpdateServer.
 *
 * The route and the request shape are unchanged, because tools/batchupdate.sh
 * knows them: a multipart POST to /update, whose field name is ignored (the
 * script calls it "name", the browser form below calls it "image", and neither
 * matters).
 *
 * The multipart body is scanned here rather than by the server, for the same
 * reason the transport has a streaming route at all: it is a megabyte, and
 * neither target has a megabyte to buffer it in. The scanner is small because
 * the shape it has to handle is small -- one part, and the boundary is the
 * first line of the body, so it does not even have to be parsed out of the
 * Content-Type header.
 *
 * Unauthenticated, as it has always been. Worth knowing before exposing one of
 * these outside a home network -- and worth knowing that the setup access
 * point, which is open, reaches it too.
 */

#include "sdkconfig.h"

#include "webui_ota.hpp"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "port/port_sys.h"
#include "webui_transport.h"

static const char *TAG = "webui_ota";

/* Long enough for any boundary a client will generate (RFC 2046 caps them at
 * 70 characters) plus the two leading dashes and the CRLF. */
#define BOUNDARY_MAX 80

/* Read in pieces this size. Large enough that the flash writes are efficient,
 * small enough to sit on a task stack alongside everything else. */
#define UPLOAD_CHUNK 1024

static const char upload_form[] =
    "<!DOCTYPE html><html><head><meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>OhEzTouch firmware</title></head><body>"
    "<h1>Firmware update</h1>"
    "<form method='post' action='/update' enctype='multipart/form-data'>"
    "<input type='file' name='image' accept='.bin'>"
    "<button type='submit'>Upload</button></form>"
    "<p>The panel restarts by itself when the upload succeeds.</p>"
    "</body></html>";

void webui_ota_handle_form(webui_request_t *req)
{
    webui_send(req, 200, "text/html", upload_form);
}

#if !CONFIG_IDF_TARGET_LINUX

#include "esp_ota_ops.h"

/* Where the multipart scanner is in the body. */
enum scan_state_e
{
    SCAN_BOUNDARY,  /* still collecting the first line                     */
    SCAN_HEADERS,   /* the part's own headers, up to a blank line          */
    SCAN_DATA,      /* the firmware, until the boundary comes round again  */
    SCAN_DONE,
};

struct upload_s
{
    enum scan_state_e state;
    char              boundary[BOUNDARY_MAX];
    size_t            boundary_len;
    char              head[BOUNDARY_MAX + 8]; /* the line being collected  */
    size_t            head_len;

    /* The last boundary_len + 2 bytes seen are held back rather than written:
     * they may turn out to be the start of the terminating boundary, and a
     * firmware image with those bytes appended does not verify. */
    char   tail[BOUNDARY_MAX + 4];
    size_t tail_len;

    esp_ota_handle_t ota;
    const esp_partition_t *partition;
    size_t written;
    bool   failed;
};

static bool ota_begin(struct upload_s *up)
{
    up->partition = esp_ota_get_next_update_partition(NULL);

    if (up->partition == NULL)
    {
        ESP_LOGE(TAG, "no OTA partition to write to");
        return false;
    }

    esp_err_t err = esp_ota_begin(up->partition, OTA_WITH_SEQUENTIAL_WRITES, &up->ota);

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "esp_ota_begin: %s", esp_err_to_name(err));
        return false;
    }

    ESP_LOGI(TAG, "writing to partition '%s'", up->partition->label);

    return true;
}

static bool ota_write(struct upload_s *up, const char *data, size_t len)
{
    if (len == 0)
        return true;

    esp_err_t err = esp_ota_write(up->ota, data, len);

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "esp_ota_write at %u bytes: %s", (unsigned)up->written,
                 esp_err_to_name(err));
        return false;
    }

    up->written += len;

    return true;
}

/* Give the scanner one more byte of the body. */
static void feed(struct upload_s *up, char c)
{
    switch (up->state)
    {
    case SCAN_BOUNDARY:
        /* The first line is "--BOUNDARY". Keeping it verbatim means the
         * Content-Type header never has to be read. */
        if (c == '\r')
            break;

        if (c == '\n')
        {
            up->head[up->head_len] = '\0';
            up->boundary_len = up->head_len;
            memcpy(up->boundary, up->head, up->head_len + 1);
            up->head_len = 0;
            up->state = SCAN_HEADERS;
            break;
        }

        if (up->head_len + 1 < sizeof(up->head))
            up->head[up->head_len++] = c;
        break;

    case SCAN_HEADERS:
        /* The part's own headers, ended by a blank line. Nothing in them is
         * needed: the field name is ignored and so is the filename. */
        if (c == '\r')
            break;

        if (c == '\n')
        {
            if (up->head_len == 0)
            {
                up->state = SCAN_DATA;

                if (ota_begin(up) == false)
                {
                    up->failed = true;
                    up->state = SCAN_DONE;
                }
            }

            up->head_len = 0;
            break;
        }

        if (up->head_len + 1 < sizeof(up->head))
            up->head[up->head_len++] = c;
        break;

    case SCAN_DATA:
    {
        /* Hold back as much as the terminator could be: CRLF plus the
         * boundary. Anything older than that cannot be part of it, so it is
         * safe to write. */
        size_t hold = up->boundary_len + 2;

        up->tail[up->tail_len++] = c;

        if (up->tail_len > hold)
        {
            size_t spill = up->tail_len - hold;

            if (ota_write(up, up->tail, spill) == false)
            {
                up->failed = true;
                up->state = SCAN_DONE;
                break;
            }

            memmove(up->tail, up->tail + spill, hold);
            up->tail_len = hold;
        }

        /* "\r\n--BOUNDARY" means the image has ended; what is held back is
         * exactly that and is discarded. */
        if (   up->tail_len == hold
            && up->tail[0] == '\r' && up->tail[1] == '\n'
            && memcmp(up->tail + 2, up->boundary, up->boundary_len) == 0)
        {
            up->tail_len = 0;
            up->state = SCAN_DONE;
        }
        break;
    }

    case SCAN_DONE:
    default:
        break;
    }
}

void webui_ota_handle_upload(webui_request_t *req)
{
    static struct upload_s up; /* 200 bytes, and the handler task's stack is 4 K */
    char                   chunk[UPLOAD_CHUNK];
    size_t                 remaining = webui_body_length(req);

    memset(&up, 0, sizeof(up));

    ESP_LOGI(TAG, "upload of %u bytes starting", (unsigned)remaining);

    while (remaining > 0)
    {
        int n = webui_body_read(req, chunk, sizeof(chunk));

        if (n < 0)
        {
            ESP_LOGE(TAG, "connection lost after %u bytes", (unsigned)up.written);
            up.failed = true;
            break;
        }

        if (n == 0)
            continue;

        remaining -= (size_t)n;

        for (int i = 0; i < n && up.failed == false; i++)
            feed(&up, chunk[i]);
    }

    if (up.failed == false && up.state == SCAN_DONE && up.written > 0)
    {
        esp_err_t err = esp_ota_end(up.ota);

        if (err == ESP_OK)
            err = esp_ota_set_boot_partition(up.partition);

        if (err == ESP_OK)
        {
            ESP_LOGI(TAG, "%u bytes written, restarting", (unsigned)up.written);

            webui_send(req, 200, "text/plain", "OK\n");

            /* The response has to reach the client first: batchupdate.sh reads
             * curl's exit status, and a reset connection is a failure to it
             * even though the update succeeded. */
            vTaskDelay(pdMS_TO_TICKS(500));
            port_restart();
            return;
        }

        ESP_LOGE(TAG, "finishing the update: %s", esp_err_to_name(err));
    }
    else if (up.ota != 0)
    {
        /* Truncated or corrupt: abort so the partition is not left half
         * written with a boot flag pointing at it. */
        esp_ota_abort(up.ota);
    }

    ESP_LOGE(TAG, "update failed after %u bytes", (unsigned)up.written);

    webui_send(req, 500, "text/plain", "update failed\n");
}

#else /* CONFIG_IDF_TARGET_LINUX */

void webui_ota_handle_upload(webui_request_t *req)
{
    /* Nothing to write to and nothing to reboot into. Reading the body anyway,
     * so that a client uploading to the simulator gets an answer rather than a
     * reset connection -- and so that the request path itself is exercised
     * here, which is the half that is worth testing on a desktop. */
    char   chunk[UPLOAD_CHUNK];
    size_t remaining = webui_body_length(req);
    size_t got = 0;

    while (remaining > 0)
    {
        int n = webui_body_read(req, chunk, sizeof(chunk));

        if (n <= 0)
            break;

        remaining -= (size_t)n;
        got += (size_t)n;
    }

    ESP_LOGI(TAG, "discarded a %u byte upload: no OTA on this target", (unsigned)got);

    webui_send(req, 501, "text/plain", "no firmware partition on this target\n");
}

#endif /* CONFIG_IDF_TARGET_LINUX */
