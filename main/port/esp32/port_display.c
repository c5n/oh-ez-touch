/**
 * @file esp32/port_display.c
 *
 * The panel on esp_lcd, replacing TFT_eSPI.
 *
 * The conventions differ from TFT_eSPI's in four ways that are each silently
 * wrong rather than loudly broken if carried over unchanged, so each is spelled
 * out at the point it matters below: the rotation, the RGB565 byte order, the
 * exclusive end coordinate of a draw, and when the flush is finished.
 *
 * The structure of the flush -- two DMA-capable buffers and a trans-done
 * callback rather than a busy wait -- is taken from Espressif's own
 * esp_lvgl_port (src/lvgl9/esp_lvgl_port_disp.c). That component is not used
 * here: it has no linux target and it installs an esp_timer LVGL tick of its
 * own, which would break the one-tick-source property port_tick_ms() exists to
 * provide.
 */
#include "port_display.h"

#include "board_pins.h"

#include <assert.h>

#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#if OHEZ_PANEL_ILI9341
#include "esp_lcd_ili9341.h"
/* The registry's ILI9341 driver ignores esp_lcd_panel_dev_config_t::data_endian
 * entirely, so the swap has to happen in the flush callback. Not reachable
 * through lvgl.h. */
#include "draw/sw/lv_draw_sw_utils.h"
#define OHEZ_SWAP_RGB565_IN_SOFTWARE 1
#else
/* IDF's own ST7789 driver does honour data_endian, through the panel's RAMCTRL
 * register, so the panel swaps the bytes as it stores them and the CPU never
 * touches the buffer. */
#define OHEZ_SWAP_RGB565_IN_SOFTWARE 0
#endif

static const char *TAG = "port_display";

/* 24 lines divides 240 exactly, so a full-screen redraw is ten equal flushes.
 * Two of them, from internal DMA-capable memory: a static array is not
 * guaranteed to be either. 320 * 24 * 2 = 15360 bytes each.
 *
 * Taller strips look like a lever on the frame rate, and they are left alone
 * deliberately rather than for want of noticing. LVGL redraws in partial mode
 * one strip at a time and walks the object tree again for each, so a tile 93 px
 * tall falls across five strips here and three at 40 lines, and
 * every style lookup behind those draws is repeated with it -- and those
 * lookups are not cached, because LV_OBJ_STYLE_CACHE has to stay 0 for the live
 * theme switch (lv_conf.h says why). The transfer itself is the same number of
 * bytes either way; only the per-strip overhead changes.
 *
 * What stops it is where the memory comes from and when. 40 lines is 2 x 25600
 * rather than 2 x 15360, and this runs from ohez_setup() *before* wlan_setup()
 * and ble_scan_setup() -- so the display would take the extra 20 KB of internal
 * DMA-capable heap out from under the WiFi and Bluetooth stacks, and the
 * symptom would be an esp_wifi_init() failure on a wall panel rather than a
 * slower screen. A fallback here cannot catch that either: at this point in the
 * boot the heap is at its emptiest, so the larger pair would always succeed and
 * the shortfall would always land on somebody else.
 *
 * What would settle it is one number from a running board, and the firmware
 * already reports it: the "Free heap" row on the web status page (and
 * <prefix>/system/heap over MQTT) once WiFi and the BLE scanner are both up.
 * That is the headroom the extra 20 KB would have to come out of. There is no
 * PSRAM on any of these boards, so all of it is internal.
 *
 * And one number says whether it is worth asking. The two buffers already
 * overlap the render of one strip with the transfer of the last, so a taller
 * strip speeds up a full repaint only if the render is losing that race --
 * which the "Renderer" and "Waiting for panel" rows beside the heap one now
 * report. If the wait dominates, this lever buys nothing at all.
 *
 * The buffer is sized on the landscape width, which is the larger of the two
 * axes: a portrait panel draws 24-line strips of 240 px into exactly the same
 * buffers. */
#define DRAW_BUFFER_LINES   24
#define DRAW_BUFFER_BYTES   (PORT_DISPLAY_WIDTH * DRAW_BUFFER_LINES * 2)

static esp_lcd_panel_handle_t panel;

/* Called from the SPI ISR when the DMA transfer this flush queued has finished.
 * lv_display_flush_ready() is documented as ISR-safe. */
static bool on_color_trans_done(esp_lcd_panel_io_handle_t io,
                                esp_lcd_panel_io_event_data_t *edata,
                                void *user_ctx)
{
    (void)io;
    (void)edata;

    lv_display_flush_ready((lv_display_t *)user_ctx);

    return false;
}

static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    (void)disp;

#if OHEZ_SWAP_RGB565_IN_SOFTWARE
    lv_draw_sw_rgb565_swap(px_map,
                           (uint32_t)lv_area_get_width(area) * lv_area_get_height(area));
#endif

    /* The end coordinates are exclusive -- the panel drivers compute their
     * column and row address from (x_end - 1). LVGL's area is inclusive at both
     * ends, hence the + 1; without it every flush is one pixel short in each
     * direction and the screen tears into strips.
     *
     * No lv_display_flush_ready() here: the transfer is asynchronous and
     * on_color_trans_done() above reports it. Calling it here as well would let
     * LVGL start rendering into a buffer the DMA is still reading. */
    esp_lcd_panel_draw_bitmap(panel, area->x1, area->y1, area->x2 + 1, area->y2 + 1, px_map);

    /* One tick per strip, and it has to be here rather than in the wait for
     * the transfer above: that wait only runs when the DMA is still busy by
     * the time the next strip is rendered, and a frame that is slow because
     * it is CPU-bound -- a page of recolored icons, the layer renders the
     * entrance fades cost -- is exactly the one where it never is. Between
     * one strip's flush and the next there is no other blocking call anywhere
     * in LVGL's renderer (its own wait_for_flushing() is a busy spin), so
     * without this the render task holds its core for the whole frame, and a
     * frame that runs longer than the task watchdog's timeout starves the
     * idle task beside it and gets reported as a hung task. The delay bounds
     * the longest stretch without a block to one strip's render time, and
     * costs under a millisecond per strip at 1000 Hz. */
    vTaskDelay(1);
}

lv_display_t *port_display_init(bool portrait)
{
    /* Before the panel IO, which needs the display to hand to
     * lv_display_flush_ready() from the trans-done callback. */
    lv_display_t *disp = lv_display_create(PORT_DISPLAY_HOR_RES(portrait),
                                           PORT_DISPLAY_HOR_RES(!portrait));
    assert(disp != NULL);

    spi_bus_config_t bus = {
        .sclk_io_num = OHEZ_LCD_PIN_SCLK,
        .mosi_io_num = OHEZ_LCD_PIN_MOSI,
        /* Not -1, and not what ILI9341_PANEL_BUS_SPI_CONFIG() would set: on the
         * ArduiTouch the XPT2046 shares this bus and is read over MISO. */
        .miso_io_num = OHEZ_LCD_PIN_MISO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = DRAW_BUFFER_BYTES,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(OHEZ_LCD_SPI_HOST, &bus, SPI_DMA_CH_AUTO));

    esp_lcd_panel_io_spi_config_t io_config = {
        .cs_gpio_num = OHEZ_LCD_PIN_CS,
        .dc_gpio_num = OHEZ_LCD_PIN_DC,
        .spi_mode = 0,
        .pclk_hz = OHEZ_LCD_PIXEL_CLOCK_HZ,
        .trans_queue_depth = 10,
        .on_color_trans_done = on_color_trans_done,
        .user_ctx = disp,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    esp_lcd_panel_io_handle_t io = NULL;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(OHEZ_LCD_SPI_HOST, &io_config, &io));

    /* setRotation(3) under TFT_eSPI wrote MADCTL = MX|MY|MV|BGR for the ILI9341
     * and MV|MY|RGB for the ST7789. esp_lcd spells those out as swap_xy plus
     * mirror plus the element order, applied after init below. */
    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = OHEZ_LCD_PIN_RST,
#if OHEZ_PANEL_ILI9341
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,
        .data_endian = LCD_RGB_DATA_ENDIAN_BIG,
#else
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .data_endian = LCD_RGB_DATA_ENDIAN_LITTLE,
#endif
        .bits_per_pixel = 16,
    };

#if OHEZ_PANEL_ILI9341
    ESP_ERROR_CHECK(esp_lcd_new_panel_ili9341(io, &panel_config, &panel));
#else
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(io, &panel_config, &panel));
#endif

    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel));

    /* Portrait drops the swap and keeps the mirrors: the panel's own 240x320
     * grid then faces up, and which edge that calls "up" is a property of the
     * glass, not of anything here. A board that comes out upside down wants
     * both mirror flags flipped -- and its touch mapping re-checked against the
     * picture, in port_indev.c. Verified on the bench: landscape, all boards;
     * portrait, simulator only so far. */
    ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(panel, !portrait));
#if OHEZ_PANEL_ILI9341
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel, true, true));
#else
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel, false, true));
#endif

    /* Off on every board: the Lanbon's -DCONFIG_TFT_INVERSION_OFF was only half
     * of it, because main.cpp called tft.invertDisplay(false) unconditionally
     * anyway. */
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel, false));

    /* No CGRAM offset on any of these panels: TFT_eSPI defaulted to a 240x320
     * controller with no gap, and none of the boards overrode it. */
    ESP_ERROR_CHECK(esp_lcd_panel_set_gap(panel, 0, 0));

    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel, true));

    void *buf1 = heap_caps_malloc(DRAW_BUFFER_BYTES, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    void *buf2 = heap_caps_malloc(DRAW_BUFFER_BYTES, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    assert(buf1 != NULL && buf2 != NULL);

    lv_display_set_flush_cb(disp, flush_cb);
    lv_display_set_buffers(disp, buf1, buf2, DRAW_BUFFER_BYTES,
                           LV_DISPLAY_RENDER_MODE_PARTIAL);

    ESP_LOGI(TAG, "%s on SPI%d at %d MHz, %s, 2 x %d byte DMA buffers",
             OHEZ_PANEL_ILI9341 ? "ILI9341" : "ST7789",
             (int)OHEZ_LCD_SPI_HOST + 1,
             OHEZ_LCD_PIXEL_CLOCK_HZ / 1000000,
             portrait ? "portrait 240x320" : "landscape 320x240",
             DRAW_BUFFER_BYTES);

    return disp;
}
