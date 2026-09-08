/**
 * The on-device settings screen.
 *
 * Everything the web interface can change, changeable on the panel. The rows of
 * the openHAB, Sensors and Other tabs are not written out here: they are one
 * pass over settings_fields[], the same table webui.cpp renders its form from,
 * so a setting cannot exist in one of the two and not the other. Only the WLAN
 * tab is hand-built, because credentials live in NVS rather than in
 * config.json, and only the Info tab is read-only.
 *
 * Unlike the item windows in openhab_ui.cpp this is a real LVGL screen. The
 * openHAB page keeps living underneath and comes back untouched -- no sitemap
 * refetch, no tile rebuild -- which also means openhab_ui_loop() can carry on
 * updating it while the user is in here.
 *
 * Edits go into a draft copy of Config::item, not into the live one. Closing
 * without saving therefore changes nothing, a half-typed hostname never reaches
 * wlan_setup(), and Save can tell exactly which fields moved -- which is what
 * decides whether a restart is worth offering.
 */

#include "sdkconfig.h"

#include "ui_settings.hpp"

#include "openhab_ui.hpp"
#include "settings_fields.hpp"
#include "ui_beep.hpp"
#include "ui_style.hpp"
#include "ui_theme.hpp"
#include "version.h"
#include "wlan.hpp"
#include "debug.h"
#include "port/port_sys.h"

#if !CONFIG_IDF_TARGET_LINUX
#include <WiFi.h>
#include <uptime.h>
#endif

#include <lvgl.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h> /* strcasecmp() */

#ifndef DEBUG_UI_SETTINGS
#define DEBUG_UI_SETTINGS 0
#endif

/* 320x240 leaves very little room, so the chrome is measured rather than
 * proportional -- window_create()'s vertical/5 header would eat a fifth of the
 * screen. There is no title bar here at all: the tab bar and the footer are as
 * much chrome as 240 px can carry, and a third bar holding only a title and a
 * close button spent a row of settings on what the footer says and does just
 * as well. */
#define TAB_BAR_HEIGHT 32
#define FOOTER_HEIGHT 34
#define ROW_HEIGHT 30

/* Enough for the widest value a row shows: a 31 character text field, or
 * "Activity timeout" as a number. */
#define VALUE_BUFFER_LEN 48

/* More than a home has, and a hard bound on what a scan may return. */
#define SCAN_RESULT_MAX 16

/* How often the WLAN tab's state line is recomputed. It reports a state
 * machine that moves on 20 and 60 second timers, so this only has to be fine
 * enough to look prompt. */
#define WLAN_STATE_REFRESH_INTERVAL 1000

/* ------------------------------------------------------------------- state */

static Config *settings_config = NULL;

/* The edited copy, and the values as they were when the screen opened.
 * settings_restart_needed() compares the two. Static rather than allocated:
 * about 250 bytes against 320 KB of RAM, and no allocation to fail. */
static settings_item_t draft;
static settings_item_t baseline;

static lv_obj_t *screen = NULL;
static lv_obj_t *prev_screen = NULL;
static lv_obj_t *tabview = NULL;

/* One per tab: the scrollable list of rows, and the footer's message label. */
static lv_obj_t *tab_rows[SETTINGS_TAB_COUNT];
static lv_obj_t *tab_status[SETTINGS_TAB_COUNT];

/* The keyboard or the confirmation prompt -- only ever one at a time, and a
 * child of the screen rather than of lv_layer_top(), because open() hides that
 * layer to keep the Infolabel banner off this screen. */
static lv_obj_t *overlay = NULL;
static lv_obj_t *overlay_textarea = NULL;

/* What the keyboard is editing: either a row of settings_fields[] in the draft,
 * or a raw buffer (the WLAN credentials, which are not part of Config). */
static const struct settings_field_s *edit_field = NULL;
static char                          *edit_buffer = NULL;
static size_t                         edit_buffer_size = 0;
static lv_obj_t                      *edit_row = NULL;

/* The WLAN tab. The credentials are edited here and only reach NVS on Save. */
static char wlan_ssid_buf[WLAN_SSID_SIZE];
static char wlan_psk_buf[WLAN_PSK_SIZE];

static lv_obj_t *wlan_state_label = NULL;
static lv_obj_t *wlan_ssid_row = NULL;
static lv_obj_t *wlan_psk_row = NULL;
static lv_obj_t *wlan_scan_list = NULL;

struct scan_result_s
{
    char   ssid[WLAN_SSID_SIZE];
    int8_t rssi;
    bool   encrypted;
};

static struct scan_result_s scan_results[SCAN_RESULT_MAX];
static uint8_t              scan_result_count = 0;
static bool                 scan_running = false;

static uint64_t wlan_state_refresh_deadline = 0;

/* A theme change asked for while an overlay is up. See ui_settings_rebuild(). */
static bool rebuild_pending = false;

/* Symbols, not words: the tab bar is five buttons across 320 px, and the
 * condensed LCARS face at 16 px still cannot fit "openHAB" and "Sensors" into
 * 64 px each. The footer names the active tab instead, which is more legible
 * than a clipped label would be. */
static const char *const tab_symbol[SETTINGS_TAB_COUNT] = {
    LV_SYMBOL_WIFI, LV_SYMBOL_HOME, LV_SYMBOL_EYE_OPEN, LV_SYMBOL_SETTINGS, LV_SYMBOL_LIST};

static const char *const tab_title[SETTINGS_TAB_COUNT] = {
    "WLAN", "openHAB", "Sensors", "Other", "Info"};

static void screen_build(uint8_t tab);
static void wlan_state_update(void);
static void keyboard_cancel_event(lv_event_t *e);

/* Keep the Infolabel banners off this screen.
 *
 * They live on lv_layer_top() so that a page rebuild cannot delete them, and
 * LVGL keeps its layers in the display's screen array -- so the centred "WLAN
 * NOT CONNECTED" warning, which is created with no timeout and never expires,
 * would sit on top of these tabs. Precisely the case that matters most, since
 * the screen opens by itself on a device that has no credentials.
 *
 * The children get the flag rather than the layer: lv_obj_remove_flag() reacts
 * to LV_OBJ_FLAG_HIDDEN by marking the object's *parent* layout dirty, with no
 * NULL check, and a layer has no parent. Hiding one works by luck --
 * lv_obj_add_flag() only touches the parent when the object's participation in
 * a layout changed, which for a parentless layer it never does -- and unhiding
 * it segfaults. A banner is a plain child of the layer and has a parent, so the
 * flag behaves on it.
 *
 * Re-applied from ui_settings_loop() because main.cpp raises a banner on any
 * WLAN state change, including while these tabs are up. lv_obj_add_flag()
 * returns immediately when the flag is already set, so repeating it is free. */
static void banners_hide(bool hidden)
{
    lv_obj_t *top = lv_layer_top();
    uint32_t  count = lv_obj_get_child_count(top);

    for (uint32_t i = 0; i < count; i++)
        lv_obj_set_flag(lv_obj_get_child(top, i), LV_OBJ_FLAG_HIDDEN, hidden);
}
static void row_refresh(lv_obj_t *row, const struct settings_field_s *f);

/* ---------------------------------------------------------------- builders */

/* v9's lv_obj_create() comes with theme background, border, radius, padding and
 * scrolling, none of which a layout container wants. openhab_ui.cpp has the
 * same helper; it is six lines, and neither file's copy is worth exporting. */
static lv_obj_t *plain_container(lv_obj_t *parent)
{
    lv_obj_t *obj = lv_obj_create(parent);

    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_set_style_pad_gap(obj, 0, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_radius(obj, 0, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);

    return obj;
}

static lv_obj_t *button_create(lv_obj_t *parent, const char *text)
{
    lv_obj_t *btn = lv_button_create(parent);

    lv_obj_add_style(btn, &ui_style_btn, LV_PART_MAIN);
    lv_obj_add_style(btn, &ui_style_btn_checked, ui_style_selector(LV_PART_MAIN, LV_STATE_CHECKED));
    lv_obj_set_size(btn, LV_SIZE_CONTENT, LV_SIZE_CONTENT);

    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, text);
    lv_obj_center(label);

    return btn;
}

/* A settings row: full width, the name on the left and the current value on the
 * right. Both a button and a two-column layout, so that the whole row is the
 * touch target -- at 30 px tall there is no room for a separate control. */
static lv_obj_t *row_create(lv_obj_t *parent, const char *name)
{
    lv_obj_t *row = lv_button_create(parent);

    lv_obj_add_style(row, &ui_style_btn, LV_PART_MAIN);
    lv_obj_add_style(row, &ui_style_btn_checked, ui_style_selector(LV_PART_MAIN, LV_STATE_CHECKED));
    lv_obj_set_size(row, lv_pct(100), ROW_HEIGHT);
    lv_obj_set_style_pad_hor(row, 6, 0);
    lv_obj_set_style_pad_ver(row, 0, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *name_label = lv_label_create(row);
    lv_label_set_text(name_label, name);
    lv_label_set_long_mode(name_label, LV_LABEL_LONG_DOT);
    lv_obj_set_flex_grow(name_label, 1);

    lv_obj_t *value_label = lv_label_create(row);
    lv_label_set_text(value_label, "");
    lv_label_set_long_mode(value_label, LV_LABEL_LONG_DOT);
    /* Capped rather than content-sized: a 31 character item name would
     * otherwise push the field's own name off the row entirely. */
    lv_obj_set_style_max_width(value_label, lv_pct(55), 0);

    return row;
}

static void row_set_value(lv_obj_t *row, const char *value)
{
    lv_label_set_text(lv_obj_get_child(row, 1), value);
}

/* --------------------------------------------------------------- the rows */

static void field_value_text(const struct settings_field_s *f, const settings_item_t *item,
                             char *buffer, size_t size)
{
    switch (f->kind)
    {
    case SETTINGS_TEXT:
    {
        const char *text = settings_field_text(f, item);

        /* An empty item name or hostname is a legitimate value, but a blank
         * right-hand column reads as a broken row. */
        snprintf(buffer, size, "%s", (text[0] == '\0') ? "--" : text);
        break;
    }

    case SETTINGS_BOOL:
        snprintf(buffer, size, "%s", settings_field_read(f, item) ? "ON" : "OFF");
        break;

    case SETTINGS_ENUM:
    {
        int32_t index = settings_field_read(f, item);

        if (index < 0 || index >= (int32_t)f->count)
            index = 0;

        snprintf(buffer, size, "%s", f->names[index]);
        break;
    }

    default:
        snprintf(buffer, size, "%ld", (long)settings_field_read(f, item));
        break;
    }
}

static void row_refresh(lv_obj_t *row, const struct settings_field_s *f)
{
    char buffer[VALUE_BUFFER_LEN];

    field_value_text(f, &draft, buffer, sizeof(buffer));
    row_set_value(row, buffer);
}

/* --------------------------------------------------------------- overlays */

static void overlay_close(void)
{
    if (overlay == NULL)
        return;

    lv_obj_delete_async(overlay);
    overlay = NULL;
    overlay_textarea = NULL;
    edit_field = NULL;
    edit_buffer = NULL;
    edit_buffer_size = 0;
    edit_row = NULL;
}

/* Full-screen, opaque and clickable: the tabs below must neither show through
 * nor take a touch that was aimed at the overlay.
 *
 * IGNORE_LAYOUT is what makes "full-screen" true. The screen is a column flex
 * whose one item, the tabview, grows to fill it; without the flag the overlay
 * becomes a second item, laid out *after* the tabview at the bottom edge and
 * 240 px tall from there, so all but its top edge falls off the display. */
static lv_obj_t *overlay_create(void)
{
    overlay_close();

    overlay = lv_obj_create(screen);

    lv_obj_add_flag(overlay, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_remove_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(overlay, 0, 0);
    lv_obj_set_size(overlay, lv_pct(100), lv_pct(100));
    lv_obj_set_style_pad_all(overlay, 4, 0);
    lv_obj_add_style(overlay, &ui_style_window, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_COVER, 0);

    return overlay;
}

static void confirm_restart_event(lv_event_t *e)
{
    LV_UNUSED(e);

#if !CONFIG_IDF_TARGET_LINUX
    ESP.restart();
#else
    overlay_close();
#endif
}

static void confirm_dismiss_event(lv_event_t *e)
{
    LV_UNUSED(e);

    BEEPER_EVENT_WINDOW_CLOSE();
    overlay_close();
}

/* LV_USE_MSGBOX is 0, and stays 0: the v9 msgbox is a full dialog with a
 * header, a footer and a button area, which is what made ui_infolabel.hpp
 * hand-roll its banner too. This is a label and two buttons. */
static void confirm_restart_open(const char *text)
{
    lv_obj_t *root = overlay_create();

    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(root, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    lv_obj_t *label = lv_label_create(root);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(label, lv_pct(96));
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(label, text);

    lv_obj_t *buttons = plain_container(root);
    lv_obj_set_size(buttons, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(buttons, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(buttons, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    lv_obj_add_event_cb(button_create(buttons, "Restart"), confirm_restart_event,
                        LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(button_create(buttons, "Later"), confirm_dismiss_event,
                        LV_EVENT_CLICKED, NULL);
}

/* --------------------------------------------------------- the keyboard */

static void keyboard_ready_event(lv_event_t *e)
{
    LV_UNUSED(e);

    const char *text = lv_textarea_get_text(overlay_textarea);

    if (edit_field != NULL)
    {
        switch (edit_field->kind)
        {
        case SETTINGS_TEXT:
            /* False means the value contained '/' or ':' on a row that forbids
             * them. Dropping it silently is what the web form does; the row
             * keeps its old value, which is visible straight away. */
            settings_field_set_text(edit_field, &draft, text);
            break;

        default:
            settings_field_set_number(edit_field, &draft, strtol(text, NULL, 10));
            break;
        }

        if (edit_row != NULL)
            row_refresh(edit_row, edit_field);
    }
    else if (edit_buffer != NULL)
    {
        strlcpy(edit_buffer, text, edit_buffer_size);

        if (edit_row != NULL)
        {
            /* The passphrase is never shown, here or in the web form. */
            if (edit_buffer == wlan_psk_buf)
                row_set_value(edit_row, (text[0] == '\0') ? "--" : "*****");
            else
                row_set_value(edit_row, (text[0] == '\0') ? "--" : text);
        }
    }

    BEEPER_EVENT_CHANGE();
    overlay_close();
}

static void keyboard_cancel_event(lv_event_t *e)
{
    LV_UNUSED(e);

    BEEPER_EVENT_WINDOW_CLOSE();
    overlay_close();
}

/* Laid out with flex rather than aligned by hand: lv_keyboard's own default is
 * half the parent's height, and any fixed share of 240 px that leaves the four
 * key rows enough finger room also cuts the bottom one off -- which is the row
 * carrying LVGL's OK and cancel keys. Letting flex hand the keyboard whatever
 * is left over cannot clip it.
 *
 * The cancel key LVGL puts in that row is LV_SYMBOL_KEYBOARD, the phone
 * convention for dismissing one; the explicit close button on the title line is
 * for everyone who does not read it that way, and it sits where the finger just
 * came from on the screen's own header. */
static void keyboard_open(const char *title, const char *value, uint32_t max_length,
                          bool numeric, bool password)
{
    lv_obj_t *root = overlay_create();

    lv_obj_set_style_pad_all(root, 3, 0);
    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(root, 3, 0);

    lv_obj_t *title_row = plain_container(root);
    lv_obj_set_size(title_row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(title_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(title_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    lv_obj_t *label = lv_label_create(title_row);
    lv_label_set_text(label, title);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_obj_add_style(label, &ui_style_label, LV_PART_MAIN);
    lv_obj_set_flex_grow(label, 1);

    lv_obj_add_event_cb(button_create(title_row, LV_SYMBOL_CLOSE), keyboard_cancel_event,
                        LV_EVENT_CLICKED, NULL);

    overlay_textarea = lv_textarea_create(root);
    lv_textarea_set_one_line(overlay_textarea, true);
    lv_textarea_set_max_length(overlay_textarea, max_length);
    lv_textarea_set_password_mode(overlay_textarea, password);
    lv_textarea_set_text(overlay_textarea, value);
    /* Without a style of its own the field would be lv_theme_simple's white on
     * the Default variant's white panel -- an invisible input. The row style is
     * the right one to borrow: this is what a row looks like being edited. */
    lv_obj_add_style(overlay_textarea, &ui_style_btn, LV_PART_MAIN);
    lv_obj_set_width(overlay_textarea, lv_pct(100));

    lv_obj_t *keyboard = lv_keyboard_create(root);
    lv_keyboard_set_mode(keyboard, numeric ? LV_KEYBOARD_MODE_NUMBER : LV_KEYBOARD_MODE_TEXT_LOWER);
    lv_keyboard_set_textarea(keyboard, overlay_textarea);
    /* The popover magnifies the pressed key over its neighbours, which is the
     * difference between usable and not at this key size. */
    lv_keyboard_set_popovers(keyboard, true);
    lv_obj_add_style(keyboard, &ui_style_window, LV_PART_MAIN);
    lv_obj_add_style(keyboard, &ui_style_btn, LV_PART_ITEMS);
    lv_obj_set_width(keyboard, lv_pct(100));
    lv_obj_set_flex_grow(keyboard, 1);

    lv_obj_add_event_cb(keyboard, keyboard_ready_event, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(keyboard, keyboard_cancel_event, LV_EVENT_CANCEL, NULL);

    BEEPER_EVENT_WINDOW();
}

static void field_edit_open(lv_obj_t *row, const struct settings_field_s *f)
{
    char buffer[VALUE_BUFFER_LEN];

    if (f->kind == SETTINGS_TEXT)
    {
        snprintf(buffer, sizeof(buffer), "%s", settings_field_text(f, &draft));
        keyboard_open(f->label, buffer, f->size - 1, false, false);
    }
    else
    {
        snprintf(buffer, sizeof(buffer), "%ld", (long)settings_field_read(f, &draft));
        /* Room for the widest bound plus a sign; the value is clamped on
         * commit anyway, so this only stops absurd typing. */
        keyboard_open(f->label, buffer, 10, true, false);
    }

    /* After keyboard_open(): it goes through overlay_create(), which closes any
     * previous overlay and clears exactly these. */
    edit_field = f;
    edit_row = row;
}

static void buffer_edit_open(lv_obj_t *row, const char *title, char *buffer, size_t size,
                             bool password)
{
    keyboard_open(title, password ? "" : buffer, (uint32_t)(size - 1), false, password);

    edit_buffer = buffer;
    edit_buffer_size = size;
    edit_row = row;
}

/* ---------------------------------------------------------- row handlers */

static void field_row_event(lv_event_t *e)
{
    const struct settings_field_s *f =
        (const struct settings_field_s *)lv_event_get_user_data(e);
    lv_obj_t *row = (lv_obj_t *)lv_event_get_current_target(e);

    switch (f->kind)
    {
    case SETTINGS_BOOL:
        settings_field_write(f, &draft, settings_field_read(f, &draft) ? 0 : 1);
        row_refresh(row, f);
        BEEPER_EVENT_CHANGE();
        break;

    case SETTINGS_ENUM:
    {
        /* Cycled rather than picked from a list: every enum in the table has
         * three options, so a dropdown and a roller both stay compiled out. */
        int32_t next = settings_field_read(f, &draft) + 1;

        if (next >= (int32_t)f->count)
            next = 0;

        settings_field_write(f, &draft, next);
        row_refresh(row, f);
        BEEPER_EVENT_CHANGE();
        break;
    }

    default:
        field_edit_open(row, f);
        break;
    }
}

static void wlan_ssid_row_event(lv_event_t *e)
{
    buffer_edit_open((lv_obj_t *)lv_event_get_current_target(e), "Network (SSID)", wlan_ssid_buf,
                     sizeof(wlan_ssid_buf), false);
}

static void wlan_psk_row_event(lv_event_t *e)
{
    buffer_edit_open((lv_obj_t *)lv_event_get_current_target(e), "Password", wlan_psk_buf,
                     sizeof(wlan_psk_buf), true);
}

static void scan_result_event(lv_event_t *e)
{
    size_t index = (size_t)(uintptr_t)lv_event_get_user_data(e);

    if (index >= scan_result_count)
        return;

    strlcpy(wlan_ssid_buf, scan_results[index].ssid, sizeof(wlan_ssid_buf));

    if (wlan_ssid_row != NULL)
        row_set_value(wlan_ssid_row, wlan_ssid_buf);

    if (scan_results[index].encrypted == false)
    {
        /* Nothing left to ask for -- and the passphrase of whatever was
         * selected before must not be carried over to an open network. */
        wlan_psk_buf[0] = '\0';

        if (wlan_psk_row != NULL)
            row_set_value(wlan_psk_row, "--");

        BEEPER_EVENT_CHANGE();
        return;
    }

    /* The passphrase is the only thing still missing, so go straight there
     * rather than making the user find the row. */
    if (wlan_psk_row != NULL)
        buffer_edit_open(wlan_psk_row, "Password", wlan_psk_buf, sizeof(wlan_psk_buf), true);
}

/* -------------------------------------------------------------- the scan */

static void scan_list_rebuild(void)
{
    if (wlan_scan_list == NULL)
        return;

    lv_obj_clean(wlan_scan_list);

    for (size_t i = 0; i < scan_result_count; i++)
    {
        char        value[VALUE_BUFFER_LEN];
        const char *lock = scan_results[i].encrypted ? "" : "  (open)";

        snprintf(value, sizeof(value), "%u %%%s",
                 openhab_ui_signal_quality(scan_results[i].rssi), lock);

        lv_obj_t *row = row_create(wlan_scan_list, scan_results[i].ssid);
        row_set_value(row, value);
        lv_obj_add_event_cb(row, scan_result_event, LV_EVENT_CLICKED, (void *)(uintptr_t)i);
    }
}

static void scan_status_set(const char *text)
{
    if (tab_status[SETTINGS_TAB_WLAN] != NULL)
        lv_label_set_text(tab_status[SETTINGS_TAB_WLAN], text);
}

#if !CONFIG_IDF_TARGET_LINUX
/* Keep the strongest sighting of each name and drop the rest: a mesh reports
 * the same SSID once per radio, which would otherwise fill the whole list. */
static void scan_result_insert(const char *ssid, int8_t rssi, bool encrypted)
{
    if (ssid == NULL || ssid[0] == '\0')
        return;

    for (size_t i = 0; i < scan_result_count; i++)
    {
        if (strcmp(scan_results[i].ssid, ssid) != 0)
            continue;

        if (rssi > scan_results[i].rssi)
        {
            scan_results[i].rssi = rssi;
            scan_results[i].encrypted = encrypted;
        }

        return;
    }

    if (scan_result_count >= SCAN_RESULT_MAX)
        return;

    strlcpy(scan_results[scan_result_count].ssid, ssid, WLAN_SSID_SIZE);
    scan_results[scan_result_count].rssi = rssi;
    scan_results[scan_result_count].encrypted = encrypted;
    scan_result_count++;
}

static void scan_results_sort(void)
{
    /* Insertion sort over at most sixteen entries. */
    for (size_t i = 1; i < scan_result_count; i++)
    {
        struct scan_result_s key = scan_results[i];
        size_t               j = i;

        while (j > 0 && scan_results[j - 1].rssi < key.rssi)
        {
            scan_results[j] = scan_results[j - 1];
            j--;
        }

        scan_results[j] = key;
    }
}
#endif /* #if !CONFIG_IDF_TARGET_LINUX */

static void scan_start(void)
{
#if !CONFIG_IDF_TARGET_LINUX
    if (scan_running == true)
        return;

    /* Asynchronous: a blocking scan takes seconds, during which
     * lv_timer_handler() would not run and the panel would look dead. The
     * connection is briefly interrupted either way, and comes back on its
     * own -- WiFi.setAutoReconnect(true) is set in wlan_setup(). */
    if (WiFi.scanNetworks(true) == WIFI_SCAN_FAILED)
    {
        scan_status_set("Scan failed");
        return;
    }

    scan_running = true;
    scan_status_set("Scanning...");
#else
    /* No radio on the host. A canned list, in the spirit of
     * sim/sitemap_fixture.cpp, so the tab can be laid out and clicked through
     * in the simulator. */
    static const struct scan_result_s canned[] = {
        {"FRITZ!Box 7590", -42, true},
        {"oheztouch-lab", -55, true},
        {"Nachbar-WLAN", -71, true},
        {"Gastnetz", -78, false},
        {"a-very-long-network-name-here", -88, true},
    };

    scan_result_count = 0;

    for (size_t i = 0; i < sizeof(canned) / sizeof(canned[0]); i++)
        scan_results[scan_result_count++] = canned[i];

    scan_list_rebuild();
    scan_status_set("5 networks");
#endif

    BEEPER_EVENT_CHANGE();
}

static void scan_poll(void)
{
#if !CONFIG_IDF_TARGET_LINUX
    if (scan_running == false)
        return;

    int16_t found = WiFi.scanComplete();

    /* WIFI_SCAN_RUNNING until it is done; a negative value that is not that
     * means the scan failed and there is nothing to collect. */
    if (found == WIFI_SCAN_RUNNING)
        return;

    scan_running = false;
    scan_result_count = 0;

    if (found < 0)
    {
        scan_status_set("Scan failed");
        WiFi.scanDelete();
        return;
    }

    for (int16_t i = 0; i < found; i++)
        scan_result_insert(WiFi.SSID(i).c_str(), (int8_t)WiFi.RSSI(i),
                           WiFi.encryptionType(i) != WIFI_AUTH_OPEN);

    WiFi.scanDelete();
    scan_results_sort();
    scan_list_rebuild();

    char text[VALUE_BUFFER_LEN];
    snprintf(text, sizeof(text), "%u network%s", (unsigned)scan_result_count,
             (scan_result_count == 1) ? "" : "s");
    scan_status_set(text);
#endif
}

static void scan_event(lv_event_t *e)
{
    LV_UNUSED(e);
    scan_start();
}

/* ------------------------------------------------------------------- save */

static void status_set(uint8_t tab, const char *text)
{
    if (tab < SETTINGS_TAB_COUNT && tab_status[tab] != NULL)
        lv_label_set_text(tab_status[tab], text);
}

static void save_event(lv_event_t *e)
{
    uint8_t     tab = (uint8_t)(uintptr_t)lv_event_get_user_data(e);
    const char *restart_label = NULL;
    bool        restart_needed = settings_restart_needed(&baseline, &draft, &restart_label);

    settings_config->item = draft;

    bool stored = settings_config->saveConfig();

    /* Whether or not the file was written, the draft is now what the running
     * firmware believes, so the next save must not offer a restart for a change
     * this one already applied. */
    baseline = draft;

    settings_apply_live(settings_config);

    if (stored == false)
    {
        /* settings_apply_live() has already run, so the values are in effect --
         * they just will not survive a reboot. saveConfig() returns false when
         * no config file was ever loaded, and always on the simulator, which
         * has no filesystem. No restart is offered either: a reboot is exactly
         * what would lose them. */
        status_set(tab, "Applied, not saved");
        BEEPER_EVENT_ERROR();
        return;
    }

    status_set(tab, "Saved");
    BEEPER_EVENT_CHANGE();

#if DEBUG_UI_SETTINGS
    debug_printf("ui_settings: saved, restart needed: %d\r\n", (int)restart_needed);
#endif

    if (restart_needed == true)
    {
        char text[120];

        snprintf(text, sizeof(text),
                 "\"%s\" is only read while the device boots.\n\nRestart now?",
                 restart_label);
        confirm_restart_open(text);
    }
}

static void wlan_save_event(lv_event_t *e)
{
    LV_UNUSED(e);

#if !CONFIG_IDF_TARGET_LINUX
    if (wlan_set_credentials(wlan_ssid_buf, wlan_psk_buf) == false)
    {
        status_set(SETTINGS_TAB_WLAN, "Needs an SSID");
        BEEPER_EVENT_ERROR();
        return;
    }

    status_set(SETTINGS_TAB_WLAN, "Connecting...");
    BEEPER_EVENT_CHANGE();
    wlan_state_update();
#else
    status_set(SETTINGS_TAB_WLAN, "No radio (sim)");
#endif
}

static void restart_event(lv_event_t *e)
{
    LV_UNUSED(e);

    BEEPER_EVENT_WINDOW();
    confirm_restart_open("Restart the device now?");
}

/* -------------------------------------------------------------- WLAN tab */

static void wlan_state_update(void)
{
    if (wlan_state_label == NULL)
        return;

    char text[120];

#if !CONFIG_IDF_TARGET_LINUX
    const char *state;

    switch (wlan_state())
    {
    case WLAN_ONLINE:
        state = "online";
        break;
    case WLAN_CONNECTING:
        state = "connecting";
        break;
    case WLAN_RETRY_WAIT:
        state = "offline, retrying";
        break;
    case WLAN_PORTAL:
        state = "not configured";
        break;
    default:
        state = "idle";
        break;
    }

    /* One line where it can be: this label competes with the scan results for
     * about five visible rows, and the RSSI and the rest of the addresses are
     * one tab away on Info. */
    if (wlan_state() == WLAN_ONLINE)
        snprintf(text, sizeof(text), "%s: %s  %s", state, wlan_sta_ssid(),
                 WiFi.localIP().toString().c_str());
    else
        snprintf(text, sizeof(text), "%s: %s", state, wlan_sta_ssid());

    /* The setup access point is the only route in on a pristine device, and
     * the banner that used to say so is hidden while this screen is up. */
    const char *ap = wlan_ap_ssid();

    if (ap != NULL)
    {
        uint32_t ip = wlan_ap_ip();

        snprintf(text + strlen(text), sizeof(text) - strlen(text),
                 "\nsetup AP %s  %u.%u.%u.%u", ap, (unsigned)(ip & 0xFF),
                 (unsigned)((ip >> 8) & 0xFF), (unsigned)((ip >> 16) & 0xFF),
                 (unsigned)((ip >> 24) & 0xFF));
    }
#else
    snprintf(text, sizeof(text), "no radio (simulator)");
#endif

    lv_label_set_text(wlan_state_label, text);
}

static void wlan_tab_build(lv_obj_t *rows)
{
    wlan_state_label = lv_label_create(rows);
    lv_label_set_long_mode(wlan_state_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(wlan_state_label, lv_pct(100));
    lv_obj_add_style(wlan_state_label, &ui_style_label_state, LV_PART_MAIN);
    wlan_state_update();

    wlan_ssid_row = row_create(rows, "Network (SSID)");
    row_set_value(wlan_ssid_row, (wlan_ssid_buf[0] == '\0') ? "--" : wlan_ssid_buf);
    lv_obj_add_event_cb(wlan_ssid_row, wlan_ssid_row_event, LV_EVENT_CLICKED, NULL);

    wlan_psk_row = row_create(rows, "Password");
    row_set_value(wlan_psk_row, (wlan_psk_buf[0] == '\0') ? "--" : "*****");
    lv_obj_add_event_cb(wlan_psk_row, wlan_psk_row_event, LV_EVENT_CLICKED, NULL);

    wlan_scan_list = plain_container(rows);
    lv_obj_set_size(wlan_scan_list, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(wlan_scan_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(wlan_scan_list, 2, 0);

    scan_list_rebuild();
}

/* -------------------------------------------------------------- Info tab */

/* One row of the Info table. Returns the next free row, so the table's height
 * follows what was actually added: the eleven rows the openHAB page used to ask
 * for were eleven whether or not the network rows were compiled in, which left
 * the simulator with a blank first row and nine empty grey stripes. */
static uint16_t info_row(lv_obj_t *table, uint16_t row, const char *name, const char *value)
{
    lv_table_set_cell_value(table, row, 0, name);
    lv_table_set_cell_value(table, row, 1, value);

    return row + 1;
}

static void info_tab_build(lv_obj_t *rows)
{
    /* Moved here from openhab_ui.cpp's header_event_handler(), which is what
     * the status bar used to open on its own. */
    lv_obj_t *table = lv_table_create(rows);

    lv_obj_add_style(table, &ui_style_table_cell, LV_PART_ITEMS);
    /* Eleven rows do not fit 240 px, so the scrollbar is on screen and needs a
     * colour of the theme's rather than lv_theme_simple's grey. The slider's
     * indicator colour is the right one to borrow: a scrollbar thumb is the
     * same idea, and for the Default theme it happens to be the very grey
     * lv_theme_simple was supplying. */
    lv_obj_set_style_bg_color(table, lv_color_hex(ui_style_theme()->slider_indic.bg),
                              LV_PART_SCROLLBAR);
    lv_table_set_column_count(table, 2);

    int32_t table_width = lv_display_get_horizontal_resolution(NULL) - 10;
    lv_table_set_column_width(table, 0, table_width * 30 / 100);
    lv_table_set_column_width(table, 1, table_width * 70 / 100);
    lv_obj_set_size(table, lv_pct(100), lv_pct(100));

    char     buffer[50];
    uint16_t row = 0;

#if !CONFIG_IDF_TARGET_LINUX
    uptime::calculateUptime();
    snprintf(buffer, sizeof(buffer), "%lu days, %luh %lum %lus", uptime::getDays(),
             uptime::getHours(), uptime::getMinutes(), uptime::getSeconds());
    row = info_row(table, row, "Uptime", buffer);
#endif

    snprintf(buffer, sizeof(buffer), "%u.%02u (%s %s)", VERSION_MAJOR, VERSION_MINOR, __DATE__,
             __TIME__);
    row = info_row(table, row, "Version", buffer);

#if !CONFIG_IDF_TARGET_LINUX
    row = info_row(table, row, "Hostname", WiFi.getHostname());
    row = info_row(table, row, "SSID", WiFi.SSID().c_str());
    row = info_row(table, row, "BSSID", WiFi.BSSIDstr().c_str());

    int8_t signal_rssi = WiFi.RSSI();

    snprintf(buffer, sizeof(buffer), "%i dBm (%u %%)", signal_rssi,
             openhab_ui_signal_quality(signal_rssi));
    row = info_row(table, row, "RSSI", buffer);

    row = info_row(table, row, "MAC", WiFi.macAddress().c_str());
    row = info_row(table, row, "IP Addr.", WiFi.localIP().toString().c_str());
    row = info_row(table, row, "Mask", WiFi.subnetMask().toString().c_str());
    row = info_row(table, row, "Gateway", WiFi.gatewayIP().toString().c_str());
    row = info_row(table, row, "DNS", WiFi.dnsIP().toString().c_str());
#endif

    lv_table_set_row_count(table, row);
}

/* ------------------------------------------------------------ the screen */

static void tab_changed_event(lv_event_t *e)
{
    LV_UNUSED(e);

    uint32_t tab = lv_tabview_get_tab_active(tabview);

    /* Leaving a tab and coming back clears what the last action reported. A
     * message stays as long as the user is looking at the tab that caused it,
     * which is the whole of its useful life. */
    if (tab < SETTINGS_TAB_COUNT && tab_status[tab] != NULL)
        lv_label_set_text(tab_status[tab], tab_title[tab]);
}

static void close_event(lv_event_t *e)
{
    LV_UNUSED(e);
    ui_settings_close();
}

/* One tab page: a scrollable list of rows over a footer that stays put. A
 * footer inside the scroll area would scroll away with the rows, and Save is
 * the one control that has to be reachable at all times. */
static lv_obj_t *tab_page_build(uint8_t tab)
{
    lv_obj_t *page = lv_tabview_add_tab(tabview, tab_symbol[tab]);

    lv_obj_remove_flag(page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(page, 0, 0);
    lv_obj_set_style_pad_gap(page, 0, 0);
    lv_obj_set_style_border_width(page, 0, 0);
    lv_obj_set_style_radius(page, 0, 0);
    lv_obj_set_style_bg_opa(page, LV_OPA_TRANSP, 0);
    lv_obj_set_flex_flow(page, LV_FLEX_FLOW_COLUMN);

    lv_obj_t *rows = plain_container(page);
    lv_obj_set_width(rows, lv_pct(100));
    lv_obj_set_flex_grow(rows, 1);
    lv_obj_add_flag(rows, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(rows, LV_DIR_VER);
    lv_obj_set_flex_flow(rows, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(rows, 3, 0);
    lv_obj_set_style_pad_row(rows, 3, 0);
    lv_obj_set_style_bg_color(rows, lv_color_hex(ui_style_theme()->slider_indic.bg),
                              LV_PART_SCROLLBAR);

    tab_rows[tab] = rows;

    /* The header's style, not a transparent container: the row the list is
     * clipped at otherwise runs straight into the buttons, which reads as the
     * footer floating over the list rather than ending it. */
    lv_obj_t *footer = lv_obj_create(page);
    lv_obj_remove_flag(footer, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(footer, lv_pct(100), FOOTER_HEIGHT);
    lv_obj_add_style(footer, &ui_style_win_header, LV_PART_MAIN);
    lv_obj_set_style_radius(footer, 0, 0);
    lv_obj_set_flex_flow(footer, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(footer, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_hor(footer, 5, 0);
    lv_obj_set_style_pad_ver(footer, 0, 0);
    lv_obj_set_style_pad_column(footer, 4, 0);

    /* First child, so close sits in the same place on all five tabs and never
     * lands where Save was a moment ago -- the footer's other buttons differ
     * per tab, this one must not appear to move. */
    lv_obj_add_event_cb(button_create(footer, LV_SYMBOL_CLOSE), close_event, LV_EVENT_CLICKED,
                        NULL);

    /* At rest this names the tab, which is the job the title bar used to do for
     * the symbol-only tab buttons; an action's result replaces it until the
     * user leaves the tab. */
    tab_status[tab] = lv_label_create(footer);
    lv_label_set_text(tab_status[tab], tab_title[tab]);
    lv_label_set_long_mode(tab_status[tab], LV_LABEL_LONG_DOT);
    lv_obj_set_flex_grow(tab_status[tab], 1);
    /* DOT only writes its dots in the last line that still fits the height, so
     * at LV_SIZE_CONTENT it never dots at all: it wraps, grows a second line,
     * and the longest message ("Applied, not saved") spills out of a bar that
     * neither scrolls nor clips. One line of the header font is the height
     * that makes DOT do what it is here for. */
    lv_obj_set_height(tab_status[tab], lv_font_get_line_height(ui_style_theme()->font_normal));

    if (tab == SETTINGS_TAB_INFO)
    {
        lv_obj_add_event_cb(button_create(footer, "Restart"), restart_event, LV_EVENT_CLICKED,
                            NULL);
    }
    else if (tab == SETTINGS_TAB_WLAN)
    {
        lv_obj_add_event_cb(button_create(footer, "Scan"), scan_event, LV_EVENT_CLICKED, NULL);
        lv_obj_add_event_cb(button_create(footer, "Save"), wlan_save_event, LV_EVENT_CLICKED,
                            NULL);
    }
    else
    {
        lv_obj_add_event_cb(button_create(footer, "Save"), save_event, LV_EVENT_CLICKED,
                            (void *)(uintptr_t)tab);
    }

    /* Appends the LCARS end cap, so it has to follow the buttons. This is the
     * bar that used to be the header, and it keeps the header's decoration. */
    ui_style_decorate_window(footer);

    return rows;
}

/* A section heading only earns one of the five or so visible lines where the tab
 * holds more than one section: "Sensors" above the only group of the Sensors tab
 * says nothing the tab bar has not already said. */
static uint8_t tab_section_count(uint8_t tab)
{
    uint8_t count = 0;

    for (size_t i = 0; i < settings_field_count; i++)
        if (settings_fields[i].kind == SETTINGS_SECTION && settings_field_tab(i) == tab)
            count++;

    return count;
}

/* The openHAB, Sensors and Other tabs, straight off the shared table. */
static void field_rows_build(uint8_t tab)
{
    lv_obj_t *rows = tab_rows[tab];
    bool      headings = tab_section_count(tab) > 1;

    for (size_t i = 0; i < settings_field_count; i++)
    {
        const struct settings_field_s *f = &settings_fields[i];

        if (settings_field_tab(i) != tab)
            continue;

        if (f->kind == SETTINGS_SECTION)
        {
            if (headings == true)
            {
                lv_obj_t *heading = lv_label_create(rows);

                lv_label_set_text(heading, f->label);
                /* The caption font, not the state font: a 22 px heading costs
                 * most of a row, and this list has about five. */
                lv_obj_add_style(heading, &ui_style_label, LV_PART_MAIN);
                lv_obj_set_style_pad_top(heading, 2, 0);
            }

            continue;
        }

        lv_obj_t *row = row_create(rows, f->label);

        row_refresh(row, f);
        lv_obj_add_event_cb(row, field_row_event, LV_EVENT_CLICKED, (void *)f);
    }
}

static void screen_build(uint8_t tab)
{
    lv_obj_clean(screen);

    for (uint8_t i = 0; i < SETTINGS_TAB_COUNT; i++)
    {
        tab_rows[i] = NULL;
        tab_status[i] = NULL;
    }

    wlan_state_label = NULL;
    wlan_ssid_row = NULL;
    wlan_psk_row = NULL;
    wlan_scan_list = NULL;

    lv_obj_set_flex_flow(screen, LV_FLEX_FLOW_COLUMN);

    tabview = lv_tabview_create(screen);
    lv_tabview_set_tab_bar_size(tabview, TAB_BAR_HEIGHT);
    lv_obj_set_width(tabview, lv_pct(100));
    lv_obj_set_flex_grow(tabview, 1);
    lv_obj_add_style(tabview, &ui_style_window, LV_PART_MAIN);

    /* Both of lv_tabview's own containers are plain lv_objs and would keep
     * lv_theme_simple's white behind everything. */
    lv_obj_t *bar = lv_tabview_get_tab_bar(tabview);
    lv_obj_add_style(bar, &ui_style_window, LV_PART_MAIN);
    lv_obj_set_style_pad_all(bar, 1, 0);
    lv_obj_set_style_pad_column(bar, 1, 0);

    lv_obj_t *content = lv_tabview_get_content(tabview);
    lv_obj_add_style(content, &ui_style_window, LV_PART_MAIN);
    lv_obj_set_style_pad_all(content, 0, 0);

    for (uint8_t i = 0; i < SETTINGS_TAB_COUNT; i++)
        tab_page_build(i);

    /* The tab buttons are real lv_buttons in v9, one per tab, and the active
     * one carries LV_STATE_CHECKED -- so the theme's own button styles mark
     * the selection with no extra work. */
    for (uint8_t i = 0; i < SETTINGS_TAB_COUNT; i++)
    {
        lv_obj_t *button = lv_tabview_get_tab_button(tabview, i);

        if (button == NULL)
            continue;

        lv_obj_add_style(button, &ui_style_btn, LV_PART_MAIN);
        lv_obj_add_style(button, &ui_style_btn_checked,
                         ui_style_selector(LV_PART_MAIN, LV_STATE_CHECKED));
    }

    wlan_tab_build(tab_rows[SETTINGS_TAB_WLAN]);
    field_rows_build(SETTINGS_TAB_OPENHAB);
    field_rows_build(SETTINGS_TAB_SENSORS);
    field_rows_build(SETTINGS_TAB_OTHER);
    info_tab_build(tab_rows[SETTINGS_TAB_INFO]);

    lv_obj_add_event_cb(tabview, tab_changed_event, LV_EVENT_VALUE_CHANGED, NULL);

    lv_tabview_set_active(tabview, tab, LV_ANIM_OFF);
}

/* ------------------------------------------------------------------- API */

void ui_settings_setup(Config *config)
{
    settings_config = config;
}

void ui_settings_open(enum settings_tab_e tab)
{
    if (settings_config == NULL || tab >= SETTINGS_TAB_COUNT)
        return;

    if (screen != NULL)
    {
        /* Already up: treat this as a request for that tab, the way the single
         * open_window slot in openhab_ui.cpp stops a second window stacking. */
        lv_tabview_set_active(tabview, tab, LV_ANIM_OFF);
        return;
    }

    draft = settings_config->item;
    baseline = draft;

    wlan_ssid_buf[0] = '\0';
    wlan_psk_buf[0] = '\0';
#if !CONFIG_IDF_TARGET_LINUX
    /* The passphrase is deliberately left blank rather than prefilled, as in
     * the web form -- it is never shown back to anyone. */
    char stored_psk[WLAN_PSK_SIZE];
    wlan_credentials_get(wlan_ssid_buf, sizeof(wlan_ssid_buf), stored_psk, sizeof(stored_psk));
#endif

    scan_result_count = 0;
    scan_running = false;

    prev_screen = lv_screen_active();

    screen = lv_obj_create(NULL);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_style(screen, &ui_style_screen, LV_PART_MAIN);
    lv_obj_set_style_pad_all(screen, 0, 0);
    lv_obj_set_style_pad_gap(screen, 0, 0);

    screen_build(tab);

    banners_hide(true);

    lv_screen_load(screen);

#if DEBUG_UI_SETTINGS
    debug_printf("ui_settings: opened on tab %u\r\n", (unsigned)tab);
#endif
}

void ui_settings_close(void)
{
    if (screen == NULL)
        return;

    overlay_close();

    banners_hide(false);

    lv_screen_load(prev_screen);

    /* Deferred: this is reached from an event on one of the screen's own
     * descendants, the same reason window_close_event_handler() defers. */
    lv_obj_delete_async(screen);

    screen = NULL;
    tabview = NULL;
    rebuild_pending = false;
    wlan_state_label = NULL;
    wlan_ssid_row = NULL;
    wlan_psk_row = NULL;
    wlan_scan_list = NULL;

    for (uint8_t i = 0; i < SETTINGS_TAB_COUNT; i++)
    {
        tab_rows[i] = NULL;
        tab_status[i] = NULL;
    }

    BEEPER_EVENT_WINDOW_CLOSE();
}

bool ui_settings_is_open(void)
{
    return screen != NULL;
}

void ui_settings_rebuild(void)
{
    if (screen == NULL)
        return;

    /* Not while the keyboard or the restart prompt is up. A theme change is not
     * always something the user just asked for: the automatic night schedule
     * requests one from openhab_ui_loop() whenever the clock crosses its
     * boundary, which can land in the middle of typing a passphrase or on top
     * of an unanswered "restart now?". Rebuilding then would throw either away.
     *
     * ui_settings_loop() picks this up once the overlay is gone. It runs from
     * loop() right after lv_timer_handler(), so by then the overlay's deferred
     * deletion has actually happened and there is nothing left to clean. */
    if (overlay != NULL)
    {
        rebuild_pending = true;
        return;
    }

    rebuild_pending = false;
    screen_build((uint8_t)lv_tabview_get_tab_active(tabview));
}

#if CONFIG_IDF_TARGET_LINUX
void ui_settings_open_from_env(void)
{
    const char *name = getenv("OHEZ_SETTINGS");

    if (name == NULL)
        return;

    for (uint8_t i = 0; i < SETTINGS_TAB_COUNT; i++)
    {
        if (strcasecmp(name, tab_title[i]) != 0)
            continue;

        ui_settings_open((enum settings_tab_e)i);
        return;
    }

    printf("ui_settings: OHEZ_SETTINGS=%s names no tab\r\n", name);
}
#endif

void ui_settings_loop(void)
{
    if (screen == NULL)
        return;

    /* A banner raised since the screen opened has to be caught too. */
    banners_hide(true);

    if (rebuild_pending == true && overlay == NULL)
        ui_settings_rebuild();

    scan_poll();

    if (port_millis() >= wlan_state_refresh_deadline)
    {
        wlan_state_refresh_deadline = port_millis() + WLAN_STATE_REFRESH_INTERVAL;
        wlan_state_update();
    }
}
