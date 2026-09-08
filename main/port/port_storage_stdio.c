/**
 * @file port_storage_stdio.c
 *
 * port_storage on plain stdio. Shared by both targets: SPIFFS is reached
 * through IDF's VFS, so once port_storage_path() has said where the store
 * lives, reading and writing a whole small file is the same code either way.
 */

#include "port_storage.h"
#include "port_internal.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "esp_log.h"

static const char *TAG = "port_storage";

ssize_t port_storage_size(const char *name)
{
    char path[PATH_MAX];
    struct stat st;

    if (port_storage_path(name, path, sizeof(path)) != ESP_OK)
        return -1;

    if (stat(path, &st) != 0)
        return -1;

    return (ssize_t)st.st_size;
}

ssize_t port_storage_read(const char *name, void *buf, size_t len)
{
    char path[PATH_MAX];

    if (port_storage_path(name, path, sizeof(path)) != ESP_OK)
        return -1;

    FILE *f = fopen(path, "rb");

    if (f == NULL)
    {
        ESP_LOGW(TAG, "cannot read %s: %s", path, strerror(errno));
        return -1;
    }

    size_t got = fread(buf, 1, len, f);

    /* A short read is only an error if it was not the end of the file: a
     * caller sizing its buffer from port_storage_size() gets exactly one
     * short read, at the end. */
    if (got < len && ferror(f))
    {
        ESP_LOGW(TAG, "error reading %s", path);
        fclose(f);
        return -1;
    }

    fclose(f);
    return (ssize_t)got;
}

ssize_t port_storage_write(const char *name, const void *buf, size_t len)
{
    char path[PATH_MAX];

    if (port_storage_path(name, path, sizeof(path)) != ESP_OK)
        return -1;

    FILE *f = fopen(path, "wb");

    if (f == NULL)
    {
        ESP_LOGE(TAG, "cannot write %s: %s", path, strerror(errno));
        return -1;
    }

    size_t put = fwrite(buf, 1, len, f);

    /* fclose() is where a buffered write actually reaches the medium, so its
     * result is part of the answer -- ignoring it would report success for a
     * write that filled the partition. */
    if (fclose(f) != 0 || put != len)
    {
        ESP_LOGE(TAG, "short write on %s (%u of %u bytes)", path,
                 (unsigned)put, (unsigned)len);
        return -1;
    }

    return (ssize_t)put;
}
