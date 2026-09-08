/**
 * @file esp32/port_indev.c
 *
 * The touch panel on esp_lcd_touch, replacing TFT_eSPI's built-in XPT2046
 * reader and SensorLib's FT6X36 driver.
 *
 * Both panels do their whole raw-to-screen conversion in a process_coordinates
 * callback, with esp_lcd_touch_config_t's swap_xy and mirror_* flags left at
 * zero. That is deliberate for each of them, and for a different reason:
 *
 *   XPT2046: the flags cannot express what this panel needs. The controller
 *     reports a 12-bit ADC reading, and the calibration is an offset and a span
 *     per axis (see below); esp_lcd_touch offers only x_max/y_max, which is a
 *     full-scale divisor. The registry driver ignores the flags entirely in any
 *     case.
 *
 *   FT6X36: the flags would be applied in the wrong order and against the wrong
 *     dimension. esp_lcd_touch applies mirror_x and mirror_y *before* swap_xy,
 *     using x_max for one and y_max for the other -- but on a swapped panel the
 *     raw x axis runs along the screen's height, so mirroring it against
 *     x_max = 320 rather than 240 is out by 80 pixels. The panel is 240x320,
 *     so the error is invisible in a code review and obvious on the bench.
 */
#include "port_indev.h"

#include "board_pins.h"
#include "port_display.h"

#include <assert.h>

#include "esp_lcd_panel_io.h"
#include "esp_lcd_touch.h"
#include "esp_log.h"

#include "port_sys.h"

#if OHEZ_TOUCH_XPT2046
#include "esp_lcd_touch_xpt2046.h"
#include "driver/spi_master.h"
#endif

#if OHEZ_TOUCH_FT5X06
#include "esp_lcd_touch_ft5x06.h"
#include "board_i2c.h"
#endif

static const char *TAG = "port_indev";

static esp_lcd_touch_handle_t touch;

static inline uint16_t clamp_to(int32_t value, int32_t limit)
{
    if (value < 0)
        return 0;
    if (value > limit)
        return (uint16_t)limit;

    return (uint16_t)value;
}

#if OHEZ_TOUCH_XPT2046

/* The four numbers TFT_eSPI was given as calData[] = {275, 3620, 264, 3532, 1}.
 *
 * They are not what they look like. setTouch() stores parameters[1] straight
 * into touchCalibration_x1, and calibrateTouch() exports that value *after*
 * subtracting x0 -- so 3620 and 3532 are spans, not maxima, and convertRawXY()
 * divides by them directly. Reading them as maxima puts every touch about 8 %
 * out across the screen.
 *
 * parameters[4] bit 0 is touchCalibration_rotate, and it is set: the axes are
 * swapped, so the screen's x comes from the controller's y. Bits 1 and 2,
 * invert_x and invert_y, are both clear. */
#define TOUCH_CAL_X_ORIGIN  275
#define TOUCH_CAL_X_SPAN    3620
#define TOUCH_CAL_Y_ORIGIN  264
#define TOUCH_CAL_Y_SPAN    3532

static void xpt2046_to_screen(esp_lcd_touch_handle_t tp, uint16_t *x, uint16_t *y,
                              uint16_t *strength, uint8_t *point_num,
                              uint8_t max_point_num)
{
    (void)tp;
    (void)strength;

    uint8_t count = (*point_num < max_point_num) ? *point_num : max_point_num;

    for (uint8_t i = 0; i < count; i++)
    {
        /* Signed, and read before either is written: the subtraction goes
         * negative for a press outside the calibrated area, and the screen x
         * is computed from the raw y. */
        int32_t raw_x = x[i];
        int32_t raw_y = y[i];

        int32_t screen_x = ((raw_y - TOUCH_CAL_X_ORIGIN) * PORT_DISPLAY_WIDTH) / TOUCH_CAL_X_SPAN;
        int32_t screen_y = ((raw_x - TOUCH_CAL_Y_ORIGIN) * PORT_DISPLAY_HEIGHT) / TOUCH_CAL_Y_SPAN;

#if OHEZ_TOUCH_FLIP
        screen_x = (PORT_DISPLAY_WIDTH - 1) - screen_x;
        screen_y = (PORT_DISPLAY_HEIGHT - 1) - screen_y;
#endif

        /* Clamped, where TFT_eSPI's getTouch() rejected the whole reading if
         * either axis landed outside the screen. A press a couple of pixels
         * past the edge is a press on the widget at the edge, not a press that
         * did not happen -- and the bezel makes those common. */
        x[i] = clamp_to(screen_x, PORT_DISPLAY_WIDTH - 1);
        y[i] = clamp_to(screen_y, PORT_DISPLAY_HEIGHT - 1);
    }
}

#endif /* OHEZ_TOUCH_XPT2046 */

#if OHEZ_TOUCH_FT5X06

static void ft5x06_to_screen(esp_lcd_touch_handle_t tp, uint16_t *x, uint16_t *y,
                             uint16_t *strength, uint8_t *point_num,
                             uint8_t max_point_num)
{
    (void)tp;
    (void)strength;

    uint8_t count = (*point_num < max_point_num) ? *point_num : max_point_num;

    for (uint8_t i = 0; i < count; i++)
    {
        /* The controller reports the panel's own portrait 240x320 grid. The
         * display is used in landscape, so the axes swap, and the resulting y
         * runs the wrong way. This is what main.cpp did by hand. */
        int32_t raw_x = x[i];
        int32_t raw_y = y[i];

        x[i] = clamp_to(raw_y, PORT_DISPLAY_WIDTH - 1);
        y[i] = clamp_to((PORT_DISPLAY_HEIGHT - 1) - raw_x, PORT_DISPLAY_HEIGHT - 1);
    }
}

#endif /* OHEZ_TOUCH_FT5X06 */

/* How long a tap that woke the display keeps the pointer quiet. Long enough
 * that the finger has lifted, short enough not to eat a deliberate second tap.
 * Unchanged from the Arduino code. */
#define TOUCH_WAKE_SUPPRESS_MS 200

static void read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;

    static uint64_t suppress_until;
    static int32_t last_x;
    static int32_t last_y;

    /* Every path below sets data->state. The Arduino version returned early
     * from the two suppression cases without setting it, so LVGL went on
     * seeing whatever the previous read reported -- a press, for those 200 ms,
     * which is exactly the press being suppressed. */
    data->point.x = last_x;
    data->point.y = last_y;

    if (port_millis() < suppress_until)
    {
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }

    uint16_t x = 0;
    uint16_t y = 0;
    uint8_t point_num = 0;

    esp_lcd_touch_read_data(touch);

    bool pressed = esp_lcd_touch_get_coordinates(touch, &x, &y, NULL, &point_num, 1);

    if (pressed == true && point_num > 0 && ohez_touch_wake() == true)
    {
        suppress_until = port_millis() + TOUCH_WAKE_SUPPRESS_MS;
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }

    if (pressed == true && point_num > 0)
    {
        last_x = x;
        last_y = y;
        data->state = LV_INDEV_STATE_PRESSED;
    }
    else
    {
        data->state = LV_INDEV_STATE_RELEASED;
    }

    /* On release LVGL still wants the last position: it decides which widget
     * was clicked from where the pointer was when it came up. */
    data->point.x = last_x;
    data->point.y = last_y;
}

void port_indev_init(lv_display_t *disp)
{
    esp_lcd_touch_config_t config = {
        .x_max = PORT_DISPLAY_WIDTH,
        .y_max = PORT_DISPLAY_HEIGHT,
        .rst_gpio_num = GPIO_NUM_NC,
        .int_gpio_num = GPIO_NUM_NC,
        .levels = {
            .reset = 0,
            .interrupt = 0,
        },
        /* All zero on purpose -- see the file comment. */
        .flags = {
            .swap_xy = 0,
            .mirror_x = 0,
            .mirror_y = 0,
        },
        .interrupt_callback = NULL,
    };

#if OHEZ_TOUCH_XPT2046
    config.process_coordinates = xpt2046_to_screen;

    /* Same SPI host as the panel, a device of its own on it. The XPT2046 tops
     * out around 2 MHz, which the component's own IO config already sets. */
    esp_lcd_panel_io_spi_config_t io_config = ESP_LCD_TOUCH_IO_SPI_XPT2046_CONFIG(OHEZ_TOUCH_PIN_CS);
    esp_lcd_panel_io_handle_t io = NULL;

    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(OHEZ_LCD_SPI_HOST, &io_config, &io));
    ESP_ERROR_CHECK(esp_lcd_touch_new_spi_xpt2046(io, &config, &touch));

    ESP_LOGI(TAG, "XPT2046 on CS %d%s", OHEZ_TOUCH_PIN_CS,
             OHEZ_TOUCH_FLIP ? ", flipped" : "");
#endif

#if OHEZ_TOUCH_FT5X06
    config.process_coordinates = ft5x06_to_screen;

    i2c_master_bus_handle_t i2c = board_i2c_bus();
    assert(i2c != NULL);

    esp_lcd_panel_io_i2c_config_t io_config = ESP_LCD_TOUCH_IO_I2C_FT5x06_CONFIG();
    io_config.scl_speed_hz = OHEZ_TOUCH_I2C_HZ;

    esp_lcd_panel_io_handle_t io = NULL;

    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(i2c, &io_config, &io));
    ESP_ERROR_CHECK(esp_lcd_touch_new_i2c_ft5x06(io, &config, &touch));

    ESP_LOGI(TAG, "FT6X36 on I2C");
#endif

    lv_indev_t *indev = lv_indev_create();

    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, read_cb);
    lv_indev_set_display(indev, disp);
}
