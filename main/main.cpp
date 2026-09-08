/**
 * @file main.cpp
 *
 * Skeleton entry point for the ESP-IDF build. The application still lives in
 * src/ and is built by platformio.ini; this file exists so that the new build
 * system can be exercised end to end -- LVGL, lodepng and ArduinoJson all link,
 * and on the linux target an SDL window actually opens -- before any
 * application code moves.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "lvgl.h"
#include "lodepng/lodepng.h"
#include "ArduinoJson.h"

#include "port/ohez_port.h"

static const char *TAG = "ohez";

/* lv_conf.h sets LV_LOG_PRINTF 0 because printf() from more than one task is
 * documented-unsafe on the FreeRTOS POSIX simulator. */
static void ui_log_print(lv_log_level_t level, const char *buf)
{
    LV_UNUSED(level);
    ESP_LOGI("lvgl", "%s", buf);
}

/* Touch each component once, so that a missing REQUIRES or a broken
 * LODEPNG_NO_COMPILE_* combination fails here rather than in a later commit. */
static void smoke_test_components(void)
{
    JsonDocument doc;
    if (deserializeJson(doc, "{\"sitemap\":\"oheztouch\"}") == DeserializationError::Ok) {
        ESP_LOGI(TAG, "ArduinoJson: sitemap=%s", doc["sitemap"].as<const char *>());
    }
    ESP_LOGI(TAG, "lodepng: %s", lodepng_error_text(0));
    ESP_LOGI(TAG, "LVGL %d.%d.%d", lv_version_major(), lv_version_minor(),
             lv_version_patch());
}

/* The same for the port layer. Every call here is one an application module
 * will make in a later commit; doing it now means a port that does not work is
 * found here rather than half way through moving src/. */
static void smoke_test_port(void)
{
    struct tm now;

    ESP_LOGI(TAG, "port_millis=%llu port_micros=%llu",
             (unsigned long long)port_millis(), (unsigned long long)port_micros());
    ESP_LOGI(TAG, "port_free_heap=%u bytes", (unsigned)port_free_heap());
    ESP_LOGI(TAG, "port_localtime: %s",
             port_localtime(&now) ? asctime(&now) : "not synchronised yet");

    if (port_storage_init() == ESP_OK)
    {
        /* Read data/config.json, which the device gets from the flashed spiffs
         * image and the host from its config directory. Read-only: overwriting
         * it here would destroy a real configuration. */
        ssize_t size = port_storage_size("config.json");

        if (size < 0)
        {
            ESP_LOGW(TAG, "port_storage: no config.json in the store yet");
        }
        else
        {
            char    head[48];
            ssize_t got = port_storage_read("config.json", head, sizeof(head) - 1);

            if (got > 0)
            {
                head[got] = '\0';
                ESP_LOGI(TAG, "port_storage: config.json is %d bytes, starts: %s",
                         (int)size, head);
            }
        }

        /* The write path, on a scratch name, so that both directions are
         * proven without touching anything that matters. */
        static const char probe[] = "written by smoke_test_port";

        if (port_storage_write("probe.txt", probe, sizeof(probe) - 1) > 0)
        {
            char    back[64] = "";
            ssize_t got      = port_storage_read("probe.txt", back, sizeof(back) - 1);

            if (got > 0)
            {
                back[got] = '\0';
                ESP_LOGI(TAG, "port_storage: read back \"%s\"", back);
            }
        }
    }

    /* port_kv: a boot counter proves that NVS persists across runs, which on
     * the host is the whole point of port_flash_init(). */
    if (port_kv_init() == ESP_OK)
    {
        char     value[16] = "";
        unsigned boots     = 0;

        if (port_kv_get_str("oheztouch", "boots", value, sizeof(value)) == ESP_OK)
            boots = (unsigned)strtoul(value, NULL, 10);

        snprintf(value, sizeof(value), "%u", boots + 1);

        if (port_kv_set_str("oheztouch", "boots", value) == ESP_OK)
            ESP_LOGI(TAG, "port_kv: boot %s (must increment across runs)", value);
    }
}

static void ui_task(void *arg)
{
    LV_UNUSED(arg);

    /* All LVGL init happens here rather than in app_main: LVGL's SDL backend
     * pumps SDL from an lv_timer, so SDL_Init() and every SDL_PollEvent() run
     * on whichever task calls lv_timer_handler(). SDL requires that to be one
     * and the same thread. */
    lv_init();
    lv_log_register_print_cb(ui_log_print);
    lv_tick_set_cb(port_tick_ms);

#if CONFIG_IDF_TARGET_LINUX
    lv_display_t *disp = lv_sdl_window_create(320, 240);
    lv_sdl_window_set_zoom(disp, 2.0f);
    lv_sdl_window_set_title(disp, "OhEzTouch");
    lv_sdl_mouse_create();

    lv_obj_t *label = lv_label_create(lv_screen_active());
    lv_label_set_text_fmt(label, "oh-ez-touch\nESP-IDF skeleton\n%s", VERSION_GIT_HASH);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_center(label);
#else
    /* The esp_lcd display and esp_lcd_touch input ports arrive with the device
     * bring-up commit; until then this target only proves that it links. */
    ESP_LOGI(TAG, "no display port yet on this target");
#endif

    for (;;) {
        uint32_t next = lv_timer_handler();
        if (next == LV_NO_TIMER_READY) {
            next = LV_DEF_REFR_PERIOD;
        }
        vTaskDelay(pdMS_TO_TICKS(next));
    }
}

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "oh-ez-touch %s starting", VERSION_GIT_HASH);
    smoke_test_components();
    smoke_test_port();

    xTaskCreate(ui_task, "ui", 16384, NULL, 5, NULL);

    /* Do not return on the linux target: FreeRTOS's linux port calls
     * vTaskDelete(NULL) on the main task afterwards, and that trips an
     * assertion in vTaskSwitchContext(). */
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
