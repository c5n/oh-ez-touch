/**
 * @file linux/port_display.c
 *
 * The simulator's display: an SDL window, from LVGL's own SDL driver
 * (LV_USE_SDL in lv_conf.h, which the linux target turns on).
 *
 * The window is created at the panel's real 320x240 and then scaled by the
 * driver, so the UI still lays out against exactly the pixel grid the device
 * has while being legible on a desktop monitor. This is what -D SDL_ZOOM=2 used
 * to do under PlatformIO.
 */
#include "port_display.h"

#include <stdlib.h>

#include "drivers/sdl/lv_sdl_window.h"

#include "esp_log.h"

static const char *TAG = "port_display";

lv_display_t *port_display_init(void)
{
    lv_display_t *disp = lv_sdl_window_create(PORT_DISPLAY_WIDTH, PORT_DISPLAY_HEIGHT);

    if (disp == NULL)
    {
        /* No display means no UI at all, and LVGL would fail on the next call
         * anyway; say why while there is still a message to read. */
        ESP_LOGE(TAG, "lv_sdl_window_create failed -- is DISPLAY set?");
        abort();
    }

    lv_sdl_window_set_zoom(disp, 2.0f);
    lv_sdl_window_set_title(disp, "OhEzTouch");

    ESP_LOGI(TAG, "SDL window %dx%d at zoom 2.0", PORT_DISPLAY_WIDTH, PORT_DISPLAY_HEIGHT);

    return disp;
}
