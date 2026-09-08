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

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "lvgl.h"
#include "lodepng/lodepng.h"
#include "ArduinoJson.h"

#include <time.h>

static const char *TAG = "ohez";

/* One tick source for both targets. esp_timer_get_time() is not available on
 * the linux target -- esp_timer registers headers-only there, so calling it is
 * a link error -- but clock_gettime(CLOCK_MONOTONIC) is implemented by IDF's
 * newlib and by glibc alike. Narrowing to 32 bit is what lv_tick_get_cb_t
 * wants, and wraps every 49 days on both targets identically. */
static uint32_t ui_tick_get(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)((uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL);
}

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

static void ui_task(void *arg)
{
    LV_UNUSED(arg);

    /* All LVGL init happens here rather than in app_main: LVGL's SDL backend
     * pumps SDL from an lv_timer, so SDL_Init() and every SDL_PollEvent() run
     * on whichever task calls lv_timer_handler(). SDL requires that to be one
     * and the same thread. */
    lv_init();
    lv_log_register_print_cb(ui_log_print);
    lv_tick_set_cb(ui_tick_get);

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

    xTaskCreate(ui_task, "ui", 16384, NULL, 5, NULL);

    /* Do not return on the linux target: FreeRTOS's linux port calls
     * vTaskDelete(NULL) on the main task afterwards, and that trips an
     * assertion in vTaskSwitchContext(). */
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
