/**
 * @file esp32/port_ntp.c
 *
 * SNTP plus the timezone, which is what Arduino's configTime() rolled into one
 * call. Split here because they fail differently: the timezone always applies,
 * while the synchronisation needs a network and may never happen.
 *
 * port_localtime() reports the difference -- it stays false until the year
 * looks plausible, so openhab_ui keeps the theme variant it has rather than
 * jumping into the night window because the clock still reads 1970.
 */
#include "port_ntp.h"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "esp_sntp.h"

static const char *TAG = "port_ntp";

static bool sntp_started;

void port_ntp_setup(const char *server, int gmt_offset_s, int dst_offset_s)
{
    /* POSIX TZ counts west of UTC, the opposite way round from the setting.
     * With no rule after it the DST offset is simply always in effect, which is
     * exactly what configTime()'s two-offset form meant. */
    int west_s = -(gmt_offset_s + dst_offset_s);
    char tz[32];

    snprintf(tz, sizeof(tz), "OHEZ%+d:%02d:%02d",
             west_s / 3600, abs((west_s / 60) % 60), abs(west_s % 60));

    setenv("TZ", tz, 1);
    tzset();

    if (server == NULL || server[0] == '\0')
    {
        ESP_LOGW(TAG, "no NTP server configured; the clock will not be set");
        return;
    }

    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG(server);

    /* Neither blocks: this runs on the task that also drives LVGL, and the
     * openHAB loop re-applies the settings periodically anyway. */
    config.start = true;
    config.sync_cb = NULL;

    if (sntp_started == true)
    {
        /* Called again after a settings change. The server list is part of the
         * initialisation, so it has to be torn down to change it. */
        esp_netif_sntp_deinit();
        sntp_started = false;
    }

    esp_err_t err = esp_netif_sntp_init(&config);

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "esp_netif_sntp_init(%s): %s", server, esp_err_to_name(err));
        return;
    }

    sntp_started = true;

    ESP_LOGI(TAG, "SNTP from %s, timezone %s", server, tz);
}
