/**
 * @file port_kv.h
 *
 * The small, persistent key/value store that backs wlan_credentials_get/set().
 *
 * It is deliberately *not* port_storage: the two stores exist separately
 * because NVS survives both an OTA and a filesystem reflash, while
 * /config.json is overwritten by every `idf.py flash`. Keeping the WLAN
 * credentials out of the config file is what stops a firmware upload from
 * silently unprovisioning the device it was uploaded to.
 *
 * Both targets use NVS. On the host that is genuine NVS on genuine emulated
 * flash -- see the port's flash-image handling -- not a stand-in, so the
 * provisioning path is exercisable in the simulator.
 *
 * This replaces Arduino's Preferences. The blob accessors exist for exactly one
 * caller: wlan.cpp's migration of AutoConnect's AC_CREDT blob.
 */
#ifndef PORT_KV_H
#define PORT_KV_H

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Bring up the store. Call once, before any other call here.
 *
 * On the host this also creates and locks the emulated flash image, so a second
 * simulator instance fails here rather than corrupting the first one's NVS.
 */
esp_err_t port_kv_init(void);

/**
 * Reads a string into `buf`, always NUL-terminating on success.
 *
 * Returns ESP_ERR_NVS_NOT_FOUND when the namespace or the key has never been
 * written, which for our callers just means "not provisioned yet".
 */
esp_err_t port_kv_get_str(const char *ns, const char *key, char *buf, size_t buf_size);

/** Writes a string, replacing any previous value, and commits. */
esp_err_t port_kv_set_str(const char *ns, const char *key, const char *value);

/** Byte length of a blob, or -1 if the namespace or key does not exist. */
ssize_t port_kv_blob_size(const char *ns, const char *key);

/** Reads at most `len` bytes of a blob. Returns the count read, or -1. */
ssize_t port_kv_get_blob(const char *ns, const char *key, void *buf, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* PORT_KV_H */
