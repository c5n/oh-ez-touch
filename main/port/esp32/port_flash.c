/**
 * @file esp32/port_flash.c
 *
 * Nothing to do: the flash NVS sits on is real, and the bootloader has already
 * made it usable by the time app_main() runs. The host's counterpart has to
 * conjure a flash image out of a file, which is the whole reason this hook
 * exists.
 */

#include "port_internal.h"

esp_err_t port_flash_init(void)
{
    return ESP_OK;
}
