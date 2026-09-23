
#ifndef OPENHAB_UI_H
#define OPENHAB_UI_H

#include "config/config.hpp"
#include "openhab/openhab_connector.hpp"
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

#if CONFIG_IDF_TARGET_LINUX || CONFIG_OHEZ_TESTIF
/* The walk on demand, for the control interface's `nav`. False when the
 * path is empty or longer than one will ever legitimately be. The walk itself
 * is asynchronous -- each step waits for the page the one before it asked for
 * -- so a true return means "started", not "arrived". */
bool openhab_ui_open_item_path(const char *path);
#endif

#if CONFIG_IDF_TARGET_LINUX
/* Arm OHEZ_ITEM: a dot-separated path of tile indices that the simulator walks
 * once the first page arrives, opening the control it ends on. The sibling of
 * ui_settings_open_from_env(), and there so that a screen three taps deep can
 * be reached from a script. */
void openhab_ui_open_item_from_env(void);
#endif

/* What the tile page is showing.
 *
 * These exist for the simulator's control interface (main/testif/), which
 * cannot describe the screen without them: everything below was, and still is,
 * file-static in openhab_ui.cpp. They read state and nothing more -- no call
 * here changes what is on screen.
 *
 * openhab_ui_page_state_name() is the one a script waits on. It is the fetch
 * cycle the tiles come out of -- "idle", "request", "waiting", "ready" -- and
 * acting on a page before it says "ready" is the obvious way to write a test
 * that passes on a fast machine and fails on a slow one. */
const char *openhab_ui_page_title(void);
const char *openhab_ui_page_state_name(void);
uint32_t openhab_ui_page_generation(void);

/* One tile, as a script sees it. The rectangle is in panel pixels, which is
 * what makes tapping a tile by its label possible without hard-coding the grid
 * that a layout change is free to move. */
struct openhab_ui_tile_s
{
    const char   *label;
    const char   *state;
    enum ItemType type;
    int32_t       x;
    int32_t       y;
    int32_t       w;
    int32_t       h;
};

/* How many tiles the page has built, and one of them. False for an index that
 * is not on screen, so a caller can walk until it stops rather than having to
 * agree with this file about the maximum. */
size_t openhab_ui_tile_count(void);
bool openhab_ui_tile_info(size_t index, struct openhab_ui_tile_s *out);

/* Whether the configured night mode says the night variant applies right now.
 * For UI_NIGHT_AUTO this reads the clock, so it can change between calls. */
bool openhab_ui_night_active(Config *config);

/* Switch the UI to another theme variant without a reboot. The request is
 * recorded here and carried out from openhab_ui_loop(), so this is safe to call
 * from the web configuration handler; a request naming the variant already in
 * effect is dropped. */
void openhab_ui_request_theme(enum ui_theme_family_e family, bool night);

#endif
