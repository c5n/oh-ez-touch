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
 *     The conversion itself is in port/touch_cal.c, and the numbers behind it
 *     are a runtime copy of the board's constants rather than the constants
 *     themselves: the settings screen can solve new ones from four taps and the
 *     panel adopts them without a restart. board_pins.h still holds what an
 *     uncalibrated board of that type starts with.
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
#include <inttypes.h>

#include "esp_lcd_panel_io.h"
#include "esp_lcd_touch.h"
#include "esp_log.h"

#include "port_sys.h"
#include "touch_cal.h"
#include "ui/ui_input.h"

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

/* Which way up the display was created. The two process_coordinates callbacks
 * are registered by pointer, so the orientation reaches them as a file static
 * rather than as an argument. */
static bool touch_portrait;

#define SCREEN_W ((int32_t)PORT_DISPLAY_HOR_RES(touch_portrait))
#define SCREEN_H ((int32_t)PORT_DISPLAY_HOR_RES(!touch_portrait))

static inline uint16_t clamp_to(int32_t value, int32_t limit)
{
    if (value < 0)
        return 0;
    if (value > limit)
        return (uint16_t)limit;

    return (uint16_t)value;
}

/* The calibration map as the ILI9341 boards want it in portrait.
 *
 * touch_cal_apply()'s flip is the touch glass mounted rotated 180 degrees,
 * and it flips both axes in both orientations. Against the portrait MADCTL
 * -- MY alone, MV and MX dropped from the landscape value -- that is right
 * for y and backwards for x: the quarter turn the panel makes carries the x
 * reversal already, so a flipped glass wants its x straight and an unflipped
 * one wants it mirrored, the opposite of what the both-axes flip produces.
 * Applied as written, the pointer comes out mirrored left-for-right against
 * the picture, which is what the bench showed.
 *
 * Mirroring x back here instead of teaching touch_cal.c keeps the panel
 * family where it is defined: the shared file also serves the simulator,
 * whose display has no mirror bits to correct for. And because the
 * correction sits outside the map, a calibration solved in portrait is the
 * landscape one -- the pairs track the raw axes, the swap lives in the
 * mapping -- so the two orientations keep sharing one set of constants.
 *
 * The solve half of the same correction is in port_indev_cal_solve(). */
static void cal_apply(const struct touch_cal_s *c, int32_t raw_x, int32_t raw_y,
                      int32_t *screen_x, int32_t *screen_y)
{
    touch_cal_apply(c, touch_portrait, OHEZ_TOUCH_FLIP ? true : false,
                    SCREEN_W, SCREEN_H, raw_x, raw_y, screen_x, screen_y);

#if OHEZ_PANEL_ILI9341
    if (touch_portrait && screen_x != NULL)
        *screen_x = (SCREEN_W - 1) - *screen_x;
#endif
}

#if OHEZ_TOUCH_XPT2046

/* What the pointer converts with. Seeded from board_pins.h, which is where the
 * calibration lived outright until the settings screen could solve one.
 *
 * TFT_eSPI's calData had a fifth number, whose bit 0 was
 * touchCalibration_rotate: it is set on every panel here, so the axes are
 * swapped and the screen's x comes from the controller's y. Bits 1 and 2,
 * invert_x and invert_y, are clear -- inversion is OHEZ_TOUCH_FLIP, which stays
 * compile-time because it describes how the glass is mounted rather than how
 * this unit's panel reads. */
static struct touch_cal_s cal = {
    OHEZ_TOUCH_CAL_X_ORIGIN,
    OHEZ_TOUCH_CAL_X_SPAN,
    OHEZ_TOUCH_CAL_Y_ORIGIN,
    OHEZ_TOUCH_CAL_Y_SPAN,
};

/* The raw pair behind the reading currently being converted, and the raw pair
 * behind the last press that was confirmed. Two of them rather than one,
 * because read_cb() throws readings away: a calibration solved from a reading
 * the confirmation rejected would be solved from exactly the noise the
 * confirmation exists to discard. */
static int32_t pending_raw_x;
static int32_t pending_raw_y;
static int32_t confirmed_raw_x;
static int32_t confirmed_raw_y;
static bool    confirmed_raw_valid;

static void xpt2046_to_screen(esp_lcd_touch_handle_t tp, uint16_t *x, uint16_t *y,
                              uint16_t *strength, uint8_t *point_num,
                              uint8_t max_point_num)
{
    (void)tp;
    (void)strength;

    uint8_t count = (*point_num < max_point_num) ? *point_num : max_point_num;

    for (uint8_t i = 0; i < count; i++)
    {
        /* Signed, and read before either is written: the subtraction inside the
         * map goes negative for a press outside the calibrated area, and the
         * screen x is computed from the raw y. */
        int32_t raw_x = x[i];
        int32_t raw_y = y[i];

        int32_t screen_x = 0;
        int32_t screen_y = 0;

        /* Only the first point is ever read back -- read_cb() asks for one --
         * so this is that one. */
        if (i == 0)
        {
            pending_raw_x = raw_x;
            pending_raw_y = raw_y;
        }

        cal_apply(&cal, raw_x, raw_y, &screen_x, &screen_y);

        /* Clamped, where TFT_eSPI's getTouch() rejected the whole reading if
         * either axis landed outside the screen. A press a couple of pixels
         * past the edge is a press on the widget at the edge, not a press that
         * did not happen -- and the bezel makes those common.
         *
         * The clamp is here and not in touch_cal_apply(), because the
         * calibration screen has to see how far outside the screen the previous
         * calibration put a tap. That distance is the thing it draws. */
        x[i] = clamp_to(screen_x, SCREEN_W - 1);
        y[i] = clamp_to(screen_y, SCREEN_H - 1);
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
        int32_t raw_x = x[i];
        int32_t raw_y = y[i];

        if (touch_portrait)
        {
            /* The controller reports the panel's own portrait 240x320 grid,
             * which is what the display shows now: the identity mapping. */
            x[i] = clamp_to(raw_x, SCREEN_W - 1);
            y[i] = clamp_to(raw_y, SCREEN_H - 1);
        }
        else
        {
            /* Landscape: the axes swap, and the resulting y runs the wrong
             * way. This is what main.cpp did by hand. */
            x[i] = clamp_to(raw_y, SCREEN_W - 1);
            y[i] = clamp_to((SCREEN_H - 1) - raw_x, SCREEN_H - 1);
        }
    }
}

#endif /* OHEZ_TOUCH_FT5X06 */

/* The raw pair of a confirmed press, kept where port_indev_raw_press() can
 * reach it. The capacitive panel has no raw reading to keep: what it reports is
 * already the pixel grid, and there is nothing to solve. */
static void latch_confirmed_raw(void)
{
#if OHEZ_TOUCH_XPT2046
    confirmed_raw_x = pending_raw_x;
    confirmed_raw_y = pending_raw_y;
    confirmed_raw_valid = true;
#endif
}

/* How long a tap that woke the display keeps the pointer quiet. Long enough
 * that the finger has lifted, short enough not to eat a deliberate second tap.
 * Unchanged from the Arduino code. */
#define TOUCH_WAKE_SUPPRESS_MS 200

/* How far apart two readings of the same press may be before they are taken
 * for two different things: a twentieth of the screen on each axis, which is
 * sixteen pixels across and twelve down. TFT_eSPI's figure, and it wants to be
 * generous -- a finger that is still landing moves, and rejecting that only
 * costs one more frame of confirmation. */
#define TOUCH_AGREE_DIVISOR 20

static inline bool touch_agrees(int32_t a, int32_t b, int32_t span)
{
    int32_t delta = a - b;

    if (delta < 0)
        delta = -delta;

    return delta <= span / TOUCH_AGREE_DIVISOR;
}

/* A press is two readings that agree, not one that arrives.
 *
 * The controller reports a press from a single burst: one Z sample over the
 * threshold, then five X/Y samples taken back to back over the same SPI
 * transaction, inside about two hundred microseconds. The averaging looks like
 * filtering and is not -- every one of those samples is the same instant, so
 * any disturbance that outlasts a fifth of a millisecond arrives as five
 * samples in perfect agreement. A resistive panel next to a switching
 * backlight and a radio produces those, and this is polled sixty-two times a
 * second for as long as the panel is powered.
 *
 * What the reading is worth is therefore decided here rather than in the
 * driver. The first press-shaped reading is only a candidate: it is held, not
 * reported, and it becomes a press when the next poll agrees with it sixteen
 * milliseconds later. Noise does not, because it is not still there.
 *
 * This is what TFT_eSPI's getTouch() did for the Arduino firmware and what the
 * esp_lcd_touch drivers do not do. Losing it cost a phantom press every so
 * often, and the symptom was not a stray tap: nearly always the panel was
 * dimmed, so ohez_touch_wake() took the press, swallowed it and left nothing
 * behind but the wake chime -- a short beep out of nowhere.
 *
 * The cost is one frame of latency on every press, and a tap shorter than two
 * polls is not a tap. Neither is reachable with a finger.
 *
 * Only the start of a press is confirmed. Once one is established every
 * reading is believed, because a finger that is dragging really does move
 * further than the tolerance -- and a release is still reported the moment a
 * poll comes back empty, which is the behaviour the widgets are written
 * against. */
static void read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;

    static uint64_t suppress_until;
    static int32_t last_x;
    static int32_t last_y;

    /* The unconfirmed reading, and whether a confirmed press is in progress. */
    static bool    candidate;
    static int32_t candidate_x;
    static int32_t candidate_y;
    static bool    holding;

    /* Every path below sets data->state. The Arduino version returned early
     * from the two suppression cases without setting it, so LVGL went on
     * seeing whatever the previous read reported -- a press, for those 200 ms,
     * which is exactly the press being suppressed. */
    data->point.x = last_x;
    data->point.y = last_y;

    if (port_millis() < suppress_until)
    {
        candidate = false;
        holding = false;
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }

    uint16_t x = 0;
    uint16_t y = 0;
    uint8_t point_num = 0;

    esp_lcd_touch_read_data(touch);

    bool pressed = esp_lcd_touch_get_coordinates(touch, &x, &y, NULL, &point_num, 1);

    if (pressed == true && point_num > 0)
    {
        if (holding == false)
        {
            /* Nothing to compare against, or the two readings are too far
             * apart to be the same finger: this one becomes the candidate and
             * the pointer stays up for another frame. */
            if (   candidate == false
                || touch_agrees(x, candidate_x, SCREEN_W) == false
                || touch_agrees(y, candidate_y, SCREEN_H) == false)
            {
                candidate = true;
                candidate_x = x;
                candidate_y = y;
                data->state = LV_INDEV_STATE_RELEASED;
                return;
            }

            holding = true;
            candidate = false;

            /* Asked once the press is real, which is the whole point of
             * waiting: waking on a candidate would chime for exactly the
             * readings this is here to throw away. */
            if (ohez_touch_wake() == true)
            {
                suppress_until = port_millis() + TOUCH_WAKE_SUPPRESS_MS;
                holding = false;
                data->state = LV_INDEV_STATE_RELEASED;
                return;
            }
        }

        last_x = x;
        last_y = y;
        latch_confirmed_raw();
        data->state = LV_INDEV_STATE_PRESSED;
    }
    else
    {
        candidate = false;
        holding = false;
        data->state = LV_INDEV_STATE_RELEASED;
    }

    /* On release LVGL still wants the last position: it decides which widget
     * was clicked from where the pointer was when it came up. */
    data->point.x = last_x;
    data->point.y = last_y;
}

bool port_indev_calibratable(void)
{
    return OHEZ_TOUCH_XPT2046 ? true : false;
}

void port_indev_cal_defaults(struct touch_cal_s *out)
{
    if (out == NULL)
        return;

#if OHEZ_TOUCH_XPT2046
    out->x_origin = OHEZ_TOUCH_CAL_X_ORIGIN;
    out->x_span = OHEZ_TOUCH_CAL_X_SPAN;
    out->y_origin = OHEZ_TOUCH_CAL_Y_ORIGIN;
    out->y_span = OHEZ_TOUCH_CAL_Y_SPAN;
#else
    /* No constants on this board because it needs none. All zero, which
     * touch_cal_valid() reports as unusable -- and nothing asks, because
     * port_indev_calibratable() is false. */
    out->x_origin = 0;
    out->x_span = 0;
    out->y_origin = 0;
    out->y_span = 0;
#endif
}

void port_indev_cal_get(struct touch_cal_s *out)
{
    if (out == NULL)
        return;

#if OHEZ_TOUCH_XPT2046
    *out = cal;
#else
    port_indev_cal_defaults(out);
#endif
}

void port_indev_cal_set(const struct touch_cal_s *next)
{
#if OHEZ_TOUCH_XPT2046
    if (touch_cal_valid(next) == false)
    {
        ESP_LOGW(TAG, "calibration ignored: span is zero");
        return;
    }

    cal = *next;

    ESP_LOGI(TAG, "calibration %" PRId32 "/%" PRId32 " %" PRId32 "/%" PRId32,
             cal.x_origin, cal.x_span, cal.y_origin, cal.y_span);
#else
    (void)next;
#endif
}

bool port_indev_raw_press(int32_t *raw_x, int32_t *raw_y)
{
#if OHEZ_TOUCH_XPT2046
    if (confirmed_raw_valid == false)
        return false;

    if (raw_x != NULL)
        *raw_x = confirmed_raw_x;

    if (raw_y != NULL)
        *raw_y = confirmed_raw_y;

    return true;
#else
    (void)raw_x;
    (void)raw_y;

    return false;
#endif
}

void port_indev_cal_map(const struct touch_cal_s *cal, int32_t raw_x, int32_t raw_y,
                        int32_t *screen_x, int32_t *screen_y)
{
    cal_apply(cal, raw_x, raw_y, screen_x, screen_y);
}

const char *port_indev_cal_solve(const struct touch_cal_sample_s *samples, size_t count,
                                 struct touch_cal_s *out)
{
#if OHEZ_PANEL_ILI9341
    /* The target half of the correction cal_apply() makes: touch_cal_solve()
     * inverts touch_cal_apply(), so constants solved against the mirrored
     * corners are the ones whose read path -- cal_apply() -- lands the taps
     * on. Four samples, so the copy is a stack array rather than a rewrite
     * of the caller's. */
    if (touch_portrait && samples != NULL && count <= TOUCH_CAL_SAMPLES)
    {
        struct touch_cal_sample_s mirrored[TOUCH_CAL_SAMPLES];

        for (size_t i = 0; i < count; i++)
        {
            mirrored[i] = samples[i];
            mirrored[i].target_x = (SCREEN_W - 1) - mirrored[i].target_x;
        }

        return touch_cal_solve(mirrored, count, touch_portrait, OHEZ_TOUCH_FLIP ? true : false,
                               SCREEN_W, SCREEN_H, out);
    }
#endif

    return touch_cal_solve(samples, count, touch_portrait, OHEZ_TOUCH_FLIP ? true : false,
                           SCREEN_W, SCREEN_H, out);
}

void port_indev_init(lv_display_t *disp, bool portrait)
{
    touch_portrait = portrait;

    esp_lcd_touch_config_t config = {
        .x_max = (uint16_t)SCREEN_W,
        .y_max = (uint16_t)SCREEN_H,
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

#if OHEZ_TOUCH_OWN_BUS
    /* The CYD wires the touch controller to four pins of its own rather than
     * sharing the panel's bus, so the host needs initialising here -- the
     * display's spi_bus_initialize() in port_display.c covers only its own.
     * No DMA for a three-byte poll at the 2 MHz an XPT2046 tops out at. */
    spi_bus_config_t bus = {
        .sclk_io_num = OHEZ_TOUCH_PIN_SCLK,
        .mosi_io_num = OHEZ_TOUCH_PIN_MOSI,
        .miso_io_num = OHEZ_TOUCH_PIN_MISO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 32,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(OHEZ_TOUCH_SPI_HOST, &bus, SPI_DMA_DISABLED));
#endif

    /* A device of its own on the touch bus -- which is the panel's bus unless
     * the board's pin table says otherwise. The XPT2046 tops out around
     * 2 MHz, which the component's own IO config already sets. */
    esp_lcd_panel_io_spi_config_t io_config = ESP_LCD_TOUCH_IO_SPI_XPT2046_CONFIG(OHEZ_TOUCH_PIN_CS);
    esp_lcd_panel_io_handle_t io = NULL;

    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(OHEZ_TOUCH_SPI_HOST, &io_config, &io));
    ESP_ERROR_CHECK(esp_lcd_touch_new_spi_xpt2046(io, &config, &touch));

    ESP_LOGI(TAG, "XPT2046 on CS %d%s%s%s", OHEZ_TOUCH_PIN_CS,
             OHEZ_TOUCH_OWN_BUS ? ", own SPI bus" : "",
             OHEZ_TOUCH_FLIP ? ", flipped" : "",
             portrait ? ", portrait" : "");
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

    ui_input_disable_swipes(indev);
}
