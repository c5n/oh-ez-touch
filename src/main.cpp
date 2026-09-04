#include <Arduino.h>
//#include "lv_conf.h"
#include <lvgl.h>
#include "version.h"
#include "config.hpp"
#include "debug.h"

#include "openhab_ui.hpp"
#include "ui_infolabel.hpp"

#if (SIMULATOR == 0)
#include <TFT_eSPI.h>
#if (TOUCH_DRIVER_FT6X36 == 1)
#include <Wire.h>
#include "TouchDrv.hpp"
#endif
#include "esp_wifi.h"
#include "ac_main.hpp"
#include "wlan.hpp"
#include "WiFi.h"
#include "ota/basic_ota.hpp"
#include "driver/backlight_control.hpp"
#include "driver/beeper_control.hpp"
#include "openhab_sensor_main.hpp"
#else
#include <unistd.h>
#define SDL_MAIN_HANDLED        /*To fix SDL's "undefined reference to WinMain" issue*/
#include <SDL2/SDL.h>
/* Display and input come from LVGL's own SDL driver, enabled by LV_USE_SDL in
 * lv_conf.h. The vendored copy of the (abandoned) lv_drivers SDL backend that
 * used to live in src/sdl is gone. */
#include <drivers/sdl/lv_sdl_window.h>
#include <drivers/sdl/lv_sdl_mouse.h>
#endif

#ifndef DEBUG_OUTPUT_BAUDRATE
#define DEBUG_OUTPUT_BAUDRATE 115200
#endif

#ifndef DEBUG_WLAN_STATES
#define DEBUG_WLAN_STATES 0
#endif

#ifndef DEBUG_DISPLAY_TOUCH
#define DEBUG_DISPLAY_TOUCH 0
#endif

#ifndef USE_ARDUINO_BASIC_OTA
#define USE_ARDUINO_BASIC_OTA 0
#endif

#ifndef TFT_BACKLIGHT_PIN
#define TFT_BACKLIGHT_PIN 15
#endif

#ifndef TFT_BACKLIGHT_INVERT
#define TFT_BACKLIGHT_INVERT 0
#endif

#ifndef TFT_TOUCH_FLIP
#define TFT_TOUCH_FLIP 0
#endif

#ifndef WLAN_OFFLINE_TIMEOUT
#define WLAN_OFFLINE_TIMEOUT (2 * 60 * 1000)
#endif

int screenWidth = 320;
int screenHeight = 240;

#if (SIMULATOR != 1)
BacklightControl tft_backlight;
TFT_eSPI tft = TFT_eSPI(); // TFT instance
#endif

#if (TOUCH_DRIVER_FT6X36 == 1)
TouchDrvFT6X36 touch_ft6x36;
#endif

#if (SIMULATOR != 1)
/* One tenth of the screen, as before. Note this must NOT be an lv_color_t
 * array: in LVGL v9 lv_color_t is a 3-byte {b,g,r} struct regardless of
 * LV_COLOR_DEPTH, so sizing it that way would allocate the wrong number of
 * bytes for an RGB565 panel. lv_display_set_buffers() takes bytes too.
 * The simulator needs none of this: lv_sdl_window_create() brings its own. */
static LV_ATTRIBUTE_MEM_ALIGN uint8_t draw_buf[320 * 10 * (LV_COLOR_DEPTH / 8)];
#endif

Config config;
Infolabel infolabel;

/* LVGL's tick source. Wrapping millis() is not cosmetic: lv_tick_get_cb_t
 * returns uint32_t, while millis() returns unsigned long, which is 64 bit on
 * the simulator host. This replaces both the v7 Ticker ISR on the device and
 * the SDL driver's tick thread. */
static uint32_t ui_tick_get(void)
{
    return (uint32_t)millis();
}

#if LV_USE_LOG != 0
// Serial debugging
static void my_print(lv_log_level_t level, const char *buf)
{
    LV_UNUSED(level);
    debug_printf("%s\r\n", buf);
}
#endif

#if (SIMULATOR != 1)
// Display flushing
void my_disp_flush(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    uint32_t w = lv_area_get_width(area);
    uint32_t h = lv_area_get_height(area);

    /* LV_COLOR_16_SWAP is gone in v9; the byte order is the driver's business.
     * pushColors(uint16_t *, len, true) swaps as it writes, which is what the
     * v7 code relied on -- so no lv_draw_sw_rgb565_swap() here. The cast
     * matters: the uint8_t * overload takes a byte count and does not swap. */
    tft.startWrite();
    tft.setAddrWindow(area->x1, area->y1, w, h);
    tft.pushColors((uint16_t *)px_map, w * h, true);
    tft.endWrite();

    lv_display_flush_ready(disp);
}

void my_touchpad_read(lv_indev_t *indev, lv_indev_data_t *data)
{
    static unsigned long suppress_touch_timeout;
    static int32_t last_x = 0;
    static int32_t last_y = 0;

    uint16_t touchX, touchY;
    bool touched = false;

#if (TOUCH_DRIVER_FT6X36 == 1)
    /* SensorLib 0.4 deprecated getPoint() in favour of getTouchPoints(); the old
     * call was only a shim around it. The arrays are zeroed because they used to
     * be left uninitialised when the panel reported no point at all. */
    int16_t ftx[2] = { 0, 0 }; int16_t fty[2] = { 0, 0 };
    const TouchPoints &ftpoints = touch_ft6x36.getTouchPoints();

    for (uint8_t i = 0; (i < ftpoints.getPointCount()) && (i < 2); i++)
    {
        ftx[i] = (int16_t)ftpoints.getPoint(i).x;
        fty[i] = (int16_t)ftpoints.getPoint(i).y;
    }

    touched = (ftpoints.getPointCount() >= 1);
#if DEBUG_DISPLAY_TOUCH
    if (touched == true)
        debug_printf("DISPLAY_TOUCH x[0]: %d y[0] %d  x[1]: %d y[1] %d\r\n", ftx[0], fty[0], ftx[1], fty[1]);
#endif

    touchX = (fty[0] > 0) ? (uint16_t)fty[0] : 0;
    //touchY = (ftx[0] > 0) ? (uint16_t)ftx[0] : 0;
    touchY = (ftx[0] > 0) ? (uint16_t)ftx[0] : 0;
    touchY = screenHeight - touchY;
    //touchY = screenHeight - (ftx[0] > 0 && ftx[0] <= screenHeight) ? ftx[0] : 0;
#else
    touched = tft.getTouch(&touchX, &touchY, 350);
#endif

#if TFT_TOUCH_FLIP
    touchX = screenWidth - touchX;
    touchY = screenHeight - touchY;
#endif

    if ((long)(millis() - suppress_touch_timeout) < 0)
    {
        return;
    }

#if (SIMULATOR != 1)
    if (touched == true && tft_backlight.resetDimTimeout() == true)
    {
        if (config.item.beeper.enabled == true)
            beeper_playNote(NOTE_C4, 50, 100, 0);
        suppress_touch_timeout = millis() + 200;
        return;
    }
#endif
#if DEBUG_DISPLAY_TOUCH
    if (touched == true)
        debug_printf("DISPLAY_TOUCH x: %u y %u\r\n", touchX, touchY);
#endif

    if (touchX <= screenWidth && touchY <= screenHeight)
    {
        data->state = touched ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;

        // Save the state and save the pressed coordinate
        if (data->state == LV_INDEV_STATE_PRESSED)
        {
            last_x = touchX;
            last_y = touchY;
        }

        // Set the coordinates (if released use the last pressed coordinates)
        data->point.x = last_x;
        data->point.y = last_y;
    }
#if DEBUG_DISPLAY_TOUCH
    else
    {
        if (touched == true)
            debug_printf("DISPLAY_TOUCH outside of expected parameters x: %u y %u\r\n", touchX, touchY);
    }
#endif

    /* data->continue_reading defaults to false: we do not buffer, so there is
     * never more data to read in one go. */
}
#endif /* #if (SIMULATOR != 1) */


void setup()
{
    debug_init();

    debug_printf("\r\n\n");
    debug_printf("***********************************************************\r\n");
    debug_printf("*                        OhEzTouch                        *\r\n");
    debug_printf("***********************************************************\r\n");
    debug_printf("\r\nTarget:     %s\r\n", TARGET_NAME);
    debug_printf("Version:    %u.%02u\r\n", VERSION_MAJOR, VERSION_MINOR);
    debug_printf("GIT Hash:   %s\r\n", VERSION_GIT_HASH);
    debug_printf("Build Time: %s %s\r\n\r\n",  __DATE__, __TIME__);

    config.setup();
    config.loadConfig("/config.json");

    lv_init();

#if LV_USE_LOG != 0
    lv_log_register_print_cb(my_print); // register print function for debugging
#endif

#ifdef BEEPER_PIN
    beeper_setup(BEEPER_PIN);

    if (config.item.beeper.enabled == true)
        beeper_enable();
#endif

#if (SIMULATOR != 1)
    tft_backlight.setDimTimeout(config.item.backlight.activity_timeout);
    tft_backlight.setNormalBrightness(config.item.backlight.normal_brightness);
    tft_backlight.setDimBrightness(config.item.backlight.dim_brightness);
    tft_backlight.setup(TFT_BACKLIGHT_PIN, TFT_BACKLIGHT_INVERT);

    tft.begin();        // TFT init
    tft.fillScreen(TFT_PINK);
    tft.setRotation(3); // Landscape orientation
    tft.invertDisplay(false);
#endif

    /* Register the tick source before anything can ask LVGL for the time. */
    lv_tick_set_cb(ui_tick_get);

    // Initialize the display
#if (SIMULATOR != 1)
    lv_display_t *disp = lv_display_create(screenWidth, screenHeight);
    lv_display_set_flush_cb(disp, my_disp_flush);
    lv_display_set_buffers(disp, draw_buf, NULL, sizeof(draw_buf),
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
#else // SIMULATOR
    lv_display_t *disp = lv_sdl_window_create(screenWidth, screenHeight);
    lv_sdl_window_set_zoom(disp, 2.0f);   // was -D SDL_ZOOM=2
    lv_sdl_window_set_title(disp, "OhEzTouch");
#endif

#if (SIMULATOR != 1)
#if (TOUCH_DRIVER_FT6X36 == 1)
    if (!touch_ft6x36.begin(Wire, FT6X36_SLAVE_ADDRESS, TOUCH_FT6X36_SDA, TOUCH_FT6X36_SCL))
    {
        debug_printf("Failed to find FT6X36 - check your wiring!");
    }
#else
    // Initialize input device touch
    uint16_t calData[5] = {275, 3620, 264, 3532, 1};
    tft.setTouch(calData);
#endif
#endif

#if (SIMULATOR != 1)
    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER); // Touch pad is a pointer-like device
    lv_indev_set_read_cb(indev, my_touchpad_read);
    lv_indev_set_display(indev, disp);
#else // SIMULATOR
    lv_sdl_mouse_create();
#endif
    /* The hand-forked v7 theme is gone; the project styles its own widgets and
     * only needs sane defaults underneath. */
    lv_display_set_theme(disp, lv_theme_simple_init(disp));

    /* Before the first widget of any kind, and before openhab_ui_setup(): the
     * info label below is created on the top layer while WLAN is still coming
     * up, and it draws on the shared styles rather than a private one of its
     * own. lv_screen_active() is valid from the lv_display_create() above,
     * which is what ui_style_init() needs to style the screen itself. */
    ui_style_select(config.item.ui.theme, openhab_ui_night_active(&config));
    ui_style_init();

#if (SIMULATOR != 1)
    infolabel.create(infolabel.INFO, "WLAN", "Connecting...", 0);
    lv_timer_handler();

    /* First step of taking the WLAN over from AutoConnect: prove that the
     * credentials it stored can be read back, while it is still the one
     * connecting. Reads only -- nothing adopts the result yet. The radio has
     * to be up first, because the SDK's own station config is one of the two
     * places wlan_credentials_import() looks. */
    {
        char ssid[WLAN_SSID_SIZE];
        char psk[WLAN_PSK_SIZE];

        WiFi.mode(WIFI_STA);

        if (wlan_credentials_get(ssid, sizeof(ssid), psk, sizeof(psk)) == true)
            debug_printf("wlan: own credentials for '%s' (%u byte key)\r\n",
                         ssid, (unsigned)strlen(psk));
        else if (wlan_credentials_import(ssid, sizeof(ssid), psk, sizeof(psk)) == true)
            debug_printf("wlan: importable credentials for '%s' (%u byte key)\r\n",
                         ssid, (unsigned)strlen(psk));
        else
            debug_printf("wlan: no credentials stored\r\n");
    }

    ac_main_setup(&config);

    WiFi.setSleep(false);
#endif

#if USE_ARDUINO_BASIC_OTA
    basic_ota_setup();
#endif

    openhab_ui_setup(&config);

#if (SIMULATOR != 1)
    openhab_sensor_main_setup(config);
#else // SIMULATOR
    openhab_ui_connect(config.item.openhab.hostname, config.item.openhab.port, config.item.openhab.sitemap);
#endif
}

void loop()
{
#if (SIMULATOR == 1)
    lv_timer_handler(); // let the GUI do its work
    openhab_ui_loop();
    SDL_Delay(5);
#else
    tft_backlight.loop();
    lv_timer_handler(); // let the GUI do its work
    ac_main_loop();
    infolabel.loop();

    static wl_status_t wlan_status = WL_NO_SHIELD;
    static unsigned long offline_timestamp = 0;

    if (WiFi.status() != wlan_status)
    {
#if DEBUG_WLAN_STATES
        debug_printf("WiFi: state change: %u -> %u\r\n", wlan_status, WiFi.status());
#endif
        wlan_status = WiFi.status();

        if (wlan_status == WL_CONNECTED)
        {
#if DEBUG_WLAN_STATES
            Serial.println("WiFi: WL_CONNECTED");
#endif
            infolabel.destroy();
            openhab_ui_set_wifi_state(true);
            openhab_ui_connect(config.item.openhab.hostname, config.item.openhab.port, config.item.openhab.sitemap);
            infolabel.create(infolabel.INFO, "WLAN", "CONNECTED!", 3);
        }
        else if (wlan_status == WL_IDLE_STATUS)
        {
            // required by AutoConnect
#if DEBUG_WLAN_STATES
            Serial.println("WiFi: WL_IDLE_STATUS");
#endif
            infolabel.create(infolabel.WARNING, "WLAN", "IDLE", 0);
            lv_timer_handler();
            delay(1000);

            ESP.restart();
            delay(1000);
        }
        else
        {
#if DEBUG_WLAN_STATES
            Serial.println("WiFi: WLAN NOT CONNECTED");
#endif
            openhab_ui_set_wifi_state(false);
            infolabel.create(infolabel.WARNING, "WLAN", "NOT CONNECTED", 0);
            offline_timestamp = millis();
        }
    }

    if (wlan_status == WL_CONNECTED)
    {
        openhab_ui_loop();
        openhab_sensor_main_loop(config);

#if USE_ARDUINO_BASIC_OTA
        basic_ota_loop();
#endif
    }

    if (wlan_status != WL_CONNECTED && millis() - offline_timestamp >= WLAN_OFFLINE_TIMEOUT)
    {
        offline_timestamp = millis();
#if DEBUG_WLAN_STATES
        Serial.println("WiFi: Offline Timeout. Reconnecting...");
#endif
        infolabel.create(infolabel.INFO, "WLAN", "Reconnecting to AP...", 0);
        lv_timer_handler();

        ac_main_reconnect();
    }
#endif
}
