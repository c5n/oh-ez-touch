/**
 * @file esp32/board_i2c.c
 *
 * See board_i2c.h.
 */
#include "board_i2c.h"

#include "board_pins.h"

#include "esp_log.h"

static const char *TAG = "board_i2c";

static i2c_master_bus_handle_t bus;

i2c_master_bus_handle_t board_i2c_bus(void)
{
    if (bus != NULL)
        return bus;

    i2c_master_bus_config_t config = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = (gpio_num_t)OHEZ_I2C_PIN_SDA,
        .scl_io_num = (gpio_num_t)OHEZ_I2C_PIN_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .intr_priority = 0,
        .trans_queue_depth = 0,
        .flags = { .enable_internal_pullup = true },
    };

    esp_err_t err = i2c_new_master_bus(&config, &bus);

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "i2c_new_master_bus on SDA %d / SCL %d: %s",
                 OHEZ_I2C_PIN_SDA, OHEZ_I2C_PIN_SCL, esp_err_to_name(err));
        bus = NULL;
        return NULL;
    }

    ESP_LOGI(TAG, "I2C on SDA %d / SCL %d", OHEZ_I2C_PIN_SDA, OHEZ_I2C_PIN_SCL);

    return bus;
}
