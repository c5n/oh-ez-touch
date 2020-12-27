
#ifndef OPENHAB_UI_H
#define OPENHAB_UI_H

#include "config.hpp"
#include <lvgl.h>

void openhab_ui_setup(Config *config);
void openhab_ui_set_wifi_state(bool wifi_state);
void openhab_ui_connect(const char *host, int port, const char *sitemap);
void openhab_ui_loop(void);

#endif
