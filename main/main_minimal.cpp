/**
 * @file main_minimal.cpp
 *
 * The minimal OTA recovery firmware (CONFIG_OHEZ_MINIMAL), one image for
 * every board.
 *
 * What it is for: a device whose old firmware still scans the upload a byte
 * at a time -- one flash write per byte -- takes the better part of an hour
 * for the full image. This firmware is roughly a third of that size and so
 * costs a third of the wait, and once it runs, its block-wise multipart
 * scan takes the full image at the speed the radio allows.
 *
 * What it does is correspondingly small: mount the config store (for the
 * hostname), bring up WLAN -- with the stored credentials, or the setup
 * access point when there are none -- and serve the status page, the REST
 * status, and /update. No display, no touch, no openHAB, no MQTT, no
 * sensors, no sound. Nothing board-specific is driven, which is why one
 * image fits all boards; the board Kconfig choice is irrelevant to this
 * build.
 */
#include "sdkconfig.h"

#include "esp_event.h"
#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "config/config.hpp"
#include "net/wlan.hpp"
#include "port/port_kv.h"
#include "version.h"
#include "web/webui_minimal.hpp"

#ifndef TARGET_NAME
#define TARGET_NAME "unknown"
#endif

/* Same name the full firmware uses; SPIFFS on the device. */
#define OHEZ_CONFIG_FILE "config.json"

static const char *TAG = "ohez";

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "OhEzTouch %u.%02u recovery firmware (%s) on %s",
             (unsigned)VERSION_MAJOR, (unsigned)VERSION_MINOR,
             VERSION_GIT_HASH, TARGET_NAME);

    /* esp_wifi and esp_netif publish here; same reason as in main.cpp. */
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* The WLAN credentials live in NVS; without this the device raises its
     * setup access point on every boot even when provisioned. */
    esp_err_t kv = port_kv_init();

    if (kv != ESP_OK)
        ESP_LOGE(TAG, "no credential store: %s", esp_err_to_name(kv));

    /* The config store is mounted and read for one field: the hostname. It
     * is the name the DHCP client registers, and the fleet list addresses
     * devices by it -- a recovery firmware that comes up under the built-in
     * default while the panel is really called something else cannot be
     * found for the full-image upload that is the whole point of this
     * build. Everything else in the file is ignored here. */
    static Config config;

    if (config.setup() == false)
        ESP_LOGW(TAG, "no config store; hostname falls back to the default");

    config.loadConfig(OHEZ_CONFIG_FILE);

    wlan_setup(&config);
    webui_minimal_setup();

    for (;;)
    {
        wlan_loop();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
