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
 * The multipart body is scanned rather than handed to the server's form
 * parser, for the same reason the transport has a streaming route at all: it
 * is a megabyte, and neither target has a megabyte to buffer it in. The
 * scanner itself is multipart.c, which knows nothing about firmware; what is
 * left here is the OTA partition it writes into and the restart afterwards.
 * That split is what makes the boundary arithmetic reachable from
 * test/host -- it is the one code path in this firmware that can leave a
 * panel unbootable, and the only one that parses bytes a stranger chose.
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

#include "multipart.h"
#include "port/port_sys.h"
#include "webui_transport.h"

static const char *TAG = "webui_ota";

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

/* What the multipart scanner writes into. The scanning itself is in
 * multipart.c, which knows nothing about firmware -- that split is what makes
 * the part with the boundary arithmetic in it reachable from test/host. */
struct upload_s
{
    esp_ota_handle_t       ota;
    const esp_partition_t *partition;
    size_t                 written;
};

static bool ota_begin(void *ctx)
{
    struct upload_s *up = (struct upload_s *)ctx;

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

static bool ota_write(void *ctx, const char *data, size_t len)
{
    struct upload_s *up = (struct upload_s *)ctx;

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

/* How many reads in a row may come back empty before the upload is written
 * off.
 *
 * The esp32 transport maps a socket timeout to 0, and the loop below cannot
 * treat that as the end of the body -- a slow client is normal on a panel
 * sharing its radio with a scan. But it cannot ignore it either: `remaining`
 * does not move, so a client that sends a Content-Length and then stops
 * talking used to hold the server's task in that loop for good, with the OTA
 * handle open and the partition half written. Each empty read is a full
 * socket timeout, so a handful of them is already a long wait. */
#define UPLOAD_MAX_IDLE_READS 8

void webui_ota_handle_upload(webui_request_t *req)
{
    /* Static, not on the stack: together they are around 200 bytes and the
     * handler task has 4 K. esp_http_server serves one request at a time, so
     * there is never a second upload to collide with. */
    static struct upload_s    up;
    static struct multipart_s mp;

    char     chunk[UPLOAD_CHUNK];
    size_t   remaining = webui_body_length(req);
    unsigned idle = 0;

    memset(&up, 0, sizeof(up));
    multipart_init(&mp, ota_begin, ota_write, &up);

    ESP_LOGI(TAG, "upload of %u bytes starting", (unsigned)remaining);

    while (remaining > 0)
    {
        int n = webui_body_read(req, chunk, sizeof(chunk));

        if (n < 0)
        {
            ESP_LOGE(TAG, "connection lost after %u bytes", (unsigned)up.written);
            break;
        }

        if (n == 0)
        {
            if (++idle >= UPLOAD_MAX_IDLE_READS)
            {
                ESP_LOGE(TAG, "upload stalled after %u bytes, %u still expected",
                         (unsigned)up.written, (unsigned)remaining);
                break;
            }

            continue;
        }

        idle = 0;

        /* Never below zero: a transport that answered with more than was
         * asked for would otherwise wrap this and run the loop for four
         * billion more bytes. */
        remaining -= ((size_t)n < remaining) ? (size_t)n : remaining;

        multipart_feed_block(&mp, chunk, (size_t)n);

        if (multipart_failed(&mp) == true)
            break;
    }

    if (multipart_complete(&mp) == true && up.written > 0)
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
