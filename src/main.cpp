#include <Arduino.h>
#include "lv_conf.h"
#include <lvgl.h>
#include <Ticker.h>
#include <TFT_eSPI.h>
#include "WiFi.h"
#include "esp_wifi.h"
#include "version.h"
#include "ac_main.hpp"
#include "config.hpp"
#include "ota/basic_ota.hpp"
#include "openhab_ui.hpp"
#include "ui_infolabel.hpp"
#include "openhab_sensor_main.hpp"
#include "driver/backlight_control.hpp"
#include "driver/beeper_control.hpp"

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

Ticker tick;               // timer for interrupt handler
TFT_eSPI tft = TFT_eSPI(); // TFT instance

BacklightControl tft_backlight;

static lv_disp_buf_t disp_buf;
static lv_color_t buf[LV_HOR_RES_MAX * 10];

Config config;
Infolabel infolabel;

#if USE_LV_LOG != 0
// Serial debugging
void my_print(lv_log_level_t level, const char *file, uint32_t line, const char *dsc)
{

    Serial.printf("%s@%d->%s\r\n", file, line, dsc);
    delay(100);
}
#endif

// Display flushing
void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p)
{
    uint16_t c;

    tft.startWrite();                                                                            // Start new TFT transaction
    tft.setAddrWindow(area->x1, area->y1, (area->x2 - area->x1 + 1), (area->y2 - area->y1 + 1)); // set the working window
    for (int y = area->y1; y <= area->y2; y++)
    {
        for (int x = area->x1; x <= area->x2; x++)
        {
            c = color_p->full;
            tft.writeColor(c, 1);
            color_p++;
        }
    }
    tft.endWrite();            // terminate TFT transaction
    lv_disp_flush_ready(disp); // tell lvgl that flushing is done
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

    if (touched == true && tft_backlight.resetDimTimeout() == true)
    {
        if (config.item.beeper.enabled == true)
            beeper_playNote(NOTE_C4, 50, 100, 0);
        suppress_touch_timeout = millis() + 200;
        return false;
    }

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
            Serial.printf("DISPLAY_TOUCH x: %u y %u\n", touchX, touchY);
#endif
    }
#if DEBUG_DISPLAY_TOUCH
    else
    {
        if (data->state = touched)
            Serial.printf("DISPLAY_TOUCH outside of expected parameters x: %u y %u\n", touchX, touchY);
    }
#endif

    return false; // Return `false` because we are not buffering and no more data to read
}

void setup()
{
    // Prepare for possible serial debug
    Serial.begin(DEBUG_OUTPUT_BAUDRATE);

    Serial.printf("\r\n\n");
    Serial.printf("***********************************************************\r\n");
    Serial.printf("*                        OhEzTouch                        *\r\n");
    Serial.printf("***********************************************************\r\n");
    Serial.printf("\r\nTarget:     %s\r\n", TARGET_NAME);
    Serial.printf("Version:    %u.%02u\r\n", VERSION_MAJOR, VERSION_MINOR);
    Serial.printf("GIT Hash:   %s\r\n", VERSION_GIT_HASH);
    Serial.printf("Build Time: %s %s\r\n\r\n",  __DATE__, __TIME__);
    config.setup();
    config.loadConfig("/config.json");

    lv_init();

#if USE_LV_LOG != 0
    lv_log_register_print_cb(my_print); // register print function for debugging
#endif

    beeper_setup(BEEPER_PIN);

    if (config.item.beeper.enabled == true)
        beeper_enable();

    tft_backlight.setDimTimeout(config.item.backlight.activity_timeout);
    tft_backlight.setNormalBrightness(config.item.backlight.normal_brightness);
    tft_backlight.setDimBrightness(config.item.backlight.dim_brightness);
    tft_backlight.setup(TFT_BACKLIGHT_PIN);

    tft.begin();        // TFT init
    tft.setRotation(3); // Landscape orientation

    lv_disp_buf_init(&disp_buf, buf, NULL, LV_HOR_RES_MAX * 10);

    // Initialize the display
    lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = screenWidth;
    disp_drv.ver_res = screenHeight;
    disp_drv.flush_cb = my_disp_flush;
    disp_drv.buffer = &disp_buf;
    lv_disp_drv_register(&disp_drv);

    // Initialize the screen
    static lv_style_t style_screen;
    lv_obj_t *scr = lv_cont_create(NULL, NULL);
    lv_style_copy(&style_screen, &lv_style_scr);
    style_screen.body.main_color = LV_COLOR_BLACK;
    style_screen.body.grad_color = LV_COLOR_BLACK;
    lv_obj_set_style(scr, &style_screen);
    lv_disp_load_scr(scr);

    // Initialize input device touch
    uint16_t calData[5] = {275, 3620, 264, 3532, 1};
    tft.setTouch(calData);

    lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);          // Descriptor of a input device driver
    indev_drv.type = LV_INDEV_TYPE_POINTER; // Touch pad is a pointer-like device
    indev_drv.read_cb = my_touchpad_read;   // Set your driver function
    lv_indev_drv_register(&indev_drv);      // Finally register the driver

    // Initialize the graphics library's tick
    tick.attach_ms(LVGL_TICK_PERIOD, lv_tick_handler);

    infolabel.create(infolabel.INFO, "Connecting to WiFi AP...", 0);
    lv_task_handler();

    ac_main_setup(&config);

    WiFi.setSleep(false);

#if USE_ARDUINO_BASIC_OTA
    basic_ota_setup();
#endif

    openhab_ui_setup(&config);
    openhab_sensor_main_setup(config);
}

void loop()
{
    tft_backlight.loop();
    lv_task_handler(); // let the GUI do its work
    ac_main_loop();
    infolabel.loop();

    static wl_status_t wlan_status = WL_NO_SHIELD;
    static unsigned long offline_timestamp = 0;

    if (WiFi.status() != wlan_status)
    {
#if DEBUG_WLAN_STATES
        Serial.printf("WiFi: state change: %u -> %u\r\n", wlan_status, WiFi.status());
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
            infolabel.create(infolabel.INFO, "WLAN CONNECTED!\n IP: " + String(ip[0]) + "." + String(ip[1]) + "." + String(ip[2]) + "." + String(ip[3]), 10);
        }
        else if (wlan_status == WL_IDLE_STATUS)
        {
            // required by AutoConnect
#if DEBUG_WLAN_STATES
            Serial.println("WiFi: WL_IDLE_STATUS");
#endif
            infolabel.create(infolabel.WARNING, "WLAN IDLE", 0);
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
            infolabel.create(infolabel.WARNING, "WLAN NOT CONNECTED", 0);
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
        infolabel.create(infolabel.INFO, " Reconnecting to WiFi AP...", 0);
        lv_task_handler();

        ac_main_reconnect();
    }
}
