/**
 * @file main.cpp
 *
 * The entry point, the two globals the UI hangs off, and the setting-application
 * policy both save paths share.
 *
 * ESP-IDF calls app_main() on both targets -- FreeRTOS's linux port supplies a
 * main() that starts the scheduler and calls it, exactly as the device port does
 * -- so the Arduino `setup()`/`loop()` pair and the main() that drove it from
 * hal/sdl2 are gone, and there is one entry point for the device and the
 * simulator.
 *
 * Everything runs on this one task. That is deliberate and load-bearing:
 * lv_conf.h sets LV_USE_OS to LV_OS_NONE, so LVGL has no locking of its own, and
 * LVGL's SDL driver pumps SDL from an lv_timer rather than a thread. The moment
 * a second task calls lv_*, both of those have to be revisited.
 */

#include "sdkconfig.h"

#include <lvgl.h>

#include "config.hpp"
#include "debug.h"
#include "port/ohez_port.h"
#include "version.h"

#include "openhab_ui.hpp"
#include "settings_fields.hpp"
#include "ui_infolabel.hpp"
#include "ui_settings.hpp"
#include "ui_style.hpp"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_event.h"
#include "esp_log.h"

#include "driver/backlight_control.hpp"
#include "driver/beeper_control.hpp"

#include "port/port_ntp.h"
#include "wlan.hpp"

#include "openhab_sensor_main.hpp"
#include "webui.hpp"

#ifndef DEBUG_WLAN_STATES
#define DEBUG_WLAN_STATES 0
#endif

/* Set by the top-level CMakeLists.txt. The fallback is what a build that has
 * not been told which board it is for reports, rather than failing to compile:
 * the device side becomes the Kconfig board choice when it runs on IDF
 * drivers. */
#ifndef TARGET_NAME
#define TARGET_NAME "unknown"
#endif

/* The config file, by the bare name port_storage takes: SPIFFS on the device,
 * the host's config directory in the simulator. It used to be "/config.json",
 * with the leading slash naming the SPIFFS mount point. */
#define OHEZ_CONFIG_FILE "config.json"

static const char *TAG = "ohez";

/* Both of these are shared now: the pin behind them is port_backlight and
 * port_beeper, and the parts here -- when to dim, and the queue of notes --
 * run on the host too, where they drive nothing but are at least exercised. */
BacklightControl tft_backlight;

Config config;
Infolabel infolabel;

/* Re-apply every setting that does not need a reboot. Declared in
 * settings_fields.hpp and called from both save paths -- the web form in
 * webui.cpp and the touch settings screen in ui_settings.cpp -- so that the two
 * agree on what a save actually does. It lives here because this is where the
 * backlight and the beeper are owned.
 *
 * Everything it touches used to be read once in setup() and never again, which
 * is why the openHAB host, the backlight levels and the beeper were effectively
 * restart-only settings without ever saying so. The theme goes through
 * openhab_ui_request_theme(), which only records a request: this is reachable
 * from the web handler, where lv_timer_handler() is not being pumped and
 * nothing may touch LVGL. Disabling the beeper needs no call at all -- the
 * per-touch blip reads config.item.beeper.enabled live. */
void settings_apply_live(Config *config)
{
    openhab_ui_request_theme(config->item.ui.theme, openhab_ui_night_active(config));

    /* A request, not a call: this is reached from the web handler, which runs
     * on the server's task, and openhab_ui_connect() rewrites the page URL that
     * the UI task may be fetching from at that moment. */
    openhab_ui_request_connect(config->item.openhab.hostname, config->item.openhab.port,
                               config->item.openhab.sitemap);

    tft_backlight.setDimTimeout(config->item.backlight.activity_timeout);
    tft_backlight.setNormalBrightness(config->item.backlight.normal_brightness);
    tft_backlight.setDimBrightness(config->item.backlight.dim_brightness);

    if (config->item.beeper.enabled == true)
        beeper_enable();
}

/* Declared by port_indev.h, called from the device's pointer read on every
 * press. Waking a dimmed display is the one thing a touch does that the widget
 * under the finger must not also see. */
extern "C" bool ohez_touch_wake(void)
{
    if (tft_backlight.resetDimTimeout() == false)
        return false;

    if (config.item.beeper.enabled == true)
        beeper_playNote(NOTE_C4, 50, 100, 0);

    return true;
}

#if LV_USE_LOG != 0
/* LVGL's own diagnostics. lv_conf.h sets LV_LOG_PRINTF to 0 and routes them
 * here instead: printf() from more than one task is documented-unsafe on the
 * FreeRTOS POSIX simulator, and esp_log also gives the lines a tag and a level
 * so they can be filtered like everything else. */
static void my_print(lv_log_level_t level, const char *buf)
{
    LV_UNUSED(level);
    ESP_LOGI("lvgl", "%s", buf);
}
#endif

static void ohez_setup(void)
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

    /* Unconditional, because DEBUG is off in every build and the banner above
     * is therefore compiled out: without this there is no way to tell which
     * firmware is running. */
    ESP_LOGI(TAG, "OhEzTouch %u.%02u (%s) on %s",
             VERSION_MAJOR, VERSION_MINOR, VERSION_GIT_HASH, TARGET_NAME);

    /* esp_http_client publishes its progress to the default event loop, and
     * logs an error per request when there is none -- three of them per icon.
     * Nothing here subscribes; the loop exists so the publisher has somewhere
     * to publish. esp_wifi and esp_netif will want it too. */
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* The other store. It holds the WLAN credentials, and nothing else opens
     * it: without this wlan_credentials_get() finds nothing on a device that
     * has been provisioned, which reads as "not configured" and raises the
     * setup access point on every boot. */
    esp_err_t kv = port_kv_init();

    if (kv != ESP_OK)
        ESP_LOGE(TAG, "no credential store: %s", esp_err_to_name(kv));

    if (config.setup() == false)
    {
        /* Keep going: loadConfig() below fills in the built-in defaults, so the
         * panel comes up usable and says what is wrong instead of not coming up
         * at all. Saving will fail until the store works. */
        ESP_LOGW(TAG, "no config store; settings cannot be saved");
    }

    config.loadConfig(OHEZ_CONFIG_FILE);

    lv_init();

#if LV_USE_LOG != 0
    lv_log_register_print_cb(my_print);
#endif

    /* Before anything can ask LVGL for the time. port_tick_ms() replaces both
     * the v7 Ticker ISR on the device and the SDL driver's tick thread, so both
     * targets read the same monotonic clock. */
    lv_tick_set_cb(port_tick_ms);

    beeper_setup();

    if (config.item.beeper.enabled == true)
        beeper_enable();

    tft_backlight.setDimTimeout(config.item.backlight.activity_timeout);
    tft_backlight.setNormalBrightness(config.item.backlight.normal_brightness);
    tft_backlight.setDimBrightness(config.item.backlight.dim_brightness);
    tft_backlight.setup();

    lv_display_t *disp = port_display_init();

    port_indev_init(disp);

    /* The hand-forked v7 theme is gone; the project styles its own widgets and
     * only needs sane defaults underneath. */
    lv_display_set_theme(disp, lv_theme_simple_init(disp));

    /* Before the first widget of any kind, and before openhab_ui_setup(): the
     * info label below is created on the top layer while WLAN is still coming
     * up, and it draws on the shared styles rather than a private one of its
     * own. lv_screen_active() is valid from port_display_init() onwards, which
     * is what ui_style_init() needs to style the screen itself. */
    ui_style_select(config.item.ui.theme, openhab_ui_night_active(&config));
    ui_style_init();

    wlan_setup(&config);

    /* Only where there is something to wait for. On a host the link is up
     * before the process starts, so announcing it would be a banner that says
     * nothing and then goes away. */
    if (wlan_state() != WLAN_ONLINE)
    {
        infolabel.create(infolabel.INFO, "WLAN", "Connecting...", 0);
        lv_timer_handler();
    }

    webui_setup(&config);

    openhab_ui_setup(&config);
    ui_settings_setup(&config);

    openhab_sensor_main_setup(config);

    port_ntp_setup(config.item.ntp.hostname, config.item.ntp.gmt_offset * 3600,
                   config.item.ntp.daylightsaving ? 3600 : 0);

    if (wlan_state() == WLAN_ONLINE)
    {
        openhab_ui_set_wifi_state(true);
        openhab_ui_connect(config.item.openhab.hostname, config.item.openhab.port,
                           config.item.openhab.sitemap);
    }

#if CONFIG_IDF_TARGET_LINUX
    ui_settings_open_from_env();
#endif
}

static void ohez_loop(void)
{
    tft_backlight.loop();

    lv_timer_handler(); // let the GUI do its work

    /* Outside the online guard further down, unlike openhab_ui_loop(): the
     * settings screen is how a device with no credentials gets any, so its
     * access point scan has to keep running while the station is offline. */
    ui_settings_loop();
    wlan_loop();
    webui_loop();
    infolabel.loop();

    /* Seeded with the state at the first call rather than with a "nothing yet"
     * value, so the state a target boots in is not announced as a change. That
     * is what keeps the simulator -- which is online before it starts -- from
     * flashing a "CONNECTED!" banner at nobody, without a guard here saying so.
     *
     * This used to poll WiFi.status(), which is why it was device-only. */
    static enum wlan_state_e reported = wlan_state();

    if (wlan_state() != reported)
    {
#if DEBUG_WLAN_STATES
        printf("WLAN: state change: %u -> %u\r\n", (unsigned)reported,
               (unsigned)wlan_state());
#endif
        bool was_online = (reported == WLAN_ONLINE);

        reported = wlan_state();

        if (reported == WLAN_ONLINE)
        {
            infolabel.destroy();
            openhab_ui_set_wifi_state(true);
            openhab_ui_connect(config.item.openhab.hostname, config.item.openhab.port,
                               config.item.openhab.sitemap);
            infolabel.create(infolabel.INFO, "WLAN", "CONNECTED!", 3);
        }
        else if (was_online == true || reported == WLAN_RETRY_WAIT)
        {
            /* There used to be an idle branch here that rebooted the device,
             * because once AutoConnect's blocking begin() had given up nothing
             * would ever start another attempt. wlan_loop() has a retry timer,
             * so idle is now just a state we leave on a later tick -- and a
             * momentary idle report can no longer reboot the device in the
             * middle of an OTA upload. */
            openhab_ui_set_wifi_state(false);
            infolabel.create(infolabel.WARNING, "WLAN", "NOT CONNECTED", 0);
        }
    }

    /* The setup access point is raised a little after the station gives up, so
     * it needs a transition of its own rather than riding on the one above.
     * This is the on-screen half of provisioning: with no captive portal there
     * is nowhere else to learn the address from. Comparing the pointer is
     * enough -- wlan_ap_ssid() returns either NULL or the one hostname. */
    static const char *reported_ap = NULL;

    if (wlan_ap_ssid() != reported_ap)
    {
        reported_ap = wlan_ap_ssid();

        if (reported_ap != NULL)
        {
            uint32_t ip = wlan_ap_ip();
            char     text[80];

            snprintf(text, sizeof(text), "AP %s\nhttp://%u.%u.%u.%u", reported_ap,
                     (unsigned)(ip & 0xFF), (unsigned)((ip >> 8) & 0xFF),
                     (unsigned)((ip >> 16) & 0xFF), (unsigned)((ip >> 24) & 0xFF));

            infolabel.create(infolabel.INFO, "Setup", text, 0);
        }
        else if (wlan_state() != WLAN_ONLINE)
        {
            infolabel.create(infolabel.WARNING, "WLAN", "NOT CONNECTED", 0);
        }
    }

    /* A pristine device: WLAN_PORTAL means no credentials are stored at all, so
     * there is nothing to retry and nobody is coming to fix it. Bring up the
     * settings screen on the WLAN tab, which is the only thing anyone can
     * usefully do with the panel in that state. Once, so that closing it is
     * respected -- and only for WLAN_PORTAL, never for a device that is merely
     * offline and will reconnect by itself. */
    static bool settings_shown_for_portal = false;

    if (settings_shown_for_portal == false && wlan_state() == WLAN_PORTAL)
    {
        settings_shown_for_portal = true;
        ui_settings_open(SETTINGS_TAB_WLAN);
    }

    if (wlan_state() == WLAN_ONLINE)
    {
        openhab_ui_loop();
        openhab_sensor_main_loop(config);
    }

    /* Was SDL_Delay(5) in the simulator and nothing at all on the device, whose
     * loop was never allowed to yield. vTaskDelay() is what lets the other
     * tasks -- the beeper, and the web server on the device -- run. */
    vTaskDelay(pdMS_TO_TICKS(5));
}

extern "C" void app_main(void)
{
    ohez_setup();

    /* Does not return, on either target. On the linux one that is a hard
     * requirement rather than a style: FreeRTOS's POSIX port calls
     * vTaskDelete(NULL) on the main task once app_main() returns, and that
     * trips an assertion in vTaskSwitchContext(). */
    for (;;)
        ohez_loop();
}
