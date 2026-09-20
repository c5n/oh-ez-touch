/**
 * @file linux/port_flash.c
 *
 * Make the host's emulated flash persist across runs, so that NVS -- and with
 * it the WLAN provisioning path -- is exercisable in the simulator.
 *
 * By default esp_partition's linux emulation (partition_linux.c) mkstemp()s a
 * fresh image per run, fills it with 0xFF and blits the build's partition table
 * into it, which means NVS starts empty every time. Pointing flash_file_name at
 * a stable path fixes that, but the API opens the file read-write and
 * EXISTING-ONLY -- it will not create it -- so the image has to be laid down
 * here, once. There is no Kconfig option for any of this; it is a runtime API.
 */

#include "port_internal.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

#include "esp_log.h"
#include "esp_private/partition_linux.h"
#include "sdkconfig.h"

static const char *TAG = "port_flash";

/* esp_flash_partitions.h, which is where ESP_PARTITION_TABLE_OFFSET normally
 * comes from, lives in bootloader_support -- and that component is not built
 * for the linux target. The Kconfig value behind it is the same number. */
#define PORT_FLASH_TABLE_OFFSET CONFIG_PARTITION_TABLE_OFFSET

/* The partition table the build just generated. Its path is passed in from
 * main/CMakeLists.txt, because only CMake knows the build directory. */
#ifndef PORT_FLASH_TABLE_BIN
#error "PORT_FLASH_TABLE_BIN must be defined by the build (see main/CMakeLists.txt)"
#endif

/** Creates the parent directories of `file_path`. */
static void port_flash_mkdir_p(const char *file_path)
{
    char tmp[PATH_MAX];

    snprintf(tmp, sizeof(tmp), "%s", file_path);

    char *slash = strrchr(tmp, '/');

    if (slash == NULL)
        return;

    *slash = '\0';

    for (char *p = tmp + 1; *p != '\0'; p++)
    {
        if (*p != '/')
            continue;

        *p = '\0';
        mkdir(tmp, 0755);
        *p = '/';
    }

    mkdir(tmp, 0755);
}

/**
 * The image path, overridable so that two simulators can be run side by side:
 *
 *   $OHEZ_STATE_DIR/flash.bin, else $XDG_STATE_HOME/oh-ez-touch/flash.bin, else
 *   $HOME/.local/state/oh-ez-touch/flash.bin, else /tmp/oh-ez-touch/flash.bin.
 */
static const char *port_flash_path(void)
{
    static char path[PATH_MAX];

    if (path[0] != '\0')
        return path;

    const char *override = getenv("OHEZ_STATE_DIR");
    const char *xdg      = getenv("XDG_STATE_HOME");
    const char *home     = getenv("HOME");

    if (override != NULL && override[0] != '\0')
        snprintf(path, sizeof(path), "%s/flash.bin", override);
    else if (xdg != NULL && xdg[0] != '\0')
        snprintf(path, sizeof(path), "%s/oh-ez-touch/flash.bin", xdg);
    else if (home != NULL && home[0] != '\0')
        snprintf(path, sizeof(path), "%s/.local/state/oh-ez-touch/flash.bin", home);
    else
        snprintf(path, sizeof(path), "/tmp/oh-ez-touch/flash.bin");

    return path;
}

/** Lays down a 0xFF image with the partition table in place. */
static esp_err_t port_flash_create(const char *path)
{
    port_flash_mkdir_p(path);

    FILE *img = fopen(path, "wb");

    if (img == NULL)
    {
        ESP_LOGE(TAG, "cannot create %s: %s", path, strerror(errno));
        return ESP_FAIL;
    }

    /* static, not automatic: this runs on the main task, and even with
     * CONFIG_ESP_MAIN_TASK_STACK_SIZE raised, a multi-kilobyte stack buffer
     * here is how the FreeRTOS ready lists get quietly overwritten. */
    static uint8_t erased[4096];

    memset(erased, 0xFF, sizeof(erased));

    for (size_t off = 0; off < ESP_PARTITION_DEFAULT_EMULATED_FLASH_SIZE;
         off += sizeof(erased))
    {
        if (fwrite(erased, 1, sizeof(erased), img) != sizeof(erased))
        {
            ESP_LOGE(TAG, "short write on %s", path);
            fclose(img);
            return ESP_FAIL;
        }
    }

    FILE *table = fopen(PORT_FLASH_TABLE_BIN, "rb");

    if (table == NULL)
    {
        ESP_LOGE(TAG, "no partition table at %s", PORT_FLASH_TABLE_BIN);
        fclose(img);
        return ESP_ERR_NOT_FOUND;
    }

    /* 0xC00 is the space the partition table gets before the first partition;
     * partitions.csv uses far less than that. */
    static uint8_t table_bytes[0xC00];
    size_t         table_len = fread(table_bytes, 1, sizeof(table_bytes), table);

    fclose(table);

    if (fseek(img, PORT_FLASH_TABLE_OFFSET, SEEK_SET) != 0 ||
        fwrite(table_bytes, 1, table_len, img) != table_len)
    {
        ESP_LOGE(TAG, "cannot place the partition table in %s", path);
        fclose(img);
        return ESP_FAIL;
    }

    if (fclose(img) != 0)
    {
        ESP_LOGE(TAG, "cannot finish writing %s: %s", path, strerror(errno));
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "created %s (%u bytes of partition table at 0x%x)", path,
             (unsigned)table_len, (unsigned)PORT_FLASH_TABLE_OFFSET);

    return ESP_OK;
}

esp_err_t port_flash_init(void)
{
    const char *path = port_flash_path();

    if (access(path, R_OK | W_OK) != 0)
    {
        esp_err_t err = port_flash_create(path);

        if (err != ESP_OK)
            return err;
    }

    /* The emulation mmap()s the image MAP_SHARED, so two instances on one image
     * corrupt each other's NVS. Hold the lock for the life of the process --
     * the descriptor is deliberately never closed. */
    int lock_fd = open(path, O_RDWR);

    if (lock_fd < 0)
    {
        ESP_LOGE(TAG, "cannot open %s: %s", path, strerror(errno));
        return ESP_FAIL;
    }

    if (flock(lock_fd, LOCK_EX | LOCK_NB) != 0)
    {
        ESP_LOGE(TAG, "%s is in use by another instance; "
                      "set OHEZ_STATE_DIR to run a second one", path);
        close(lock_fd);
        return ESP_ERR_INVALID_STATE;
    }

    esp_partition_file_mmap_ctrl_t *ctrl = esp_partition_get_file_mmap_ctrl_input();

    snprintf(ctrl->flash_file_name, sizeof(ctrl->flash_file_name), "%s", path);
    /* Both of these MUST stay empty: esp_partition rejects either of them in
     * combination with a flash file name. */
    ctrl->flash_file_size      = 0;
    ctrl->partition_file_name[0] = '\0';

    ESP_LOGI(TAG, "emulated flash backed by %s", path);

    return ESP_OK;
}
