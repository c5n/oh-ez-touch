/**
 * @file port_internal.h
 *
 * Hooks the shared port sources need from the per-target ones. Not part of the
 * platform boundary -- application code has no business including this.
 */
#ifndef PORT_INTERNAL_H
#define PORT_INTERNAL_H

#include <stddef.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Writes the absolute path of `name` in the config store into `buf`.
 *
 * The per-target half of port_storage: the read/write/size logic is plain
 * stdio and identical on both targets, so only the directory differs.
 */
esp_err_t port_storage_path(const char *name, char *buf, size_t buf_size);

/**
 * Makes the flash that NVS sits on usable, before nvs_flash_init().
 *
 * A no-op on the device, where the flash is real. On the host it points
 * esp_partition's file emulation at a stable image so that NVS survives a
 * restart, creating and locking that image if necessary.
 */
esp_err_t port_flash_init(void);

#ifdef __cplusplus
}
#endif

#endif /* PORT_INTERNAL_H */
