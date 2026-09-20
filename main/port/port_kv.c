/**
 * @file port_kv.c
 *
 * port_kv on NVS. Shared by both targets -- NVS runs on the host too, on the
 * emulated flash that port_flash_init() sets up -- so only that one hook and
 * the erase-and-retry policy below are target business.
 */

#include "port_kv.h"
#include "port_internal.h"

#include <string.h>

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "port_kv";

esp_err_t port_kv_init(void)
{
    esp_err_t err = port_flash_init();

    if (err != ESP_OK)
        return err;

    err = nvs_flash_init();

    /* A partition that was never initialised, or that a newer NVS format has
     * been written to, is recoverable exactly once: erase it and start over.
     * Losing the WLAN credentials is bad, but so is a device that cannot come
     * up at all, and the soft-AP provisioning path exists to recover from it. */
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_LOGW(TAG, "NVS unusable (%s); erasing it", esp_err_to_name(err));

        err = nvs_flash_erase();

        if (err == ESP_OK)
            err = nvs_flash_init();
    }

    if (err != ESP_OK)
        ESP_LOGE(TAG, "nvs_flash_init: %s", esp_err_to_name(err));

    return err;
}

esp_err_t port_kv_get_str(const char *ns, const char *key, char *buf, size_t buf_size)
{
    nvs_handle_t handle;

    if (buf_size == 0)
        return ESP_ERR_INVALID_ARG;

    buf[0] = '\0';

    /* Opening read-only fails when the namespace has never been written, which
     * is simply a store nothing has been saved to yet. Report it as the
     * missing-key case so callers need only one check. */
    esp_err_t err = nvs_open(ns, NVS_READONLY, &handle);

    if (err != ESP_OK)
        return (err == ESP_ERR_NVS_NOT_FOUND) ? ESP_ERR_NVS_NOT_FOUND : err;

    size_t len = buf_size;

    err = nvs_get_str(handle, key, buf, &len);
    nvs_close(handle);

    /* nvs_get_str() leaves the buffer untouched when the value does not fit,
     * so do not hand the caller whatever was in it. */
    if (err != ESP_OK)
        buf[0] = '\0';

    return err;
}

esp_err_t port_kv_set_str(const char *ns, const char *key, const char *value)
{
    nvs_handle_t handle;

    esp_err_t err = nvs_open(ns, NVS_READWRITE, &handle);

    if (err != ESP_OK)
        return err;

    err = nvs_set_str(handle, key, value);

    if (err == ESP_OK)
        err = nvs_commit(handle);

    nvs_close(handle);
    return err;
}

ssize_t port_kv_blob_size(const char *ns, const char *key)
{
    nvs_handle_t handle;

    if (nvs_open(ns, NVS_READONLY, &handle) != ESP_OK)
        return -1;

    size_t    len = 0;
    esp_err_t err = nvs_get_blob(handle, key, NULL, &len);

    nvs_close(handle);

    return (err == ESP_OK) ? (ssize_t)len : -1;
}

ssize_t port_kv_get_blob(const char *ns, const char *key, void *buf, size_t len)
{
    nvs_handle_t handle;

    if (nvs_open(ns, NVS_READONLY, &handle) != ESP_OK)
        return -1;

    size_t    got = len;
    esp_err_t err = nvs_get_blob(handle, key, buf, &got);

    nvs_close(handle);

    return (err == ESP_OK) ? (ssize_t)got : -1;
}
