/**
 * @file port_storage.h
 *
 * The config file. One named blob per file, replaced whole -- which is all
 * Config::loadConfig()/saveConfig() has ever done with SPIFFS.
 *
 * Device: SPIFFS, the `spiffs` partition of partitions.csv, written by
 *   `idf.py flash` from data/.
 * Host:   $XDG_CONFIG_HOME/oh-ez-touch/ (or ~/.config/oh-ez-touch/), so the
 *   simulator has a real config file that can be edited by hand. IDF excludes
 *   esp_spiffs.c from the linux build, so there is no SPIFFS to emulate there
 *   even if that were desirable.
 *
 * With this, loadConfig()/saveConfig() become fully shared code and the
 * simulator's `#else` branch in config.hpp -- which duplicates the defaults
 * from data/config.json, and already disagrees with it -- goes away.
 */
#ifndef PORT_STORAGE_H
#define PORT_STORAGE_H

#include <stddef.h>
#include <sys/types.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Mount or create the store. Call once, before any other call here.
 *
 * Returns ESP_OK, or an error if the store is unusable -- a device whose SPIFFS
 * partition has never been written reports that here rather than as a puzzling
 * "every setting reverted to its default" later.
 */
esp_err_t port_storage_init(void);

/**
 * Size of a stored blob in bytes, or -1 if it does not exist.
 *
 * `name` is a bare file name with no directory part and no leading slash, e.g.
 * "config.json". The port owns the directory.
 */
ssize_t port_storage_size(const char *name);

/** Reads at most `len` bytes. Returns the count read, or -1. */
ssize_t port_storage_read(const char *name, void *buf, size_t len);

/**
 * Replaces the blob with `len` bytes. Returns the count written, or -1.
 *
 * Not atomic on either target: a power cut mid-write leaves a truncated file,
 * and loadConfig() then falls back to the built-in defaults. That is the
 * behaviour SPIFFS already had.
 */
ssize_t port_storage_write(const char *name, const void *buf, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* PORT_STORAGE_H */
