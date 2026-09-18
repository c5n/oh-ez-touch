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
 * Everything that touches LVGL runs on this one task. That is deliberate and
 * load-bearing: lv_conf.h sets LV_USE_OS to LV_OS_NONE, so LVGL has no locking
 * of its own, and LVGL's SDL driver pumps SDL from an lv_timer rather than a
 * thread. The moment a second task calls lv_*, both of those have to be
 * revisited.
 *
 * Tasks that are not this one therefore hand work back rather than doing it:
 * the web server and the MQTT client record a request that the owning loop
 * carries out, and the openHAB client task -- which is where every HTTP
 * request to openHAB now waits -- answers on a queue that openhab_ui_loop()
 * drains. None of them calls lv_*. That is the whole of the arrangement, and
 * it is what lets a page load take five seconds without the screen noticing.
 */

#include "sdkconfig.h"

#include <lvgl.h>

#include "config/config.hpp"
#include "debug.h"
#include "config/config_fields.hpp"
#include "version.h"

#include "ble/ble_scan.hpp"
#include "control/backlight_control.hpp"
#include "control/beeper_control.hpp"
#include "mqtt/ohez_mqtt.hpp"
#include "net/wlan.hpp"
#include "openhab/openhab_client.hpp"
#include "openhab/openhab_sitemaps.hpp"
#include "peripherals/led.hpp"
#include "peripherals/relay.hpp"
#include "peripherals/sensor_main.hpp"
#include "port/ohez_port.h"
#include "testif/testif.hpp"
#include "ui/openhab_ui.hpp"
#include "ui/ui_beep.hpp"
#include "ui/ui_frame_probe.h"
#include "ui/ui_messagebox.hpp"
#include "ui/ui_screen.hpp"
#include "ui/ui_settings.hpp"
#include "ui/ui_style.hpp"
#include "web/webui.hpp"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_event.h"
#include "esp_log.h"

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
Messagebox messagebox;

/* Re-apply every setting that does not need a reboot. Declared in
 * config_fields.hpp and called from both save paths -- the web form in
 * webui.cpp and the touch settings screen in ui_settings.cpp -- so that the two
 * agree on what a save actually does. It lives here because this is where the
 * backlight and the beeper are owned.
 *
 * Everything it touches used to be read once in setup() and never again, which
 * is why the openHAB host, the backlight levels and the beeper were effectively
 * restart-only settings without ever saying so. The theme goes through
 * openhab_ui_request_theme(), which only records a request: this is reachable
 * from the web handler, where lv_timer_handler() is not being pumped and
 * nothing may touch LVGL. The MQTT client is asked the same way, and for the
 * same reason -- tearing down a connection and republishing three dozen
 * retained topics is not work for a POST handler.
 *
 * Disabling the beeper does need a call, and the comment here used to say it
 * did not: the claim was that the per-touch blip read the setting live, which
 * was true of that one blip and of no other sound in the firmware. Every
 * BEEPER_EVENT_* went through beeper_play(), which never asked, and
 * beeper_enable() had no off path -- so unchecking the box on a panel that had
 * booted with sound on left every touch, link and error chime playing until
 * the next restart. beeper_set_enabled() is the fix, and beeper_play() is
 * where it is now checked. */
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

    beeper_set_volume((uint8_t)config->item.beeper.volume);
    beeper_set_enabled(config->item.beeper.enabled);
    /* Both gates, because they answer different questions: beeper_control's is
     * "may anything sound", ui_beep's is also what keeps the panel quiet while
     * it is still starting up. A save has to move them together. */
    ui_beep_set_enabled(config->item.beeper.enabled);

    /* Unconditional, and not only when the broker settings changed: the client
     * decides that for itself, because it is the only thing that knows what it
     * was started with. What every save does need is for the settings it
     * publishes to be republished. */
    ohez_mqtt_request_reconfigure();
}

/* Declared by port_indev.h, called from the device's pointer read on every
 * press. Waking a dimmed display is the one thing a touch does that the widget
 * under the finger must not also see. */
extern "C" bool ohez_touch_wake(void)
{
    if (tft_backlight.resetDimTimeout() == false)
        return false;

    /* The theme's wake chime, rather than the hard-coded C4 this used to be:
     * what a gesture sounds like is the theme's business, and whether the
     * beeper is on at all is beeper_play()'s. This is also the one press in
     * the interface that gets no contact tick -- port_indev suppresses the
     * pointer for 200 ms after a wake, so the widget under the finger never
     * sees it, and this chime is the whole of the feedback. */
    BEEPER_EVENT_WAKE();

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
    beeper_set_volume((uint8_t)config.item.beeper.volume);
    beeper_set_enabled(config.item.beeper.enabled);

    tft_backlight.setDimTimeout(config.item.backlight.activity_timeout);
    tft_backlight.setNormalBrightness(config.item.backlight.normal_brightness);
    tft_backlight.setDimBrightness(config.item.backlight.dim_brightness);
    tft_backlight.setup();

    lv_display_t *disp = port_display_init();

    /* Straight after the display and before any widget exists, so the very
     * first frame is counted. It only registers event callbacks; what they are
     * for is in ui_frame_probe.c, and where they come out is the web status
     * page, <prefix>/system/fps and the test interface's `status`. */
    ui_frame_probe_attach(disp);

    port_indev_init(disp);

    /* The hand-forked v7 theme is gone; the project styles its own widgets and
     * only needs sane defaults underneath. */
    lv_display_set_theme(disp, lv_theme_simple_init(disp));

    /* Before the first widget of any kind, and before openhab_ui_setup(): the
     * info label below is created on the top layer while WLAN is still coming
     * up, and it draws on the shared styles rather than a private one of its
     * own. ui_screen_setup() comes first of all, because it is what claims the
     * display's screen as the root and puts the theme's background on it --
     * ui_style_init() no longer touches lv_screen_active() itself. */
    ui_screen_setup();

    ui_style_select(config.item.ui.theme, openhab_ui_night_active(&config));
    ui_style_init();

    wlan_setup(&config);

    /* Only where there is something to wait for. On a host the link is up
     * before the process starts, so announcing it would be a banner that says
     * nothing and then goes away. */
    if (wlan_state() != WLAN_ONLINE)
    {
        messagebox.create(messagebox.INFO, "WLAN", "Connecting...", 0);
        lv_timer_handler();
    }

    webui_setup(&config);

    openhab_ui_setup(&config);
    ui_settings_setup(&config);

    /* Before openhab_ui_connect() below, which is the first thing to submit a
     * request. Starting it while the link is still down is harmless -- it
     * blocks on an empty queue until there is something to fetch. */
    if (openhab_client_setup() == false)
        ESP_LOGE(TAG, "no openHAB client task; the panel will not reach openHAB");

    sensor_main_setup(config);

    /* Before the MQTT client, because that is what they talk through and the
     * only thing they talk through: each claims a subtree of the command tree
     * with ohez_mqtt_subscribe(), and the client asks the broker for them
     * when it connects. No-ops on a board without the hardware, which is
     * every board but the Lanbon L8-HS. */
    relay_setup();
    led_setup();

    /* And the beeper, for the same reason and in the same place: `sound/set`
     * is a subtree of the command tree like `relay/+/set` is, and the module
     * that owns the sounds is the one that claims it. Unlike those two it is
     * on every board -- the Lanbon has no buzzer, but it also has no way to
     * know that here, and port_beeper is where the silence lives. */
    ui_beep_mqtt_setup();

    ohez_mqtt_setup(&config);

    /* After the MQTT client, which is where its findings go, and last of the
     * three because it is the one that claims a radio. */
    ble_scan_setup(config);

    port_ntp_setup(config.item.ntp.hostname, config.item.ntp.gmt_offset * 3600,
                   config.item.ntp.daylightsaving ? 3600 : 0);

    if (wlan_state() == WLAN_ONLINE)
    {
        openhab_ui_set_wifi_state(true);
        openhab_ui_connect(config.item.openhab.hostname, config.item.openhab.port,
                           config.item.openhab.sitemap);
    }

#if CONFIG_IDF_TARGET_LINUX
    openhab_ui_open_item_from_env();
    ui_settings_open_from_env();
#endif

    /* Last, so that a script connecting to it finds a panel that is already
     * set up rather than one still deciding what to show. No-op on the device,
     * which has no control interface. */
    testif_setup();

    /* And after even that, the first sound the panel makes.
     *
     * ui_beep starts muted, and this is the line that unmutes it, which is the
     * whole reason it starts that way: everything raised while the UI was
     * being built -- the "Connecting..." banner at the top of this function
     * among them -- would otherwise have announced itself to the room before
     * the panel was ready to be looked at.
     *
     * A device that came up in portal mode gets this too, and then the setup
     * AP's banner a second later. That is not an oversight: a panel that has
     * lost its credentials wants to be noticed, and the chime followed by the
     * notification is exactly the sequence that says so. */
    ui_beep_set_enabled(config.item.beeper.enabled);
    BEEPER_EVENT_BOOT();
}

/* How long the loop below is allowed to sleep between two calls into LVGL.
 *
 * The floor is one tick: vTaskDelay(0) does not yield to an equal-priority
 * task, so a zero here would spin. The ceiling is what the loops below this
 * one -- the WLAN state machine, the settings screen's scan, the openHAB
 * polling -- are serviced at; they are all deadline-driven at hundreds of
 * milliseconds or more, so 10 ms is far finer than any of them needs and is
 * only there to keep the sleep bounded when LVGL has nothing due at all. */
#define OHEZ_LOOP_DELAY_MIN_MS  1
#define OHEZ_LOOP_DELAY_MAX_MS  10

static void ohez_loop(void)
{
    tft_backlight.loop();

    /* Not simply lv_timer_handler(): a screenshot is streamed straight out of
     * LVGL's own frame buffer rather than copied first, so for the few
     * milliseconds that takes the frame has to stop changing underneath the
     * reader. The hold times out by itself, and on the device the call is a
     * constant false that the compiler folds away. */
    uint32_t sleep_ms = OHEZ_LOOP_DELAY_MIN_MS;

    if (testif_frame_hold() == false)
        sleep_ms = lv_timer_handler(); // let the GUI do its work

    /* Outside the online guard further down, unlike openhab_ui_loop(): the
     * settings screen is how a device with no credentials gets any, so its
     * access point scan has to keep running while the station is offline. */
    ui_settings_loop();

    /* And with it, for the same reason turned around: the list of sitemaps is
     * fetched because somebody opened the openHAB settings, and on a panel
     * that cannot reach its server that has to end in a failure the screen can
     * report rather than in a request nothing ever times out. */
    openhab_sitemaps_loop();
    ui_screen_loop();
    wlan_loop();
    webui_loop();
    testif_loop();
    messagebox.loop();

    /* Seeded with the state at the first call rather than with a "nothing yet"
     * value, so the state a target boots in is not announced as a change. That
     * is what keeps the simulator -- which is online before it starts -- from
     * flashing a "CONNECTED!" banner at nobody, without a guard here saying so.
     *
     * This used to poll WiFi.status(), which is why it was device-only. */
    static enum wlan_state_e reported = wlan_state();

    if (wlan_state() != reported)
    {
#if CONFIG_OHEZ_DEBUG_WLAN_STATES
        printf("WLAN: state change: %u -> %u\r\n", (unsigned)reported,
               (unsigned)wlan_state());
#endif
        bool was_online = (reported == WLAN_ONLINE);

        reported = wlan_state();

        if (reported == WLAN_ONLINE)
        {
            messagebox.destroy();
            openhab_ui_set_wifi_state(true);
            openhab_ui_connect(config.item.openhab.hostname, config.item.openhab.port,
                               config.item.openhab.sitemap);
            messagebox.create(messagebox.INFO, "WLAN", "CONNECTED!", 3);
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
            messagebox.create(messagebox.WARNING, "WLAN", "NOT CONNECTED", 0);

            /* With a restart under it. wlan_loop() will keep trying by itself
             * and usually gets there, but a radio that has stopped answering
             * is one of the two faults this panel cannot talk its way out of,
             * and this is the offer that replaced rebooting unasked. */
            messagebox.offerRestart();
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

            messagebox.create(messagebox.INFO, "Setup", text, 0);
        }
        else if (wlan_state() != WLAN_ONLINE)
        {
            messagebox.create(messagebox.WARNING, "WLAN", "NOT CONNECTED", 0);
            messagebox.offerRestart();
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
        sensor_main_loop(config);

        /* Inside the guard, which is what defers the first connection until
         * there is a network to make it on: esp-mqtt would otherwise spend the
         * whole of the association failing to resolve the broker's name, once a
         * second, with a log line each time. */
        ohez_mqtt_loop(config);

        /* After it, not before: a command is applied from inside that call,
         * and these two are what publish the state it left behind. */
        relay_loop();
        led_loop();

        /* Inside it too, though for a weaker reason: the scanner has somewhere
         * to publish only while the link is up, and scanning with nowhere to
         * send the result would spend radio share the WiFi wants. */
        ble_scan_loop(config);
    }

    /* Was SDL_Delay(5) in the simulator and nothing at all on the device, whose
     * loop was never allowed to yield. vTaskDelay() is what lets the other
     * tasks -- the beeper, and the web server on the device -- run.
     *
     * Sleeping for what lv_timer_handler() asked for rather than a fixed 5 ms.
     * The fixed figure was the wrong shape in both directions: it woke this
     * task 200 times a second to re-run every loop above for nothing, and it
     * still delivered a frame up to 5 ms after LVGL wanted it. Nothing is
     * sampled faster for the spinning, either -- the pointer is read from an
     * lv_timer on the same LV_DEF_REFR_PERIOD as the display, so a touch is
     * seen when lv_timer_handler() runs and not before. What the CPU stops
     * spending here goes to the one thing that is short of it, which is the
     * software renderer.
     *
     * LV_NO_TIMER_READY is the answer when no timer is due at all, which is
     * neither a delay nor a small number; the clamp is what makes it one. */
    if (sleep_ms < OHEZ_LOOP_DELAY_MIN_MS)
        sleep_ms = OHEZ_LOOP_DELAY_MIN_MS;
    else if (sleep_ms > OHEZ_LOOP_DELAY_MAX_MS)
        sleep_ms = OHEZ_LOOP_DELAY_MAX_MS;

    vTaskDelay(pdMS_TO_TICKS(sleep_ms));
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
