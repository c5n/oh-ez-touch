/**
 * @file linux/port_storage.c
 *
 * The config store on the host: a real directory of real files, so that
 * config.json can be read and edited by hand while the simulator runs.
 *
 * IDF excludes esp_spiffs.c from the linux build, so there is no SPIFFS here to
 * emulate; that is not a limitation worth working around, because a hand-
 * editable file is what makes the simulator useful.
 */

#include "port_storage.h"
#include "port_internal.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "esp_log.h"

static const char *TAG = "port_storage";

#define PORT_STORAGE_APP_DIR "oh-ez-touch"

/**
 * The store directory, XDG-style, overridable for tests and for running two
 * simulators side by side:
 *
 *   $OHEZ_CONFIG_DIR, else $XDG_CONFIG_HOME/oh-ez-touch, else
 *   $HOME/.config/oh-ez-touch, else /tmp/oh-ez-touch.
 */
static const char *port_storage_dir(void)
{
    static char dir[PATH_MAX];

    if (dir[0] != '\0')
        return dir;

    const char *override = getenv("OHEZ_CONFIG_DIR");
    const char *xdg      = getenv("XDG_CONFIG_HOME");
    const char *home     = getenv("HOME");

    if (override != NULL && override[0] != '\0')
        snprintf(dir, sizeof(dir), "%s", override);
    else if (xdg != NULL && xdg[0] != '\0')
        snprintf(dir, sizeof(dir), "%s/%s", xdg, PORT_STORAGE_APP_DIR);
    else if (home != NULL && home[0] != '\0')
        snprintf(dir, sizeof(dir), "%s/.config/%s", home, PORT_STORAGE_APP_DIR);
    else
        snprintf(dir, sizeof(dir), "/tmp/%s", PORT_STORAGE_APP_DIR);

    return dir;
}

esp_err_t port_storage_init(void)
{
    const char *dir = port_storage_dir();

    /* One level only: $XDG_CONFIG_HOME and $HOME are the user's, and this
     * project has no business creating them. */
    if (mkdir(dir, 0755) != 0 && errno != EEXIST)
    {
        ESP_LOGE(TAG, "cannot create %s: %s", dir, strerror(errno));
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "config store is %s", dir);
    return ESP_OK;
}

esp_err_t port_storage_path(const char *name, char *buf, size_t buf_size)
{
    int n = snprintf(buf, buf_size, "%s/%s", port_storage_dir(), name);

    if (n < 0 || (size_t)n >= buf_size)
        return ESP_ERR_INVALID_SIZE;

    return ESP_OK;
}
