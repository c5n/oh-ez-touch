/**
 * @file webui_minimal.cpp
 *
 * The minimal recovery firmware's web interface.
 *
 * Three things and nothing else: a status page that tools/batchupdate.py
 * can verify (it parses the Version and Target table cells, so their markup
 * is the same as the full firmware's), the /api/status JSON the fleet
 * manager probes (version, target and mac are the fields it insists on),
 * and the /update upload from webui_ota.cpp -- whose block-wise multipart
 * scan is the reason this firmware exists: it is the fast path the full
 * image then comes up on.
 */
#include "webui_minimal.hpp"

#include <stdio.h>

#include "esp_log.h"
#include "esp_wifi.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "port/port_sys.h"
#include "version.h"
#include "webui_ota.hpp"
#include "webui_transport.h"

/* Set by the top-level CMakeLists.txt, "Minimal" for this build. */
#ifndef TARGET_NAME
#define TARGET_NAME "unknown"
#endif

/* Same default as the full firmware's webui.cpp. */
#ifndef WEBUI_PORT
#define WEBUI_PORT 80
#endif

static const char *TAG = "webui_minimal";

/* The station MAC, as "aa:bb:cc:dd:ee:ff". The radio is up by the time any
 * request arrives, so esp_wifi_get_mac cannot fail here; all-zero is the
 * honest answer if it ever does. */
static void sta_mac(char out[18])
{
    unsigned char mac[6] = {0};

    esp_wifi_get_mac(WIFI_IF_STA, mac);

    snprintf(out, 18, "%02x:%02x:%02x:%02x:%02x:%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

static void handle_root(webui_request_t *req)
{
    char page[512];

    snprintf(page, sizeof(page),
             "<!DOCTYPE html><html><head><meta charset='utf-8'>"
             "<meta name='viewport' content='width=device-width,initial-scale=1'>"
             "<title>OhEzTouch recovery</title></head><body>"
             "<h1>OhEzTouch recovery firmware</h1>"
             "<table>"
             "<tr><td>Version</td><td>%u.%02u</td></tr>"
             "<tr><td>Target</td><td>%s</td></tr>"
             "</table>"
             "<p><a href='/update'>Firmware update</a></p>"
             "<form method='post' action='/restart'>"
             "<button type='submit'>Restart</button></form>"
             "</body></html>",
             (unsigned)VERSION_MAJOR, (unsigned)VERSION_MINOR, TARGET_NAME);

    webui_send(req, 200, "text/html", page);
}

static void handle_status(webui_request_t *req)
{
    char mac[18];
    char json[160];

    sta_mac(mac);

    snprintf(json, sizeof(json),
             "{\"version\":\"%u.%02u\",\"target\":\"%s\",\"mac\":\"%s\"}",
             (unsigned)VERSION_MAJOR, (unsigned)VERSION_MINOR, TARGET_NAME,
             mac);

    webui_send(req, 200, "application/json", json);
}

static void handle_restart(webui_request_t *req)
{
    webui_send(req, 200, "text/plain", "restarting\n");

    /* The response has to reach the client first, as in webui_ota.cpp. */
    vTaskDelay(pdMS_TO_TICKS(500));
    port_restart();
}

static void handle_not_found(webui_request_t *req)
{
    webui_redirect(req, 302, "/");
}

void webui_minimal_setup(void)
{
    webui_transport_route("/", WEBUI_GET, handle_root);
    webui_transport_route("/api/status", WEBUI_GET, handle_status);
    webui_transport_route("/restart", WEBUI_POST, handle_restart);
    webui_transport_route("/restart", WEBUI_GET, handle_restart);

    /* The path and the method are what tools/batchupdate.py knows. */
    webui_transport_route("/update", WEBUI_GET, webui_ota_handle_form);
    webui_transport_route_stream("/update", webui_ota_handle_upload);

    webui_transport_route_default(handle_not_found);

    if (webui_transport_start(WEBUI_PORT) == false)
        ESP_LOGE(TAG, "web server did not start; OTA is not reachable");
}
