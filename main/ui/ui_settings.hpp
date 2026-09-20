#ifndef UI_SETTINGS_HPP
#define UI_SETTINGS_HPP

#include "sdkconfig.h"

#include "config/config.hpp"
#include "config/config_fields.hpp"

/* The on-device settings screen: what the web interface offers, on the panel.
 *
 * It is a real LVGL screen rather than another full-size overlay on the openHAB
 * page, so that page survives untouched underneath and comes back exactly as it
 * was. Reached by tapping the status bar -- which used to open the Systeminfo
 * window, now the Info tab of this -- and opened by itself at boot on a device
 * that has no WLAN credentials yet, which is the case that could previously
 * only be resolved with a second device and a browser. */

void ui_settings_setup(Config *config);

/* Opens on the section named, or on the root menu when passed
 * SETTINGS_TAB_COUNT -- which is what "settings" with nothing more specific
 * in mind means. The sections are spread over two menus; ui_settings.cpp says
 * which, and opening one directly skips straight past them. */
void ui_settings_open(enum settings_tab_e tab);
void ui_settings_close(void);
bool ui_settings_is_open(void);

/* Which page the screen is showing, by the same name OHEZ_SETTINGS takes --
 * a section ("WLAN", "Theme") or a menu ("Settings", "System"). NULL when the
 * screen is closed. For the control interface's screen dump: "the settings are
 * open" is not enough to tell a test which page it is looking at. */
const char *ui_settings_page_name(void);

/* Rebuild the screen in place, on whatever page it is showing. For a theme
 * change: the shared styles carry the colours and fonts by themselves, but the
 * bars and the keyboard have local styles set at creation, and the Info
 * table's cell values are a snapshot. Called from openhab_ui.cpp's
 * theme_apply_pending(), next to where it rebuilds the tile page for the same
 * reason. */
void ui_settings_rebuild(void);

/* Polls the asynchronous access point scan and refreshes the WLAN state line.
 * Called unconditionally from loop(), not only while the station is connected:
 * provisioning happens while offline, which is the whole point. */
void ui_settings_loop(void);

#if CONFIG_IDF_TARGET_LINUX
/* Open the screen at boot, on the page OHEZ_SETTINGS names -- any section
 * (wlan, openhab, mqtt, sensors, device, time, theme, audio, info) or either menu
 * (settings, which "index" also names, and system); anything else, including
 * an unset variable, leaves it closed. On the device the screen is reached by
 * tapping the status bar, or comes up by itself when there are no
 * credentials, and the host has neither a status bar worth tapping nor a
 * radio:
 *
 *   OHEZ_SETTINGS=wlan ./build/linux/oh-ez-touch.elf
 *
 * Same idea as OHEZ_THEME and OHEZ_NIGHT, for which see config.cpp. */
void ui_settings_open_from_env(void);

/* The lookup behind it, by the same names, for the control interface's
 * `settings` command -- so that a script and OHEZ_SETTINGS reach the same
 * pages by the same spellings rather than drifting apart. False when nothing
 * is called that. */
bool ui_settings_open_by_name(const char *name);
#endif

#endif // UI_SETTINGS_HPP
