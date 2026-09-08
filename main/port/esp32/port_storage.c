/**
 * @file esp32/port_storage.c
 *
 * The config store on the device: the `spiffs` partition of partitions.csv,
 * whose contents come from data/ via spiffs_create_partition_image() in the
 * top-level CMakeLists.txt.
 */

#include "port_storage.h"
#include "port_internal.h"

#include <stdio.h>

#include "esp_log.h"
#include "esp_spiffs.h"

static const char *TAG = "port_storage";

#define PORT_STORAGE_MOUNT "/spiffs"

esp_err_t port_storage_init(void)
{
    esp_vfs_spiffs_conf_t conf = {
        .base_path              = PORT_STORAGE_MOUNT,
        .partition_label        = "spiffs",
        .max_files              = 2,
        /* False on purpose. The image is built and flashed with the firmware,
         * so an unmountable partition means a flash that did not take -- and
         * formatting it would replace that diagnosis with a device that has
         * quietly lost its configuration. */
        .format_if_mount_failed = false,
    };

    esp_err_t err = esp_vfs_spiffs_register(&conf);

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "cannot mount the spiffs partition: %s", esp_err_to_name(err));
        return err;
    }

    size_t total = 0;
    size_t used  = 0;

    if (esp_spiffs_info(conf.partition_label, &total, &used) == ESP_OK)
        ESP_LOGI(TAG, "mounted %s, %u of %u bytes used", PORT_STORAGE_MOUNT,
                 (unsigned)used, (unsigned)total);

    return ESP_OK;
}

esp_err_t port_storage_path(const char *name, char *buf, size_t buf_size)
{
    int n = snprintf(buf, buf_size, "%s/%s", PORT_STORAGE_MOUNT, name);

    if (n < 0 || (size_t)n >= buf_size)
        return ESP_ERR_INVALID_SIZE;

    return ESP_OK;
}
