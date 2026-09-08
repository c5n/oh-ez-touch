
#ifndef OPENHAB_UI_H
#define OPENHAB_UI_H

#include "config.hpp"
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
void openhab_ui_loop(void);

/* Whether the configured night mode says the night variant applies right now.
 * For UI_NIGHT_AUTO this reads the clock, so it can change between calls. */
bool openhab_ui_night_active(Config *config);

/* Switch the UI to another theme variant without a reboot. The request is
 * recorded here and carried out from openhab_ui_loop(), so this is safe to call
 * from the web configuration handler; a request naming the variant already in
 * effect is dropped. */
void openhab_ui_request_theme(enum ui_theme_family_e family, bool night);

#endif
