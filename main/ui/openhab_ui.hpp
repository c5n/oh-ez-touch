
#ifndef OPENHAB_UI_H
#define OPENHAB_UI_H

#include "config/config.hpp"
#include "ui_theme.hpp"
#include <lvgl.h>
#include <stdint.h>

/* An RSSI in dBm as a percentage, the way the status bar has always shown it.
 * Public because the settings screen's Info tab and its access point scan want
 * the same scale as the header, not one of their own. */
uint8_t openhab_ui_signal_quality(int8_t rssi);

void openhab_ui_setup(Config *config);
void openhab_ui_set_wifi_state(bool wifi_state);
void openhab_ui_connect(const char *host, uint16_t port, const char *sitemap);

/* The same, asked for from a task that does not own the UI -- the web handler.
 * It only records the request; openhab_ui_loop() carries it out. Without this
 * the handler would rewrite the page URL out from under a fetch in progress.
 * Same shape, and the same reason, as openhab_ui_request_theme(). */
void openhab_ui_request_connect(const char *host, uint16_t port, const char *sitemap);
void openhab_ui_loop(void);

#if CONFIG_IDF_TARGET_LINUX
/* Arm OHEZ_ITEM: a dot-separated path of tile indices that the simulator walks
 * once the first page arrives, opening the control it ends on. The sibling of
 * ui_settings_open_from_env(), and there so that a screen three taps deep can
 * be reached from a script. */
void openhab_ui_open_item_from_env(void);
#endif

/* Whether the configured night mode says the night variant applies right now.
 * For UI_NIGHT_AUTO this reads the clock, so it can change between calls. */
bool openhab_ui_night_active(Config *config);

/* Switch the UI to another theme variant without a reboot. The request is
 * recorded here and carried out from openhab_ui_loop(), so this is safe to call
 * from the web configuration handler; a request naming the variant already in
 * effect is dropped. */
void openhab_ui_request_theme(enum ui_theme_family_e family, bool night);

#endif
