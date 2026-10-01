#ifndef UI_SETTINGS_HPP
#define UI_SETTINGS_HPP

#include "sdkconfig.h"

#include <lvgl.h>

#include "config/config.hpp"
#include "config/config_fields.hpp"
#include "port/touch_cal.h"

/* The on-device settings screen: what the web interface offers, on the panel.
 *
 * It is a real LVGL screen rather than another full-size overlay on the openHAB
 * page, so that page survives untouched underneath and comes back exactly as it
 * was. Reached by tapping the status bar -- which used to open the Systeminfo
 * window, now the Systeminfo page of this -- and opened by itself at boot on a device
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
 * page's lines are a snapshot. Called from openhab_ui.cpp's
 * theme_apply_pending(), next to where it rebuilds the tile page for the same
 * reason. */
void ui_settings_rebuild(void);

/* Polls the asynchronous access point scan and refreshes the WLAN state line.
 * Called unconditionally from loop(), not only while the station is connected:
 * provisioning happens while offline, which is the whole point. */
void ui_settings_loop(void);

/* Back to the root menu if a page behind the System PIN is showing, committing
 * it the way any navigation away does. What ui_pin calls when an unlock lapses;
 * nothing at all when the settings are closed or on an unprotected page. */
void ui_settings_leave_protected(void);

/* The screen's single overlay slot: full-screen, opaque and click-eating, the
 * one the keyboard and the restart confirmation are built in.
 *
 * Exposed for the calibration flow in ui_calibration.cpp, which needs the whole
 * screen and cannot have a pushed one -- ui_screen allows exactly one screen
 * over the root and the settings screen is already it. A second call replaces
 * whatever was in the slot, so a caller never has to check.
 *
 * NULL when the settings screen is not open. */
lv_obj_t *ui_settings_overlay(void);
void      ui_settings_overlay_dismiss(void);

/* Store a calibration the user has accepted: into the draft, into the live
 * config, to the file, and into the pointer.
 *
 * Its own transaction rather than a change to the draft alone, unlike every
 * other row on this screen. The pointer has to pick the numbers up at once,
 * on the one page where the reason the user came is that tapping is not
 * working properly -- and waiting for the page to be left and committed would
 * leave a "Keep" that nothing kept. Other pending edits in the draft are left
 * alone. */
void ui_settings_touch_cal_keep(const struct touch_cal_s *cal);

#if CONFIG_IDF_TARGET_LINUX || CONFIG_OHEZ_TESTIF
/* The lookup behind OHEZ_SETTINGS and the control interface's `settings`
 * command, by the same names -- so that a script and OHEZ_SETTINGS reach the
 * same pages by the same spellings rather than drifting apart. False when
 * nothing is called that. */
bool ui_settings_open_by_name(const char *name);
#endif

#if CONFIG_IDF_TARGET_LINUX
/* Open the screen at boot, on the page OHEZ_SETTINGS names -- any section
 * (wlan, openhab, mqtt, sensors, device, touch, time, theme, audio,
 * systeminfo, fonts, icons) or either menu
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
#endif

#endif // UI_SETTINGS_HPP
