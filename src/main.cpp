#include <Arduino.h>
//#include "lv_conf.h"
#include <lvgl.h>
#include "version.h"
#include "config.hpp"
#include "openhab_ui.hpp"
#include "ui_infolabel.hpp"
#include "debug.h"

#if (SIMULATOR == 0)
#include <TFT_eSPI.h>
#include <Ticker.h>
#include "esp_wifi.h"
#include "ac_main.hpp"
#include "WiFi.h"
#include "ota/basic_ota.hpp"
#include "driver/backlight_control.hpp"
#include "driver/beeper_control.hpp"
#include "openhab_sensor_main.hpp"
#else
#include <unistd.h>
#define SDL_MAIN_HANDLED        /*To fix SDL's "undefined reference to WinMain" issue*/
#include <SDL2/SDL.h>
//#include "display/monitor.h"
#include "indev/mouse.h"
#include "indev/mousewheel.h"
#include "indev/keyboard.h"
#include "sdl/sdl.h"
#endif
#include "themes/custom_theme_default.h"

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

#ifndef LVGL_TICK_PERIOD
#define LVGL_TICK_PERIOD 20
#endif

#ifndef TFT_BACKLIGHT_PIN
#define TFT_BACKLIGHT_PIN 15
#endif

#ifndef TFT_TOUCH_FLIP
#define TFT_TOUCH_FLIP 0
#endif

#ifndef BEEPER_PIN
#define BEEPER_PIN 21
#endif

#ifndef WLAN_OFFLINE_TIMEOUT
#define WLAN_OFFLINE_TIMEOUT (2 * 60 * 1000)
#endif

int screenWidth = 320;
int screenHeight = 240;

#if (SIMULATOR != 1)
Ticker tick;               // timer for interrupt handler
BacklightControl tft_backlight;
TFT_eSPI tft = TFT_eSPI(); // TFT instance
#endif



static lv_disp_buf_t disp_buf;
static lv_color_t buf[LV_HOR_RES_MAX * 10];

Config config;
Infolabel infolabel;

#if USE_LV_LOG != 0
// Serial debugging
void my_print(lv_log_level_t level, const char *file, uint32_t line, const char *dsc)
{

    debug_printf("%s@%d->%s\r\n", file, line, dsc);
    delay(100);
}
#endif

#if (SIMULATOR != 1)
// Display flushing
void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p)
{
    uint32_t w = (area->x2 - area->x1 + 1);
    uint32_t h = (area->y2 - area->y1 + 1);

    tft.startWrite();
    tft.setAddrWindow(area->x1, area->y1, w, h);
    tft.pushColors(&color_p->full, w * h, true);
    tft.endWrite();

    lv_disp_flush_ready(disp);
}

// Interrupt driven periodic handler
static void lv_tick_handler(void)
{
    lv_tick_inc(LVGL_TICK_PERIOD);
}

bool my_touchpad_read(lv_indev_drv_t *indev_driver, lv_indev_data_t *data)
{
    static unsigned long suppress_touch_timeout;
    static lv_coord_t last_x = 0;
    static lv_coord_t last_y = 0;

    uint16_t touchX, touchY;

    bool touched = tft.getTouch(&touchX, &touchY, 350);

#if TFT_TOUCH_FLIP
    touchX = screenWidth - touchX;
    touchY = screenHeight - touchY;
#endif

    if (suppress_touch_timeout > millis())
    {
        return false;
    }

#if (SIMULATOR != 1)
    if (touched == true && tft_backlight.resetDimTimeout() == true)
    {
        if (config.item.beeper.enabled == true)
            beeper_playNote(NOTE_C4, 50, 100, 0);
        suppress_touch_timeout = millis() + 200;
        return false;
    }
#endif

    if (touchX <= screenWidth || touchY <= screenHeight)
    {
        if (data->state == LV_INDEV_STATE_REL && touched == true)
        {
        }

        data->state = touched ? LV_INDEV_STATE_PR : LV_INDEV_STATE_REL;

        // Save the state and save the pressed coordinate
        if (data->state == LV_INDEV_STATE_PR)
        {
            last_x = touchX;
            last_y = touchY;
        }

        // Set the coordinates (if released use the last pressed coordinates)
        data->point.x = last_x;
        data->point.y = last_y;
#if DEBUG_DISPLAY_TOUCH
        if (data->state = touched)
            debug_printf("DISPLAY_TOUCH x: %u y %u\r\n", touchX, touchY);
#endif
    }
#if DEBUG_DISPLAY_TOUCH
    else
    {
        if (data->state = touched)
            debug_printf("DISPLAY_TOUCH outside of expected parameters x: %u y %u\r\n", touchX, touchY);
    }
#endif

    return false; // Return `false` because we are not buffering and no more data to read
}
#endif /* #if (SIMULATOR != 1) */

#if (SIMULATOR == 1)
static int tick_thread(void * data)
{
    (void)data;

    while(1) {
        SDL_Delay(5);   /*Sleep for 5 millisecond*/
        lv_tick_inc(5); /*Tell LittelvGL that 5 milliseconds were elapsed*/
    }

    return 0;
}
#endif



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

#if USE_LV_LOG != 0
    lv_log_register_print_cb(my_print); // register print function for debugging
#endif

#if (SIMULATOR != 1)
    beeper_setup(BEEPER_PIN);

    if (config.item.beeper.enabled == true)
        beeper_enable();

    tft_backlight.setDimTimeout(config.item.backlight.activity_timeout);
    tft_backlight.setNormalBrightness(config.item.backlight.normal_brightness);
    tft_backlight.setDimBrightness(config.item.backlight.dim_brightness);
    tft_backlight.setup(TFT_BACKLIGHT_PIN);

    tft.begin();        // TFT init
    tft.fillScreen(TFT_PINK);
    tft.setRotation(3); // Landscape orientation
#endif


    lv_disp_buf_init(&disp_buf, buf, NULL, LV_HOR_RES_MAX * 10);

    // Initialize the display
    lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = screenWidth;
    disp_drv.ver_res = screenHeight;
#if (SIMULATOR != 1)
    disp_drv.flush_cb = my_disp_flush;
#else // SIMULATOR
    disp_drv.flush_cb = sdl_display_flush;
#endif
    disp_drv.buffer = &disp_buf;
    lv_disp_drv_register(&disp_drv);

#if (SIMULATOR != 1)
    // Initialize input device touch
    uint16_t calData[5] = {275, 3620, 264, 3532, 1};
    tft.setTouch(calData);
#endif

    lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);          // Descriptor of a input device driver
    indev_drv.type = LV_INDEV_TYPE_POINTER; // Touch pad is a pointer-like device
#if (SIMULATOR != 1)
    indev_drv.read_cb = my_touchpad_read;   // Set your driver function
#else // SIMULATOR
    indev_drv.read_cb = sdl_mouse_read;
#endif
    lv_indev_drv_register(&indev_drv);      // Finally register the driver

#if (SIMULATOR != 1)
    // Initialize the graphics library's tick
    tick.attach_ms(LVGL_TICK_PERIOD, lv_tick_handler);
#else // SIMULATOR
    sdl_init();
    //SDL_CreateThread(tick_thread, "tick", NULL);
#endif
    lv_theme_t * th = custom_theme_default_init(LV_THEME_DEFAULT_COLOR_PRIMARY, LV_THEME_DEFAULT_COLOR_SECONDARY, LV_THEME_DEFAULT_FLAG, LV_THEME_DEFAULT_FONT_SMALL , LV_THEME_DEFAULT_FONT_NORMAL, LV_THEME_DEFAULT_FONT_SUBTITLE, LV_THEME_DEFAULT_FONT_TITLE);
    lv_theme_set_act(th);

    // Initialize the screen
    lv_obj_t *scr = lv_cont_create(NULL, NULL);
    lv_disp_load_scr(scr);

#if (SIMULATOR != 1)
    infolabel.create(infolabel.INFO, "WLAN", "Connecting...", 0);
    lv_task_handler();

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
    lv_task_handler(); // let the GUI do its work
    openhab_ui_loop();
    SDL_Delay(5);
#else
    tft_backlight.loop();
    lv_task_handler(); // let the GUI do its work
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
            IPAddress ip = WiFi.localIP();
            infolabel.create(infolabel.INFO, "WLAN", "CONNECTED!", 3);
        }
        else if (wlan_status == WL_IDLE_STATUS)
        {
            // required by AutoConnect
#if DEBUG_WLAN_STATES
            Serial.println("WiFi: WL_IDLE_STATUS");
#endif
            infolabel.create(infolabel.WARNING, "WLAN", "IDLE", 0);
            lv_task_handler();
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

    if (wlan_status != WL_CONNECTED && millis() > offline_timestamp + WLAN_OFFLINE_TIMEOUT)
    {
        offline_timestamp = millis();
#if DEBUG_WLAN_STATES
        Serial.println("WiFi: Offline Timeout. Reconnecting...");
#endif
        infolabel.create(infolabel.INFO, "WLAN", "Reconnecting to AP...", 0);
        lv_task_handler();

        ac_main_reconnect();
    }
#endif
}
