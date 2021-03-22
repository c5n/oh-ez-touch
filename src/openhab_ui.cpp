#include "openhab_ui.hpp"
#include "openhab_connector.hpp"
#include "ui_infolabel.hpp"
#include "driver/beeper_control.hpp"

#include "lodepng/lodepng.h"
#include "WiFi.h"
#include "time.h"
#include "uptime.h"
#include "version.h"

#include <HTTPClient.h>
#include <lvgl.h>

#ifndef DEBUG_OPENHAB_UI
#define DEBUG_OPENHAB_UI 0
#endif

#ifndef WIDGET_COUNT_MAX
#define WIDGET_COUNT_MAX 6
#endif

#ifndef ITEM_UPDATE_INTERVAL
#define ITEM_UPDATE_INTERVAL 5000
#endif

#ifndef GET_SITEMAP_RETRY_INTERVAL
#define GET_SITEMAP_RETRY_INTERVAL 5000
#endif

#ifndef NTP_TIME_UPDATE_INTERVAL
#define NTP_TIME_UPDATE_INTERVAL (60 * 60 * 1000)
#endif

#ifndef CONNECTION_ERROR_TIMEOUT_S
#define CONNECTION_ERROR_TIMEOUT_S 180
#endif

#define HEADER_SIGNAL_UPDATE_INTERVAL 5000

#define ICON_PNG_BUFFER_SIZE 5000

#ifndef BEEPER_VOLUME
#define BEEPER_VOLUME 50
#endif

#define BEEPER_EVENT_CHANGE()              \
    {                                      \
        beeper_playNote(NOTE_C7, BEEPER_VOLUME, 5, 0); \
    }
#define BEEPER_EVENT_LINK()                  \
    {                                        \
        beeper_playNote(NOTE_C7, BEEPER_VOLUME, 20, 10); \
        beeper_playNote(NOTE_E7, BEEPER_VOLUME, 10, 0);  \
    }
#define BEEPER_EVENT_LINK_BACK()            \
    {                                       \
        beeper_playNote(NOTE_E7, BEEPER_VOLUME, 10, 5); \
        beeper_playNote(NOTE_C7, BEEPER_VOLUME, 10, 5); \
        beeper_playNote(NOTE_A6, BEEPER_VOLUME, 20, 0); \
    }
#define BEEPER_EVENT_WINDOW()               \
    {                                       \
        beeper_playNote(NOTE_C7, BEEPER_VOLUME, 10, 0); \
        beeper_playNote(NOTE_E7, BEEPER_VOLUME, 10, 0); \
        beeper_playNote(NOTE_G7, BEEPER_VOLUME, 20, 0); \
    }
#define BEEPER_EVENT_WINDOW_CLOSE()         \
    {                                       \
        beeper_playNote(NOTE_G7, BEEPER_VOLUME, 10, 0); \
        beeper_playNote(NOTE_E7, BEEPER_VOLUME, 10, 0); \
        beeper_playNote(NOTE_C7, BEEPER_VOLUME, 20, 0); \
    }
#define BEEPER_EVENT_ERROR()                 \
    {                                        \
        beeper_playNote(NOTE_E3, BEEPER_VOLUME, 50, 0); \
        beeper_playNote(NOTE_C3, BEEPER_VOLUME, 100, 0); \
    }

#define STR_PAGE_LEN        128
#define STR_WEBSITE_LEN     128

extern void lodepng_free(void* ptr);

HTTPClient http;
Infolabel openhab_ui_infolabel;

static Config *current_config;

lv_style_t custom_style_top_bar;
lv_style_t custom_style_label_state;
lv_style_t custom_style_label_state_large;
lv_style_t custom_style_label;
lv_style_t custom_style_button;
lv_style_t custom_style_button_toggle;
lv_style_t custom_style_windows_header;

struct header_s
{
    lv_obj_t *container = nullptr;
    struct
    {
        lv_obj_t *clock = nullptr;
        lv_obj_t *title = nullptr;
        lv_obj_t *wifi = nullptr;
        lv_obj_t *signal = nullptr;
    } item;
};

static struct header_s header;

static lv_obj_t *content = nullptr;

struct widget_context_s
{
    unsigned long update_timestamp = 0;
    bool refresh_request = false;
    lv_obj_t *container = NULL;
    lv_style_t container_style;
    lv_obj_t *label = NULL;
    lv_style_t label_style;
    lv_obj_t *img_obj = NULL;
    lv_img_dsc_t img_dsc;
    lv_obj_t *state_widget = NULL;
    lv_style_t state_widget_style;
    lv_obj_t *state_window_widget = NULL;
    Item *item = NULL;
};

struct statistics_s
{
    uint32_t update_success_cnt = 0;
    uint32_t update_fail_cnt = 0;
    uint32_t sitemap_success_cnt = 0;
    uint32_t sitemap_fail_cnt = 0;
};

Sitemap sitemap;

struct widget_context_s widget_context[WIDGET_COUNT_MAX];

struct statistics_s statistics;

void update_state_widget(struct widget_context_s *ctx);

static bool refresh_page;
static char current_page[STR_PAGE_LEN];
static char last_page[STR_PAGE_LEN];
static char current_website[STR_WEBSITE_LEN];

uint8_t get_signal_quality(int8_t rssi)
{
    if (rssi < -100)
        return 0;
    else if (rssi > -50)
        return 100;
    else
        return 2 * (rssi + 100);
}

void window_close_event_handler(lv_obj_t *btn, lv_event_t event)
{
    if (event == LV_EVENT_RELEASED)
    {
        lv_obj_t *win = lv_win_get_from_btn(btn);

        lv_obj_del(win);
        BEEPER_EVENT_WINDOW_CLOSE();
    }
}

void header_event_handler(lv_obj_t *obj, lv_event_t event)
{
    if (event == LV_EVENT_CLICKED)
    {
#if DEBUG_OPENHAB_UI
        printf("header_event_handler: LV_EVENT_CLICKED\r\n");
#endif

        BEEPER_EVENT_WINDOW();

        // Create a window
        lv_obj_t *win = lv_win_create(lv_scr_act(), NULL);
        //lv_win_set_sb_mode(win, LV_SB_MODE_OFF);
        lv_win_set_title(win, "Systeminfo");

        // Add close button to the header
        lv_obj_t *close_btn = lv_win_add_btn(win, LV_SYMBOL_CLOSE);
        lv_win_set_header_height(win, LV_DPI / 3);
        lv_obj_set_event_cb(close_btn, window_close_event_handler);

        // Create a normal cell style
        static lv_style_t style_cell1;
        lv_style_init(&style_cell1);
        lv_style_set_border_color(&style_cell1, LV_STATE_DEFAULT, LV_COLOR_SILVER);
        lv_style_set_border_width(&style_cell1, LV_STATE_DEFAULT, 1);
        lv_style_set_pad_top(&style_cell1, LV_STATE_DEFAULT, 0);
        lv_style_set_pad_bottom(&style_cell1, LV_STATE_DEFAULT, 0);
        lv_style_set_margin_bottom(&style_cell1, LV_STATE_DEFAULT, 0);
        lv_style_set_text_font(&style_cell1, LV_STATE_DEFAULT, &custom_font_roboto_16);

        static lv_style_t style_bg;
        lv_style_init(&style_bg);
        lv_style_set_border_color(&style_bg, LV_STATE_DEFAULT, LV_COLOR_SILVER);

        lv_obj_t *table = lv_table_create(win, NULL);
        //lv_table_set_style(table, LV_TABLE_STYLE_CELL1, &style_cell1);
        lv_obj_add_style(table, LV_TABLE_PART_CELL1, &style_cell1);
        lv_obj_add_style(table, LV_TABLE_PART_BG, &style_bg);
        lv_table_set_col_cnt(table, 2);
        lv_table_set_row_cnt(table, 11);
        lv_coord_t table_width = lv_disp_get_hor_res(NULL) - 10;
        lv_table_set_col_width(table, 0, table_width * 30 / 100);
        lv_table_set_col_width(table, 1, table_width * 70 / 100);
        lv_obj_align(table, NULL, LV_ALIGN_CENTER, 0, 0);

        char temp_buffer[50];
        uint16_t row = 0;

        lv_table_set_cell_value(table, row, 0, "Uptime");
        uptime::calculateUptime();
        sprintf(temp_buffer, "%lu days, %luh %lum %lus",
                uptime::getDays(), uptime::getHours(), uptime::getMinutes(), uptime::getSeconds());
        lv_table_set_cell_value(table, row, 1, temp_buffer);

        lv_table_set_cell_value(table, ++row, 0, "Version");
        sprintf(temp_buffer, "%u.%02u (%s %s)", VERSION_MAJOR, VERSION_MINOR, __DATE__, __TIME__);
        lv_table_set_cell_value(table, row, 1, temp_buffer);

        lv_table_set_cell_value(table, ++row, 0, "Hostname");
        lv_table_set_cell_value(table, row, 1, WiFi.getHostname());

        lv_table_set_cell_value(table, ++row, 0, "SSID");
        lv_table_set_cell_value(table, row, 1, WiFi.SSID().c_str());

        lv_table_set_cell_value(table, ++row, 0, "BSSID");
        lv_table_set_cell_value(table, row, 1, WiFi.BSSIDstr().c_str());

        lv_table_set_cell_value(table, ++row, 0, "RSSI");

        int8_t signal_rssi = WiFi.RSSI();

        sprintf(temp_buffer, "%i dBm (%u %%)", signal_rssi, get_signal_quality(signal_rssi));
        lv_table_set_cell_value(table, row, 1, temp_buffer);

        lv_table_set_cell_value(table, ++row, 0, "MAC");
        lv_table_set_cell_value(table, row, 1, WiFi.macAddress().c_str());

        lv_table_set_cell_value(table, ++row, 0, "IP Addr.");
        IPAddress ip = WiFi.localIP();
        sprintf(temp_buffer, "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
        lv_table_set_cell_value(table, row, 1, temp_buffer);

        lv_table_set_cell_value(table, ++row, 0, "Mask");
        IPAddress mask = WiFi.subnetMask();
        sprintf(temp_buffer, "%u.%u.%u.%u", mask[0], mask[1], mask[2], mask[3]);
        lv_table_set_cell_value(table, row, 1, temp_buffer);

        lv_table_set_cell_value(table, ++row, 0, "Gateway");
        IPAddress gwip = WiFi.gatewayIP();
        sprintf(temp_buffer, "%u.%u.%u.%u", gwip[0], gwip[1], gwip[2], gwip[3]);
        lv_table_set_cell_value(table, row, 1, temp_buffer);

        lv_table_set_cell_value(table, ++row, 0, "DNS");
        IPAddress dnsip = WiFi.dnsIP();
        sprintf(temp_buffer, "%u.%u.%u.%u", dnsip[0], dnsip[1], dnsip[2], dnsip[3]);
        lv_table_set_cell_value(table, row, 1, temp_buffer);
    }
}

lv_color_hsv_t hsvCStringToLVColor(const char *hsvstring)
{
    const char *ptr = hsvstring;
    char *endptr;

    lv_color_hsv_t hsvcolor;

    hsvcolor.h = strtol(ptr, &endptr, 10);
    hsvcolor.s = strtol(endptr + 1, &endptr, 10);
    hsvcolor.v = strtol(endptr + 1, &endptr, 10);

    Serial.printf("HSV String: %s -> H=%u S=%u V=%u", hsvstring, hsvcolor.h, hsvcolor.s, hsvcolor.v);

    return hsvcolor;
}

static void window_item_colorpicker_event_handler(lv_obj_t *obj, lv_event_t event)
{
    if (event == LV_EVENT_VALUE_CHANGED)
    {
#if DEBUG_OPENHAB_UI
        Serial.printf("window_item_colorpicker_event_handler: LV_EVENT_VALUE_CHANGED\n");
#endif
        struct widget_context_s *ctx = (struct widget_context_s *)lv_obj_get_user_data(obj);

        if (ctx != nullptr)
        {
            char hsv[STR_STATE_TEXT_LEN];
            snprintf(hsv, sizeof(hsv), "%u,%u,%u",
                    lv_cpicker_get_hue(ctx->state_window_widget),
                    lv_cpicker_get_saturation(ctx->state_window_widget),
                    lv_cpicker_get_value(ctx->state_window_widget));
#if DEBUG_OPENHAB_UI
            Serial.printf("hsv string: %s\r\n", hsv);
#endif
            ctx->item->setStateText(hsv);
            ctx->item->publish(ctx->item->getLink());
            ctx->refresh_request = true;
            BEEPER_EVENT_CHANGE();
        }
    }
}

static void window_item_colorpicker_saturation_event_handler(lv_obj_t *obj, lv_event_t event)
{
    if (event == LV_EVENT_VALUE_CHANGED)
    {
#if DEBUG_OPENHAB_UI
        Serial.printf("window_item_colorpicker_saturation_event_handler: LV_EVENT_VALUE_CHANGED\n");
#endif
        struct widget_context_s *ctx = (struct widget_context_s *)lv_obj_get_user_data(obj);

        if (ctx != nullptr)
        {
#if DEBUG_OPENHAB_UI
            Serial.printf("saturation: %u\n", lv_slider_get_value(obj));
#endif
            lv_cpicker_set_saturation(ctx->state_window_widget, lv_slider_get_value(obj));
            lv_event_send(ctx->state_window_widget, LV_EVENT_VALUE_CHANGED, NULL);
            BEEPER_EVENT_CHANGE();
        }
    }
}

static void window_item_colorpicker_value_event_handler(lv_obj_t *obj, lv_event_t event)
{
    if (event == LV_EVENT_VALUE_CHANGED)
    {
#if DEBUG_OPENHAB_UI
        Serial.printf("window_item_colorpicker_value_event_handler: LV_EVENT_VALUE_CHANGED\n");
#endif
        struct widget_context_s *ctx = (struct widget_context_s *)lv_obj_get_user_data(obj);

        if (ctx != nullptr)
        {
#if DEBUG_OPENHAB_UI
            Serial.printf("value: %u\n", lv_slider_get_value(obj));
#endif
            lv_cpicker_set_value(ctx->state_window_widget, lv_slider_get_value(obj));
            lv_event_send(ctx->state_window_widget, LV_EVENT_VALUE_CHANGED, NULL);
            BEEPER_EVENT_CHANGE();
        }
    }
}

void window_item_colorpicker(struct widget_context_s *ctx)
{
    // Create a window
    lv_obj_t *win = lv_win_create(lv_scr_act(), NULL);
    lv_obj_add_style(win, LV_WIN_PART_HEADER, &custom_style_windows_header);
    // lv_win_set_sb_mode(win, LV_SB_MODE_OFF);
    lv_win_set_title(win, ctx->item->getLabel());

    // Add close button to the header
    lv_obj_t *close_btn = lv_win_add_btn(win, LV_SYMBOL_CLOSE); // Add close button and use built-in close action
    lv_obj_set_event_cb(close_btn, window_close_event_handler);

    // Add content
    lv_obj_t *cont = lv_cont_create(win, NULL);
    // lv_cont_set_style(cont, LV_CONT_STYLE_MAIN, &lv_style_transp_fit);
    lv_obj_set_auto_realign(cont, true);                   // Auto realign when the size changes*/
    lv_obj_align_origo(cont, NULL, LV_ALIGN_CENTER, 0, 0); // This parameters will be sued when realigned
    //lv_cont_set_fit(cont, LV_FIT_FLOOD);
    lv_cont_set_layout(cont, LV_LAYOUT_PRETTY_MID);

    // Set the style of the color ring
    static lv_style_t styleMain;
    // lv_style_copy(&styleMain, &lv_style_plain);
    // styleMain.line.width = 30;
    // Make the background white
    // styleMain.body.main_color = styleMain.body.grad_color = LV_COLOR_WHITE;

    // Set the style of the knob
    static lv_style_t styleIndicator;
    // lv_style_copy(&styleIndicator, &lv_style_pretty);
    // styleIndicator.body.border.color = LV_COLOR_WHITE;

    // Ensure that the knob is fully opaque
    // styleIndicator.body.opa = LV_OPA_COVER;
    // styleIndicator.body.border.opa = LV_OPA_COVER;

    lv_obj_t *colorPicker = lv_cpicker_create(cont, NULL);
    lv_coord_t picker_size = lv_obj_get_height(cont);
    lv_obj_set_size(colorPicker, picker_size, picker_size);

    // Choose the 'DISC' type
    lv_cpicker_set_type(colorPicker, LV_CPICKER_TYPE_DISC);

    // Set the styles
    // lv_cpicker_set_style(colorPicker, LV_CPICKER_STYLE_MAIN, &styleMain);
    // lv_cpicker_set_style(colorPicker, LV_CPICKER_STYLE_INDICATOR, &styleIndicator);

    // Change the knob's color to that of the selected color
    // lv_cpicker_set_indic_colored(colorPicker, true);
    // lv_cpicker_set_preview(colorPicker, true);
    lv_obj_set_user_data(colorPicker, (lv_obj_user_data_t)ctx);
    lv_obj_set_event_cb(colorPicker, window_item_colorpicker_event_handler);

    lv_color_hsv_t color_hsv = hsvCStringToLVColor(ctx->item->getStateText());
    lv_cpicker_set_hsv(colorPicker, color_hsv);

    // Create container for saturation controls right of the colorwheel
    lv_obj_t *cont_sat = lv_cont_create(cont, NULL);
    // lv_cont_set_style(cont_sat, LV_CONT_STYLE_MAIN, &lv_style_transp);
    lv_cont_set_fit(cont_sat, LV_FIT_TIGHT);
    lv_cont_set_layout(cont_sat, LV_LAYOUT_CENTER);
    lv_obj_set_size(cont_sat, 40, lv_obj_get_height(cont));

    // Create a label above the slider as spacer
    lv_obj_t *sat_slider_spacer = lv_label_create(cont_sat, NULL);
    lv_label_set_text(sat_slider_spacer, "   ");

    // Create a saturation slider
    lv_obj_t *sat_slider = lv_slider_create(cont_sat, NULL);
    lv_obj_set_size(sat_slider, 30, lv_obj_get_height(cont) - LV_DPI * 2 / 3);
    lv_obj_set_event_cb(sat_slider, window_item_colorpicker_saturation_event_handler);
    lv_slider_set_range(sat_slider, 0, 100);
    lv_slider_set_value(sat_slider, color_hsv.s, LV_ANIM_OFF);
    lv_obj_set_user_data(sat_slider, (lv_obj_user_data_t)ctx);

    // Create a label below the slider
    lv_obj_t *sat_slider_label = lv_label_create(cont_sat, NULL);
    lv_label_set_text(sat_slider_label, "Saturation");
    lv_obj_set_auto_realign(sat_slider_label, true);
    lv_obj_align(sat_slider_label, sat_slider, LV_ALIGN_IN_TOP_MID, 0, 0);

    // Create container for value controls right of the saturation slider
    lv_obj_t *cont_val = lv_cont_create(cont, NULL);
    lv_cont_set_fit(cont_val, LV_FIT_TIGHT);
    // lv_cont_set_style(cont_val, LV_CONT_STYLE_MAIN, &lv_style_transp);
    // lv_cont_set_layout(cont_val, LV_LAYOUT_COL_M);

    // Create a label above the slider as spacer
    lv_obj_t *val_slider_spacer = lv_label_create(cont_val, NULL);
    lv_label_set_text(val_slider_spacer, "   ");

    // Create a val slider
    lv_obj_t *val_slider = lv_slider_create(cont_val, NULL);
    lv_obj_set_size(val_slider, 30, lv_obj_get_height(cont) - LV_DPI * 2 / 3);
    lv_obj_align(val_slider, cont_val, LV_ALIGN_OUT_BOTTOM_MID, 0, 00);
    lv_obj_set_event_cb(val_slider, window_item_colorpicker_value_event_handler);
    lv_slider_set_range(val_slider, 0, 100);
    lv_slider_set_value(val_slider, color_hsv.v, LV_ANIM_OFF);
    lv_obj_set_user_data(val_slider, (lv_obj_user_data_t)ctx);

    // Create a label below the slider
    lv_obj_t *val_slider_label = lv_label_create(cont_val, NULL);
    lv_label_set_text(val_slider_label, "Value");
    lv_obj_set_auto_realign(val_slider_label, true);
    lv_obj_align(val_slider_label, val_slider, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);

    ctx->state_window_widget = colorPicker;
}

static void window_item_selection_event_handler(lv_obj_t *obj, lv_event_t event)
{
    if (event == LV_EVENT_CLICKED)
    {
#if DEBUG_OPENHAB_UI
        printf("window_item_selection_event_handler: LV_EVENT_CLICKED\r\n");
#endif
        struct widget_context_s *ctx = (struct widget_context_s *)lv_obj_get_user_data(obj);

        if (ctx != nullptr)
        {
            // deactivate all buttons
            lv_obj_t *btn = NULL;
            do
            {
                btn = lv_obj_get_child(ctx->state_window_widget, btn);

                if (btn != NULL)
                    lv_btn_set_state(btn, LV_BTN_STATE_RELEASED);

            } while (btn != NULL);

            // activate pressed
            lv_btn_set_state(obj, LV_BTN_STATE_PRESSED);

            lv_obj_t *label = lv_obj_get_child(obj, NULL);
            char *command = (char *)lv_obj_get_user_data(label);

#if DEBUG_OPENHAB_UI
            Serial.printf("button pressed Label: %s, Command: %s\r\n", lv_label_get_text(label), command);
#endif
            ctx->item->setStateText(command);
            ctx->item->publish(ctx->item->getLink());
            ctx->refresh_request = true;
            BEEPER_EVENT_CHANGE();
        }
    }
}

void window_item_selection(struct widget_context_s *ctx)
{
#if DEBUG_OPENHAB_UI
        printf("window_item_selection()\r\n");
#endif
    // Create a window
    lv_obj_t *win = lv_win_create(lv_scr_act(), NULL);
    lv_obj_add_style(win, LV_WIN_PART_HEADER, &custom_style_windows_header);
    // lv_win_set_sb_mode(win, LV_SB_MODE_OFF);
    lv_win_set_title(win, ctx->item->getLabel());

    // Add close button to the header
    lv_obj_t *close_btn = lv_win_add_btn(win, LV_SYMBOL_CLOSE);
    lv_obj_set_event_cb(close_btn, window_close_event_handler);

    // Add buttons
    lv_obj_t *cont;

    cont = lv_cont_create(win, NULL);
    // lv_cont_set_style(cont, LV_CONT_STYLE_MAIN, &lv_style_transp_fit);
    lv_obj_set_auto_realign(cont, true);
    lv_obj_align_origo(cont, NULL, LV_ALIGN_CENTER, 0, 0);
    lv_cont_set_fit(cont, LV_FIT_TIGHT);
    lv_cont_set_layout(cont, LV_LAYOUT_GRID);

    for (size_t index = 0; index < ctx->item->getSelectionCount(); index++)
    {
        lv_obj_t *btn = lv_btn_create(cont, NULL);
        lv_obj_set_user_data(btn, (lv_obj_user_data_t)ctx);
        lv_obj_set_event_cb(btn, window_item_selection_event_handler);
        lv_btn_set_fit2(btn, LV_FIT_TIGHT, LV_FIT_TIGHT);

        lv_obj_t *label = lv_label_create(btn, NULL);
        lv_label_set_text(label, ctx->item->getSelectionLabel(index));
        lv_obj_set_user_data(label, (lv_obj_user_data_t)ctx->item->getSelectionCommand(index));

#if DEBUG_OPENHAB_UI
        printf("Label: \"%s\", State: \"%s\"\n", ctx->item->getSelectionLabel(index), ctx->item->getStateText());
#endif
        if (strcmp(ctx->item->getSelectionCommand(index), ctx->item->getStateText()) == 0)
        {
#if DEBUG_OPENHAB_UI
        printf("Button pressed!\n");
#endif
            lv_btn_set_state(btn, LV_BTN_STATE_PRESSED);
        }
        else
        {
            lv_btn_set_state(btn, LV_BTN_STATE_RELEASED);
        }
    }

    ctx->state_window_widget = cont;
}

static void window_item_rollershutter_event_handler(lv_obj_t *obj, lv_event_t event)
{
    if (event == LV_EVENT_CLICKED)
    {
#if DEBUG_OPENHAB_UI
        printf("window_item_rollershutter_event_handler: LV_EVENT_CLICKED\n");
#endif
        struct widget_context_s *ctx = (struct widget_context_s *)lv_obj_get_user_data(obj);

        if (ctx != nullptr)
        {
            // deactivate all buttons
            lv_obj_t *btn = NULL;
            do
            {
                btn = lv_obj_get_child(ctx->state_window_widget, btn);

                if (btn != NULL)
                     lv_btn_set_state(btn, LV_BTN_STATE_RELEASED);

            } while (btn != NULL);

            // activate pressed
            lv_btn_set_state(obj, LV_BTN_STATE_PRESSED);

            lv_obj_t *label = lv_obj_get_child(obj, NULL);
            char *command = (char *)lv_obj_get_user_data(label);

#if DEBUG_OPENHAB_UI
            Serial.printf("button pressed Command: %s\n", command);
#endif
            ctx->item->setStateText(command);
            ctx->item->publish(ctx->item->getLink());
            ctx->refresh_request = true;
            BEEPER_EVENT_CHANGE();
        }
    }
}

void window_item_rollershutter(struct widget_context_s *ctx)
{
#if DEBUG_OPENHAB_UI
        printf("window_item_rollershutter()\n");
#endif
    // Create a window
    lv_obj_t *win = lv_win_create(lv_scr_act(), NULL);
    lv_obj_add_style(win, LV_WIN_PART_HEADER, &custom_style_windows_header);
    // lv_win_set_sb_mode(win, LV_SB_MODE_OFF);
    lv_win_set_title(win, ctx->item->getLabel());

    // Add close button to the header
    lv_obj_t *close_btn = lv_win_add_btn(win, LV_SYMBOL_CLOSE);
    lv_obj_set_event_cb(close_btn, window_close_event_handler);

    // Add buttons
    lv_obj_t *cont;

    cont = lv_cont_create(win, NULL);
    // lv_cont_set_style(cont, LV_CONT_STYLE_MAIN, &lv_style_transp_fit);
    lv_obj_set_size(cont, lv_obj_get_width(win) * 4 / 5, lv_obj_get_height(win) / 3);
    lv_obj_align_origo(cont, NULL, LV_ALIGN_CENTER, 0, 0);
    lv_cont_set_layout(cont, LV_LAYOUT_PRETTY_MID);

    lv_obj_t *btn;
    lv_obj_t *label;

    // Button UP
    btn = lv_btn_create(cont, NULL);
    lv_obj_set_user_data(btn, (lv_obj_user_data_t)ctx);
    lv_obj_set_event_cb(btn, window_item_rollershutter_event_handler);
    lv_btn_set_fit2(btn, LV_FIT_TIGHT, LV_FIT_TIGHT);
    label = lv_label_create(btn, NULL);
    lv_label_set_text(label, LV_SYMBOL_UP);
    lv_obj_set_user_data(label, (lv_obj_user_data_t)"UP");
    // lv_btn_set_style(btn, LV_BTN_STYLE_REL, &custom_style_button);

    // Button STOP
    btn = lv_btn_create(cont, NULL);
    lv_obj_set_user_data(btn, (lv_obj_user_data_t)ctx);
    lv_obj_set_event_cb(btn, window_item_rollershutter_event_handler);
    lv_btn_set_fit2(btn, LV_FIT_TIGHT, LV_FIT_TIGHT);
    label = lv_label_create(btn, NULL);
    lv_label_set_text(label, LV_SYMBOL_STOP);
    lv_obj_set_user_data(label, (lv_obj_user_data_t)"STOP");
    // lv_btn_set_style(btn, LV_BTN_STYLE_REL, &custom_style_button);

    // Button DOWN
    btn = lv_btn_create(cont, NULL);
    lv_obj_set_user_data(btn, (lv_obj_user_data_t)ctx);
    lv_obj_set_event_cb(btn, window_item_rollershutter_event_handler);
    lv_btn_set_fit2(btn, LV_FIT_TIGHT, LV_FIT_TIGHT);
    label = lv_label_create(btn, NULL);
    lv_label_set_text(label, LV_SYMBOL_DOWN);
    lv_obj_set_user_data(label, (lv_obj_user_data_t)"DOWN");
    // lv_btn_set_style(btn, LV_BTN_STYLE_REL, &custom_style_button);

    ctx->state_window_widget = cont;
}

static void window_item_player_event_handler(lv_obj_t *obj, lv_event_t event)
{
    if (event == LV_EVENT_CLICKED)
    {
#if DEBUG_OPENHAB_UI
        printf("window_item_player_event_handler: LV_EVENT_CLICKED\n");
#endif
        struct widget_context_s *ctx = (struct widget_context_s *)lv_obj_get_user_data(obj);

        if (ctx != nullptr)
        {
            // deactivate all buttons
            lv_obj_t *btn = NULL;
            do
            {
                btn = lv_obj_get_child(ctx->state_window_widget, btn);

                if (btn != NULL)
                    lv_btn_set_state(btn, LV_BTN_STATE_RELEASED);

            } while (btn != NULL);

            // activate pressed
            lv_btn_set_state(obj, LV_BTN_STATE_PRESSED);

            lv_obj_t *label = lv_obj_get_child(obj, NULL);
            char *command = (char *)lv_obj_get_user_data(label);

#if DEBUG_OPENHAB_UI
            Serial.printf("button pressed Command: %s\n", command);
#endif
            ctx->item->setStateText(command);
            ctx->item->publish(ctx->item->getLink());
            ctx->refresh_request = true;
            BEEPER_EVENT_CHANGE();
        }
    }
}

void window_item_player(struct widget_context_s *ctx)
{
#if DEBUG_OPENHAB_UI
        printf("window_item_player()\r\n");
#endif
    // Create a window
    lv_obj_t *win = lv_win_create(lv_scr_act(), NULL);
    lv_obj_add_style(win, LV_WIN_PART_HEADER, &custom_style_windows_header);
    // lv_win_set_sb_mode(win, LV_SB_MODE_OFF);
    lv_win_set_title(win, ctx->item->getLabel());

    // Add close button to the header
    lv_obj_t *close_btn = lv_win_add_btn(win, LV_SYMBOL_CLOSE);
    lv_obj_set_event_cb(close_btn, window_close_event_handler);

    // Add buttons
    lv_obj_t *cont;

    cont = lv_cont_create(win, NULL);
    // lv_cont_set_style(cont, LV_CONT_STYLE_MAIN, &lv_style_transp_fit);
    lv_obj_set_auto_realign(cont, true);
    lv_obj_set_size(cont, lv_obj_get_width(win) - LV_DPI / 4, lv_obj_get_height(win) / 3);
    lv_obj_align_origo(cont, NULL, LV_ALIGN_CENTER, 0, 0);
    lv_cont_set_layout(cont, LV_LAYOUT_PRETTY_MID);

    lv_obj_t *btn;
    lv_obj_t *label;

    // Button PREVIOUS
    btn = lv_btn_create(cont, NULL);
    lv_obj_set_user_data(btn, (lv_obj_user_data_t)ctx);
    lv_obj_set_event_cb(btn, window_item_player_event_handler);
    lv_btn_set_fit2(btn, LV_FIT_TIGHT, LV_FIT_TIGHT);
    label = lv_label_create(btn, NULL);
    lv_label_set_text(label, LV_SYMBOL_PREV);
    lv_obj_set_user_data(label, (lv_obj_user_data_t)"PREVIOUS");
    // lv_btn_set_style(btn, LV_BTN_STYLE_REL, &custom_style_button);

    // Button PAUSE
    btn = lv_btn_create(cont, NULL);
    lv_obj_set_user_data(btn, (lv_obj_user_data_t)ctx);
    lv_obj_set_event_cb(btn, window_item_player_event_handler);
    lv_btn_set_fit2(btn, LV_FIT_TIGHT, LV_FIT_TIGHT);
    label = lv_label_create(btn, NULL);
    lv_label_set_text(label, LV_SYMBOL_PAUSE);
    lv_obj_set_user_data(label, (lv_obj_user_data_t)"PAUSE");
    if (strcmp(ctx->item->getStateText(), "PAUSE") == 0)
        lv_btn_set_state(btn, LV_BTN_STATE_PRESSED);
    else
        lv_btn_set_state(btn, LV_BTN_STATE_RELEASED);

    // Button PLAY
    btn = lv_btn_create(cont, NULL);
    lv_obj_set_user_data(btn, (lv_obj_user_data_t)ctx);
    lv_obj_set_event_cb(btn, window_item_player_event_handler);
    lv_btn_set_fit2(btn, LV_FIT_TIGHT, LV_FIT_TIGHT);
    label = lv_label_create(btn, NULL);
    lv_label_set_text(label, LV_SYMBOL_PLAY);
    lv_obj_set_user_data(label, (lv_obj_user_data_t)"PLAY");
    //lv_btn_set_style(btn, LV_BTN_STYLE_REL, &custom_style_button);
    if (strcmp(ctx->item->getStateText(), "PLAY") == 0)
        lv_btn_set_state(btn, LV_BTN_STATE_PRESSED);
    else
        lv_btn_set_state(btn, LV_BTN_STATE_RELEASED);

    // Button NEXT
    btn = lv_btn_create(cont, NULL);
    lv_obj_set_user_data(btn, (lv_obj_user_data_t)ctx);
    lv_obj_set_event_cb(btn, window_item_player_event_handler);
    lv_btn_set_fit2(btn, LV_FIT_TIGHT, LV_FIT_TIGHT);
    label = lv_label_create(btn, NULL);
    lv_label_set_text(label, LV_SYMBOL_NEXT);
    lv_obj_set_user_data(label, (lv_obj_user_data_t)"NEXT");
    // lv_btn_set_style(btn, LV_BTN_STYLE_REL, &custom_style_button);

    ctx->state_window_widget = cont;
}

static void window_item_slider_event_handler(lv_obj_t *obj, lv_event_t event)
{
    if (event == LV_EVENT_VALUE_CHANGED)
    {
#if DEBUG_OPENHAB_UI
        printf("window_item_slider_event_handler: LV_EVENT_VALUE_CHANGED\n");
#endif
        struct widget_context_s *ctx = (struct widget_context_s *)lv_obj_get_user_data(obj);

        if (ctx != nullptr)
        {
            ctx->item->setStateNumber(lv_slider_get_value(obj));

            if (strncmp(ctx->item->getNumberPattern(), "%d", 2) == 0)
                lv_label_set_text_fmt(ctx->state_window_widget, ctx->item->getNumberPattern(), (uint16_t)ctx->item->getStateNumber());
            else
                lv_label_set_text_fmt(ctx->state_window_widget, ctx->item->getNumberPattern(), ctx->item->getStateNumber());

            ctx->item->publish(ctx->item->getLink());
            ctx->refresh_request = true;
            BEEPER_EVENT_CHANGE();
        }
    }
}

void window_item_slider(struct widget_context_s *ctx)
{
    // Create a window
    lv_obj_t *win = lv_win_create(lv_scr_act(), NULL);
    lv_obj_add_style(win, LV_WIN_PART_HEADER, &custom_style_windows_header);
    // lv_win_set_sb_mode(win, LV_SB_MODE_OFF);
    lv_win_set_title(win, ctx->item->getLabel());

    // Add close button to the header
    lv_obj_t *close_btn = lv_win_add_btn(win, LV_SYMBOL_CLOSE);
    lv_obj_set_event_cb(close_btn, window_close_event_handler);

    // Add slider
    lv_obj_t *slider = lv_slider_create(win, NULL);
    lv_slider_set_range(slider, ctx->item->getMinVal(), ctx->item->getMaxVal());
    lv_slider_set_value(slider, ctx->item->getStateNumber(), LV_ANIM_OFF);
    lv_obj_set_width(slider, lv_obj_get_width(win) - LV_DPI / 3);
    lv_obj_align(slider, NULL, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_user_data(slider, (lv_obj_user_data_t)ctx);
    lv_obj_set_event_cb(slider, window_item_slider_event_handler);

    // Add state label above
    lv_obj_t *state_label = lv_label_create(win, NULL);

    if (strncmp(ctx->item->getNumberPattern(), "%d", 2) == 0)
        lv_label_set_text_fmt(state_label, ctx->item->getNumberPattern(), (uint16_t)ctx->item->getStateNumber());
    else
        lv_label_set_text_fmt(state_label, ctx->item->getNumberPattern(), ctx->item->getStateNumber());

    lv_obj_add_style(state_label, LV_LABEL_PART_MAIN, &custom_style_label_state_large);
    lv_obj_set_auto_realign(state_label, true);
    lv_obj_align(state_label, slider, LV_ALIGN_OUT_TOP_MID, 0, 0 - LV_DPI / 10);
    lv_obj_set_width(state_label, lv_obj_get_width(win) - LV_DPI / 20);

    // Add minimum value label left below
    lv_obj_t *min_value_label = lv_label_create(win, NULL);

    if (strncmp(ctx->item->getNumberPattern(), "%d", 2) == 0)
        lv_label_set_text_fmt(min_value_label, ctx->item->getNumberPattern(), (uint16_t)ctx->item->getMinVal());
    else
        lv_label_set_text_fmt(min_value_label, ctx->item->getNumberPattern(), ctx->item->getMinVal());

    lv_obj_add_style(min_value_label, LV_LABEL_PART_MAIN, &custom_style_label_state);
    lv_obj_set_auto_realign(min_value_label, true);
    lv_obj_align(min_value_label, slider, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 0);
    lv_obj_set_width(state_label, lv_obj_get_width(win) / 2 - LV_DPI / 20);

    // Add maximum value label right below
    lv_obj_t *max_value_label = lv_label_create(win, NULL);

    if (strncmp(ctx->item->getNumberPattern(), "%d", 2) == 0)
        lv_label_set_text_fmt(max_value_label, ctx->item->getNumberPattern(), (uint16_t)ctx->item->getMaxVal());
    else
        lv_label_set_text_fmt(max_value_label, ctx->item->getNumberPattern(), ctx->item->getMaxVal());

    lv_obj_add_style(max_value_label, LV_LABEL_PART_MAIN, &custom_style_label_state);
    lv_obj_set_auto_realign(max_value_label, true);
    lv_obj_align(max_value_label, slider, LV_ALIGN_OUT_BOTTOM_RIGHT, 0, 0);
    lv_obj_set_width(state_label, lv_obj_get_width(win) / 2 - LV_DPI / 20);

    ctx->state_window_widget = state_label;
}

static void window_item_setpoint_event_handler(lv_obj_t *obj, lv_event_t event)
{
    if (event == LV_EVENT_CLICKED)
    {
#if DEBUG_OPENHAB_UI
        printf("window_item_setpoint_event_handler: LV_EVENT_CLICKED\n");
#endif
        struct widget_context_s *ctx = (struct widget_context_s *)lv_obj_get_user_data(obj);

        if (ctx != nullptr)
        {
            const char *txt = lv_btnmatrix_get_active_btn_text(obj);

#if DEBUG_OPENHAB_UI
            Serial.print("window_item_setpoint_event_handler: btn_text = ");
            Serial.println(txt);
#endif
            if (strcmp(txt, LV_SYMBOL_PLUS) == 0)
            {
                ctx->item->setStateNumber(ctx->item->getStateNumber() + ctx->item->getStep());
                if (ctx->item->getStateNumber() > ctx->item->getMaxVal())
                    ctx->item->setStateNumber(ctx->item->getMaxVal());
            }
            else if (strcmp(txt, LV_SYMBOL_MINUS) == 0)
            {
                ctx->item->setStateNumber(ctx->item->getStateNumber() - ctx->item->getStep());
                if (ctx->item->getStateNumber() < ctx->item->getMinVal())
                    ctx->item->setStateNumber(ctx->item->getMinVal());
            }

            if (strncmp(ctx->item->getNumberPattern(), "%d", 2) == 0)
                lv_label_set_text_fmt(ctx->state_window_widget, ctx->item->getNumberPattern(), (uint16_t)ctx->item->getStateNumber());
            else
                lv_label_set_text_fmt(ctx->state_window_widget, ctx->item->getNumberPattern(), ctx->item->getStateNumber());

            ctx->item->publish(ctx->item->getLink());
            ctx->refresh_request = true;
            BEEPER_EVENT_CHANGE();
        }
    }
}

void window_item_setpoint(struct widget_context_s *ctx)
{
    // Create a window
    lv_obj_t *win = lv_win_create(lv_scr_act(), NULL);
    lv_obj_add_style(win, LV_WIN_PART_HEADER, &custom_style_windows_header);
    // lv_win_set_sb_mode(win, LV_SB_MODE_OFF);
    lv_win_set_title(win, ctx->item->getLabel());

    // Add close button to the header
    lv_obj_t *close_btn = lv_win_add_btn(win, LV_SYMBOL_CLOSE);
    lv_obj_set_event_cb(close_btn, window_close_event_handler);

    // Add content
    static const char *btnm_map[] = {LV_SYMBOL_MINUS, LV_SYMBOL_PLUS, ""};
    lv_obj_t *btnm1 = lv_btnmatrix_create(win, NULL);
    // lv_btnm_set_style(btnm1, LV_BTNM_STYLE_BTN_REL, &custom_style_button);
    lv_btnmatrix_set_map(btnm1, btnm_map);
    lv_obj_set_height(btnm1, lv_obj_get_height(win) / 3);
    lv_obj_align(btnm1, NULL, LV_ALIGN_IN_BOTTOM_MID, 0, 0);
    lv_obj_set_user_data(btnm1, (lv_obj_user_data_t)ctx);
    lv_obj_set_event_cb(btnm1, window_item_setpoint_event_handler);

    lv_obj_t *state_label = lv_label_create(win, NULL);
    lv_label_set_align(state_label, LV_LABEL_ALIGN_CENTER);
    lv_label_set_long_mode(state_label, LV_LABEL_LONG_BREAK);
    lv_obj_set_width(state_label, lv_obj_get_width(win) - LV_DPI / 20);

    if (strncmp(ctx->item->getNumberPattern(), "%d", 2) == 0)
        lv_label_set_text_fmt(state_label, ctx->item->getNumberPattern(), (uint16_t)ctx->item->getStateNumber());
    else
        lv_label_set_text_fmt(state_label, ctx->item->getNumberPattern(), ctx->item->getStateNumber());

    lv_obj_add_style(state_label, LV_LABEL_PART_MAIN, &custom_style_label_state_large);
    lv_obj_set_auto_realign(state_label, true);
    lv_obj_align(state_label, btnm1, LV_ALIGN_OUT_TOP_MID, 0, 0 - LV_DPI / 4);
    ctx->state_window_widget = state_label;
}

static void event_handler(lv_obj_t *obj, lv_event_t event)
{
    if (event == LV_EVENT_CLICKED)
    {
#if DEBUG_OPENHAB_UI
        printf("event_handler: LV_EVENT_CLICKED\r\n");
#endif
        struct widget_context_s *ctx = (struct widget_context_s *)lv_obj_get_user_data(obj);

        if (ctx != nullptr)
        {
            if (ctx->item->getType() == ItemType::type_string)
            {
#if DEBUG_OPENHAB_UI
                if (ctx->item->getType() == ItemType::type_number)
                {
                    printf("Item State Number :");
                    printf(ctx->item->getNumberPattern(), ctx->item->getStateNumber());
                }
                else
                {
                    printf("Item State Text: %s ", ctx->item->getStateText());
                }

                printf("Item Link: %s", ctx->item->getLink());
#endif
            }
            else if (ctx->item->getType() == ItemType::type_link
                     || ctx->item->getType() == ItemType::type_parent_link
                     || ctx->item->getType() == ItemType::type_group)
            {
#if DEBUG_OPENHAB_UI
                printf("LinkedPage Link: %s", ctx->item->getPageLink());
#endif
                strncpy(last_page, current_page, sizeof(last_page));
                strncpy(current_page, ctx->item->getPageLink(), sizeof(current_page));
                refresh_page = true;
                if (ctx->item->getType() == ItemType::type_parent_link)
                    BEEPER_EVENT_LINK_BACK()
                else
                    BEEPER_EVENT_LINK();
            }
            else if (ctx->item->getType() == ItemType::type_switch)
            {
#if DEBUG_OPENHAB_UI
                printf("Link: %s", ctx->item->getLink());
                printf(" ... Posting update");
#endif

                if (strncmp(ctx->item->getStateText(), "OFF", 3) == 0)
                    ctx->item->setStateText("ON");
                else
                    ctx->item->setStateText("OFF");

                ctx->item->publish(ctx->item->getLink());
                ctx->refresh_request = true;
                BEEPER_EVENT_CHANGE();
            }
            else if (ctx->item->getType() == ItemType::type_setpoint)
            {
                BEEPER_EVENT_WINDOW();
                window_item_setpoint(ctx);
            }
            else if (ctx->item->getType() == ItemType::type_slider)
            {
                BEEPER_EVENT_WINDOW();
                window_item_slider(ctx);
            }
            else if (ctx->item->getType() == ItemType::type_selection)
            {
                BEEPER_EVENT_WINDOW();
                window_item_selection(ctx);
            }
            else if (ctx->item->getType() == ItemType::type_rollershutter)
            {
                BEEPER_EVENT_WINDOW();
                window_item_rollershutter(ctx);
            }
            else if (ctx->item->getType() == ItemType::type_player)
            {
                BEEPER_EVENT_WINDOW();
                window_item_player(ctx);
            }
            else if (ctx->item->getType() == ItemType::type_colorpicker)
            {
                BEEPER_EVENT_WINDOW();
                window_item_colorpicker(ctx);
            }
            else
            {
                BEEPER_EVENT_ERROR();
#if DEBUG_OPENHAB_UI
                printf("Unknown");
#endif
            }
        }
#if DEBUG_OPENHAB_UI
        printf("\r\n");
#endif
    }
}

void update_state_widget(struct widget_context_s *ctx)
{
    if (ctx->state_widget == NULL)
        return;

    if (    ctx->item->getType() == ItemType::type_string)
    {
        if (sizeof(ctx->item->getTransformedStateText()) > 0)
        {
            lv_label_set_text(ctx->state_widget, ctx->item->getTransformedStateText());
        }
        else
        {
            lv_label_set_text(ctx->state_widget, ctx->item->getStateText());
        }
    }
    else if (ctx->item->getType() == ItemType::type_group && ctx->item->stateIsNumber() == false)
    {
        lv_label_set_text(ctx->state_widget, ctx->item->getStateText());
    }
    else if (   ctx->item->getType() == ItemType::type_number
            || (ctx->item->getType() == ItemType::type_group && ctx->item->stateIsNumber() == true))
    {
        if (strncmp(ctx->item->getNumberPattern(), "%d", 2) == 0)
            lv_label_set_text_fmt(ctx->state_widget, ctx->item->getNumberPattern(), (uint16_t)ctx->item->getStateNumber());
        else
            lv_label_set_text_fmt(ctx->state_widget, ctx->item->getNumberPattern(), ctx->item->getStateNumber());
    }
    else if (  ctx->item->getType() == ItemType::type_switch)
    {
        // Mapping found for Switch, should support ON=Text1 and OFF=Text2 (tested)
        if ( ctx->item->getSelectionCount() > 0)
        {
            for (size_t index = 0; index < ctx->item->getSelectionCount(); index++)
            {
                if (strcmp(ctx->item->getSelectionCommand(index), ctx->item->getStateText()) == 0)
                {
                    lv_label_set_text(ctx->state_widget, ctx->item->getSelectionLabel(index));
                    break;
                }
            }
        }
        else
        {
            lv_label_set_text(ctx->state_widget, ctx->item->getStateText());
        }
    }
    else if (  ctx->item->getType() == ItemType::type_player)
    {
        lv_label_set_text(ctx->state_widget, ctx->item->getStateText());
    }
    else if (  ctx->item->getType() == ItemType::type_setpoint
            || ctx->item->getType() == ItemType::type_slider
            || ctx->item->getType() == ItemType::type_rollershutter)
    {
        if (strncmp(ctx->item->getNumberPattern(), "%d", 2) == 0)
            lv_label_set_text_fmt(ctx->state_widget, ctx->item->getNumberPattern(), (uint16_t)ctx->item->getStateNumber());
        else
            lv_label_set_text_fmt(ctx->state_widget, ctx->item->getNumberPattern(), ctx->item->getStateNumber());
    }
    else if (ctx->item->getType() == ItemType::type_selection)
    {
        for (size_t index = 0; index < ctx->item->getSelectionCount(); index++)
        {
            if (strcmp(ctx->item->getSelectionCommand(index), ctx->item->getStateText()) == 0)
            {
                lv_label_set_text(ctx->state_widget, ctx->item->getSelectionLabel(index));
                break;
            }
        }
    }
    else if (ctx->item->getType() == ItemType::type_colorpicker)
    {
        lv_color_hsv_t color_hsv = hsvCStringToLVColor(ctx->item->getStateText());
        // ctx->state_widget_style.body.main_color = lv_color_hsv_to_rgb(color_hsv.h, color_hsv.s, color_hsv.v);
        // ctx->state_widget_style.body.grad_color = lv_color_hsv_to_rgb(color_hsv.h, color_hsv.s, color_hsv.v);
        lv_obj_invalidate(ctx->state_widget);
    }
    else
    {
        Serial.print("update_state_widget: unknown or unsupported item type id: ");
        Serial.println(ctx->item->getType());
    }
}

/**
 * If the display is not in 32 bit format (ARGB888) then covert the image to the current color depth
 * @param img the ARGB888 image
 * @param px_cnt number of pixels in `img`
 */
void convert_color_depth(uint8_t *img, uint32_t px_cnt)
{
#if LV_COLOR_DEPTH == 32
    lv_color32_t *img_argb = (lv_color32_t *)img;
    lv_color_t c;
    lv_color_t *img_c = (lv_color_t *)img;
    uint32_t i;
    for (i = 0; i < px_cnt; i++)
    {
        c = LV_COLOR_MAKE(img_argb[i].ch.red, img_argb[i].ch.green, img_argb[i].ch.blue);
        img_c[i].ch.red = c.ch.blue;
        img_c[i].ch.blue = c.ch.red;
    }
#elif LV_COLOR_DEPTH == 16
    lv_color32_t *img_argb = (lv_color32_t *)img;
    lv_color_t c;
    uint32_t i;
    for (i = 0; i < px_cnt; i++)
    {
        c = LV_COLOR_MAKE(img_argb[i].ch.blue, img_argb[i].ch.green, img_argb[i].ch.red);
        img[i * 3 + 2] = img_argb[i].ch.alpha;
        img[i * 3 + 1] = c.full >> 8;
        img[i * 3 + 0] = c.full & 0xFF;
    }
#elif LV_COLOR_DEPTH == 8
    lv_color32_t *img_argb = (lv_color32_t *)img;
    lv_color_t c;
    uint32_t i;
    for (i = 0; i < px_cnt; i++)
    {
        c = LV_COLOR_MAKE(img_argb[i].red, img_argb[i].green, img_argb[i].blue);
        img[i * 3 + 1] = img_argb[i].alpha;
        img[i * 3 + 0] = c.full
    }
#endif
}

void free_icon(lv_img_dsc_t *pdsc)
{
    if (pdsc->data == NULL)
        return;
    const uint8_t *pref = pdsc->data;

    free((void *)pdsc->data);

    // clear all cache references
    for (size_t ref = 0; ref < WIDGET_COUNT_MAX; ref++)
    {
        if (widget_context[ref].img_dsc.data == pref)
        {
            widget_context[ref].img_dsc.data = NULL;
            widget_context[ref].img_dsc.data_size = 0;
        }
    }
}

void load_icon(struct widget_context_s *wctx)
{
    // free old image data
    if (wctx->img_dsc.data != NULL)
        free((void *)wctx->img_dsc.data);

    // reset image descriptor
    wctx->img_dsc.header.w = 0;
    wctx->img_dsc.header.h = 0;
    wctx->img_dsc.data_size = 0;
    wctx->img_dsc.data = NULL;

    wctx->img_dsc.header.always_zero = 0;                 // It must be zero
    wctx->img_dsc.header.cf = LV_IMG_CF_TRUE_COLOR_ALPHA; // Set the color format

    static uint8_t iconbuffer[ICON_PNG_BUFFER_SIZE] = {0};

    size_t iconsize = wctx->item->getIcon(current_website, wctx->item->getIconName(), wctx->item->getStateText(), iconbuffer, sizeof(iconbuffer));

    // Decode the PNG image
    unsigned char *png_decoded;
    uint32_t png_width, png_height;

#if DEBUG_OPENHAB_UI
    printf("load_icon: %s ", wctx->item->getIconName());
#endif

    // Decode the loaded image in ARGB8888
    unsigned int error = lodepng_decode32(&png_decoded, &png_width, &png_height, iconbuffer, iconsize);

    if (error)
    {
        printf("PNG decode error %u: %s\n", error, lodepng_error_text(error));
        return;
    }

    convert_color_depth(png_decoded, png_width * png_height);

    // Initialize an image descriptor for LittlevGL with the decoded image
    wctx->img_dsc.header.w = png_width;
    wctx->img_dsc.header.h = png_height;
    wctx->img_dsc.data_size = png_width * png_height * 4;
    wctx->img_dsc.data = png_decoded;

#if DEBUG_OPENHAB_UI
    printf("size: %u x %u, data_size %u\n", png_width, png_height, wctx->img_dsc.data_size);
#endif
}

void update_icon(struct widget_context_s *wctx)
{
    load_icon(wctx);
    if (wctx->img_dsc.data_size > 0 && wctx->img_obj != NULL)
        lv_img_set_src(wctx->img_obj, &wctx->img_dsc);
}

static void header_create(void)
{
    header.container = lv_cont_create(lv_disp_get_scr_act(NULL), NULL);
    lv_obj_add_style(header.container, LV_LABEL_PART_MAIN, &custom_style_top_bar);
    lv_obj_set_width(header.container, lv_disp_get_hor_res(NULL));
    //lv_obj_set_height(header.container, 24);

    lv_obj_set_click(header.container, true);
    lv_obj_set_event_cb(header.container, header_event_handler);

    header.item.clock = lv_label_create(header.container, NULL);
    lv_obj_add_style(header.item.clock, LV_LABEL_PART_MAIN, &custom_style_top_bar);
    lv_label_set_text(header.item.clock, "--:--");
    lv_obj_align(header.item.clock, NULL, LV_ALIGN_IN_LEFT_MID, LV_DPI / 10, 0);

    header.item.wifi = lv_label_create(header.container, NULL);
    lv_obj_add_style(header.item.wifi, LV_LABEL_PART_MAIN, &custom_style_top_bar);
    lv_label_set_text(header.item.wifi, LV_SYMBOL_POWER);
    lv_obj_align(header.item.wifi, NULL, LV_ALIGN_IN_RIGHT_MID, -LV_DPI / 2, 0);

    header.item.signal = lv_label_create(header.container, NULL);
    lv_obj_add_style(header.item.signal, LV_LABEL_PART_MAIN, &custom_style_top_bar);
    lv_label_set_text(header.item.signal, "  %");
    lv_obj_align(header.item.signal, NULL, LV_ALIGN_IN_RIGHT_MID, -LV_DPI / 5, 0);

    header.item.title = lv_label_create(header.container, NULL);
    lv_obj_add_style(header.item.title, LV_LABEL_PART_MAIN, &custom_style_top_bar);
    lv_label_set_text(header.item.title, "Welcome to OhEzTouch");
    lv_label_set_long_mode(header.item.title, LV_LABEL_LONG_SROLL);
    lv_label_set_align(header.item.title, LV_LABEL_ALIGN_CENTER);
    lv_obj_align(header.item.title, NULL, LV_ALIGN_CENTER, 0, 0);

    lv_cont_set_fit2(header.container, LV_FIT_NONE, LV_FIT_TIGHT); // Let the height be set automatically
    lv_obj_set_pos(header.container, 0, 0);
}

static void header_set_title(const char* text)
{
    lv_label_set_text(header.item.title, text);
}

static void header_update()
{
    static int last_second;
    struct tm timeinfo;

    if (getLocalTime(&timeinfo, 0))
    {
        if (timeinfo.tm_sec != last_second)
        {
            last_second = timeinfo.tm_sec;

            if (timeinfo.tm_sec % 2 == 0)
                lv_label_set_text_fmt(header.item.clock, "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
            else
                lv_label_set_text_fmt(header.item.clock, "%02d %02d", timeinfo.tm_hour, timeinfo.tm_min);
        }
    }

    static unsigned long signal_last_update;

    if (millis() > signal_last_update + HEADER_SIGNAL_UPDATE_INTERVAL)
    {
        signal_last_update = millis();
        lv_label_set_text_fmt(header.item.signal, "%02d%%", get_signal_quality(WiFi.RSSI()));
    }
}

static void content_create(void)
{
    lv_coord_t hres = lv_disp_get_hor_res(NULL);
    lv_coord_t vres = lv_disp_get_ver_res(NULL);

    content = lv_cont_create(lv_disp_get_scr_act(NULL), NULL);

    lv_obj_set_size(content, hres, vres - lv_obj_get_height(header.container));
    lv_obj_set_pos(content, 0, lv_obj_get_height(header.container));

    lv_cont_set_layout(content, LV_LAYOUT_PRETTY_MID);
}

void widget_destroy(lv_obj_t *parent, struct widget_context_s *wctx)
{
    if (wctx->container != NULL)
    {
        /* child objects (label, img_obj and state_widget) will be deleted as well */
        lv_obj_del(wctx->container);
    }

    wctx->container = NULL;
    wctx->label = NULL;
    wctx->img_obj = NULL;
    wctx->state_widget = NULL;
    wctx->state_window_widget = NULL;
    wctx->item = NULL;

    wctx->update_timestamp = 0;
    wctx->refresh_request = false;
}

void widget_create(lv_obj_t *parent, struct widget_context_s *wctx)
{
#if DEBUG_OPENHAB_UI
    printf("widget_create: type=%u\r\n", wctx->item->getType());
#endif

    lv_obj_t *cont;

    // Create widget button
    wctx->container = lv_cont_create(parent, NULL);
    lv_obj_set_click(wctx->container, true);
    lv_obj_set_event_cb(wctx->container, event_handler);
    lv_obj_set_size(wctx->container, lv_obj_get_width(parent) / 3 - LV_DPI / 16, lv_obj_get_height(parent) / 2 - LV_DPI / 16);
    lv_cont_set_fit(wctx->container, LV_FIT_NONE);
    lv_cont_set_layout(wctx->container, LV_LAYOUT_COLUMN_MID);

    //const lv_style_t *cont_style = lv_cont_get_style(cont, LV_CONT_STYLE_MAIN);
    //lv_style_copy(&wctx->container_style, cont_style);
    lv_style_init(&wctx->container_style);
    lv_style_set_margin_left(&wctx->container_style,LV_STATE_DEFAULT, -5);
    lv_style_set_margin_right(&wctx->container_style,LV_STATE_DEFAULT, -5);
    lv_style_set_margin_top(&wctx->container_style,LV_STATE_DEFAULT, -2);
    lv_style_set_margin_bottom(&wctx->container_style,LV_STATE_DEFAULT, -2);
    lv_style_set_border_color(&wctx->container_style, LV_STATE_DEFAULT, LV_COLOR_BLACK);
    lv_style_set_border_opa(&wctx->container_style, LV_STATE_DEFAULT, 64);
    lv_style_set_bg_grad_color(&wctx->container_style, LV_STATE_DEFAULT, LV_COLOR_SILVER);
    lv_style_set_bg_grad_dir(&wctx->container_style, LV_STATE_DEFAULT, LV_GRAD_DIR_VER);
    lv_obj_add_style(wctx->container, LV_CONT_PART_MAIN, &wctx->container_style);

    // Create top label object
    wctx->label = lv_label_create(wctx->container, NULL);
    lv_obj_add_style(wctx->label, LV_LABEL_PART_MAIN, &custom_style_label);
    lv_obj_set_auto_realign(wctx->label, true);
    lv_label_set_long_mode(wctx->label, LV_LABEL_LONG_BREAK);
    lv_label_set_text(wctx->label, wctx->item->getLabel());
    lv_obj_set_width(wctx->label, lv_obj_get_width(wctx->container) - LV_DPI / 20);
    lv_label_set_align(wctx->label, LV_LABEL_ALIGN_CENTER);
    lv_obj_move_foreground(wctx->label);
    lv_obj_add_protect(wctx->label, LV_PROTECT_POS | LV_PROTECT_FOLLOW);
    lv_obj_align(wctx->label, NULL, LV_ALIGN_IN_TOP_MID, 0, 3);

    // Create center image object
    wctx->img_obj = lv_img_create(wctx->container, NULL);

    if (wctx->img_dsc.data_size > 0)
    {
        lv_img_set_src(wctx->img_obj, &wctx->img_dsc);
        lv_obj_set_style_local_image_opa(wctx->img_obj, LV_IMG_PART_MAIN, LV_STATE_DEFAULT, 80);
    }
    else if (wctx->item->getType() == ItemType::type_parent_link)
    {
        lv_obj_add_style(wctx->img_obj, LV_IMG_PART_MAIN, &custom_style_label_state_large);
        lv_img_set_src(wctx->img_obj, LV_SYMBOL_NEW_LINE);
        lv_obj_set_style_local_text_opa(wctx->img_obj, LV_IMG_PART_MAIN, LV_STATE_DEFAULT, 140);
    }
    else
    {
        lv_obj_add_style(wctx->img_obj, LV_IMG_PART_MAIN, &custom_style_label_state_large);
        lv_img_set_src(wctx->img_obj, LV_SYMBOL_EYE_OPEN);
        lv_obj_set_style_local_text_opa(wctx->img_obj, LV_IMG_PART_MAIN, LV_STATE_DEFAULT, 50);
    }

    lv_obj_move_background(wctx->img_obj);
    lv_obj_add_protect(wctx->img_obj, LV_PROTECT_POS | LV_PROTECT_FOLLOW);
    lv_obj_align(wctx->img_obj, NULL, LV_ALIGN_CENTER, 0, 0);

    // Define bottom label and button boarder style
    if (   wctx->item->getType() == ItemType::type_parent_link
        || wctx->item->getType() == ItemType::type_link)
    {
        lv_style_set_border_color(&wctx->container_style, LV_STATE_DEFAULT, LV_COLOR_BLUE);
        lv_style_set_border_width(&wctx->container_style, LV_STATE_DEFAULT, 4);
        lv_label_set_align(wctx->label, LV_LABEL_ALIGN_CENTER);
        lv_obj_add_style(wctx->label, LV_LABEL_PART_MAIN, &custom_style_label_state);
        lv_obj_align(wctx->label, NULL, LV_ALIGN_CENTER, 0, 0);
    }
    else if (   wctx->item->getType() == ItemType::type_string
             || wctx->item->getType() == ItemType::type_number)
    {
        lv_obj_t *state_label = lv_label_create(wctx->container, NULL);
        lv_label_set_align(state_label, LV_LABEL_ALIGN_CENTER);
        lv_label_set_long_mode(state_label, LV_LABEL_LONG_BREAK);
        lv_style_copy(&wctx->state_widget_style, &custom_style_label_state);
        lv_obj_add_style(state_label, LV_LABEL_PART_MAIN, &wctx->state_widget_style);
        lv_obj_move_foreground(state_label);
        lv_obj_set_width(state_label, lv_obj_get_width(wctx->container));
        lv_obj_add_protect(state_label, LV_PROTECT_POS | LV_PROTECT_FOLLOW);
        lv_obj_align(state_label, NULL, LV_ALIGN_IN_BOTTOM_MID, 0, -3);

        wctx->state_widget = state_label;
    }
    else if (   wctx->item->getType() == ItemType::type_group)
    {
        lv_style_set_border_color(&wctx->container_style, LV_STATE_DEFAULT, LV_COLOR_BLUE);
        lv_style_set_border_width(&wctx->container_style, LV_STATE_DEFAULT, 4);
        lv_obj_t *state_label = lv_label_create(cont, NULL);
        lv_label_set_align(state_label, LV_LABEL_ALIGN_CENTER);
        lv_label_set_long_mode(state_label, LV_LABEL_LONG_BREAK);
        lv_style_copy(&wctx->state_widget_style, &custom_style_label_state);
        lv_obj_add_style(state_label, LV_LABEL_PART_MAIN, &wctx->state_widget_style);
        lv_obj_move_foreground(state_label);
        lv_obj_set_width(state_label, lv_obj_get_width(cont));
        lv_obj_add_protect(state_label, LV_PROTECT_POS | LV_PROTECT_FOLLOW);
        lv_obj_align(state_label, NULL, LV_ALIGN_IN_BOTTOM_MID, 0, -3);

        wctx->state_widget = state_label;
    }
    else if (   wctx->item->getType() == ItemType::type_switch
             || wctx->item->getType() == ItemType::type_setpoint
             || wctx->item->getType() == ItemType::type_slider
             || wctx->item->getType() == ItemType::type_selection
             || wctx->item->getType() == ItemType::type_rollershutter
             || wctx->item->getType() == ItemType::type_player)
    {
        lv_style_set_border_width(&wctx->container_style, LV_STATE_DEFAULT, 4);

        lv_obj_t *state_label = lv_label_create(wctx->container, NULL);
        lv_label_set_align(state_label, LV_LABEL_ALIGN_CENTER);
        lv_label_set_long_mode(state_label, LV_LABEL_LONG_BREAK);
        lv_style_copy(&wctx->state_widget_style, &custom_style_label_state);
        lv_obj_add_style(state_label, LV_LABEL_PART_MAIN, &wctx->state_widget_style);
        lv_obj_move_foreground(state_label);
        lv_obj_set_width(state_label, lv_obj_get_width(wctx->container));
        lv_obj_add_protect(state_label, LV_PROTECT_POS | LV_PROTECT_FOLLOW);
        lv_obj_align(state_label, NULL, LV_ALIGN_IN_BOTTOM_MID, 0, -3);

        wctx->state_widget = state_label;
    }
    else if (wctx->item->getType() == ItemType::type_colorpicker)
    {
        lv_style_set_border_width(&wctx->container_style, LV_STATE_DEFAULT, 4);

        lv_obj_t *state_obj = lv_obj_create(wctx->container, NULL);
        // lv_style_copy(&wctx->state_widget_style, &lv_style_pretty_color);
        lv_obj_add_style(state_obj, LV_LABEL_PART_MAIN, &wctx->state_widget_style);
        lv_obj_move_foreground(state_obj);
        lv_obj_set_width(state_obj, lv_obj_get_width(wctx->container) / 3);
        lv_obj_set_height(state_obj, 22);
        lv_obj_add_protect(state_obj, LV_PROTECT_POS | LV_PROTECT_FOLLOW);
        lv_obj_align(state_obj, NULL, LV_ALIGN_IN_BOTTOM_MID, 0, -6);

        wctx->state_widget = state_obj;
    }

    lv_obj_set_user_data(wctx->container, (lv_obj_user_data_t)wctx);
}

void show(lv_obj_t *parent)
{
    // cleanup page
    for (size_t i = 0; i < WIDGET_COUNT_MAX; i++)
    {
        free_icon(&widget_context[i].img_dsc);
        widget_destroy(parent, &widget_context[i]);
    }

    header_set_title(sitemap.getPageName());

    for (size_t i = 0; i < WIDGET_COUNT_MAX; i++)
    {
        widget_context[i].item = sitemap.getItem(i);

        if (widget_context[i].item->getType() != ItemType::type_unknown)
        {
            if (widget_context[i].item->getType() != ItemType::type_parent_link)
                load_icon(&widget_context[i]);

            widget_create(parent, &widget_context[i]);
            update_state_widget(&widget_context[i]);
        }
    }
}

//////////////////////////////////////////////////////////////////////////////
// exported

void openhab_ui_setup(Config *config)
{
    current_config = config;

    lv_style_init(&custom_style_top_bar);
    lv_style_set_text_font(&custom_style_top_bar, LV_STATE_DEFAULT, &custom_font_roboto_16);
    lv_style_set_bg_grad_color(&custom_style_top_bar, LV_STATE_DEFAULT, LV_COLOR_SILVER);
    lv_style_set_bg_grad_dir(&custom_style_top_bar, LV_STATE_DEFAULT, LV_GRAD_DIR_VER);


    // lv_style_copy(&custom_style_label_state, &lv_style_plain);
    lv_style_init(&custom_style_label_state);
    lv_style_set_text_font(&custom_style_label_state, LV_STATE_DEFAULT, &custom_font_roboto_22);
    lv_style_set_text_line_space(&custom_style_label_state, LV_STATE_DEFAULT, 0);

    // lv_style_copy(&custom_style_label_state_large, &lv_style_plain);
    lv_style_init(&custom_style_label_state_large);
    lv_style_set_text_font(&custom_style_label_state_large, LV_STATE_DEFAULT, &lv_font_montserrat_36);
    // custom_style_label_state_large.text.font = &lv_font_roboto_28;
    // custom_style_label_state_large.text.line_space = 0;

    // lv_style_copy(&custom_style_label, &lv_style_plain);
    lv_style_init(&custom_style_label);
    // lv_style_set_pad_left(&custom_style_label, LV_STATE_DEFAULT, LV_DPI / 5);
    // lv_style_set_pad_right(&custom_style_label, LV_STATE_DEFAULT, LV_DPI / 5);
    // lv_style_set_pad_bottom(&custom_style_label, LV_STATE_DEFAULT, LV_DPI / 5);
    lv_style_set_text_font(&custom_style_label, LV_STATE_DEFAULT, &custom_font_roboto_16);
    //lv_style_set_text_line_space(&custom_style_label, LV_STATE_DEFAULT, -5);

    lv_style_init(&custom_style_button);
    // custom_style_button.text.color = lv_color_make(0x20, 0x20, 0x20);
    // custom_style_button.body.main_color = LV_COLOR_WHITE;
    // custom_style_button.body.grad_color = LV_COLOR_SILVER;

    // lv_style_copy(&custom_style_button_toggle, &lv_style_btn_tgl_rel);

    lv_style_init(&custom_style_windows_header);
    lv_style_set_text_font(&custom_style_windows_header, LV_STATE_DEFAULT, &custom_font_roboto_22);

    header_create();
    content_create();
}

void openhab_ui_set_wifi_state(bool wifi_state)
{
    if (wifi_state == true)
        lv_label_set_text(header.item.wifi, LV_SYMBOL_WIFI);
    else
        lv_label_set_text(header.item.wifi, LV_SYMBOL_REFRESH);
}

void openhab_ui_connect(const char *host, uint16_t port, const char *sitemap)
{
    snprintf(current_website, sizeof(current_website), "http://%s:%u", host, port);
    snprintf(current_page, sizeof(current_page), "%s/rest/sitemaps/%s/%s?type=json", current_website, sitemap, sitemap);

    refresh_page = true;
}

void openhab_ui_loop(void)
{
    static unsigned long refresh_retry_timeout;
    static unsigned long update_ntp_next_timestamp;
    static unsigned long connection_error_handling_timestamp;
    static bool sitemap_ok;
#if DEBUG_OPENHAB_UI
    static unsigned long statistics_timestamp;
#endif
    openhab_ui_infolabel.loop();

    if (refresh_page == true && millis() > refresh_retry_timeout)
    {
        if (sitemap.openlink(current_page) == 0)
        {
            refresh_page = false;
            sitemap_ok = true;
            openhab_ui_infolabel.destroy();
            show(content);
#if DEBUG_OPENHAB_UI
            Serial.print("Free Heap: ");
            Serial.println(ESP.getFreeHeap());
#endif
            statistics.sitemap_success_cnt++;
        }
        else
        {
#if DEBUG_OPENHAB_UI
            printf("openhab_ui_loop: openlink failed: %s\r\n", current_page);
#endif
            sitemap_ok = false;
            openhab_ui_infolabel.create(openhab_ui_infolabel.ERROR, "SITEMAP ACCESS FAILED", current_page, 0);
            refresh_retry_timeout = millis() + GET_SITEMAP_RETRY_INTERVAL;

            statistics.sitemap_fail_cnt++;
        }
    }

    if (sitemap_ok == true)
    {
        for (size_t i = 0; i < WIDGET_COUNT_MAX; ++i)
        {
            if (widget_context[i].item == NULL)
                continue;

            if (   (widget_context[i].item->getType() != ItemType::type_unknown)
                && (widget_context[i].item->getType() != ItemType::type_link)
                && (widget_context[i].item->getType() != ItemType::type_parent_link))
            {
                if (widget_context[i].refresh_request == true)
                {
                    // update widget from local state
                    widget_context[i].update_timestamp = millis();
                    widget_context[i].refresh_request = false;
                    update_icon(&widget_context[i]);
                    update_state_widget(&widget_context[i]);
                    statistics.update_success_cnt++;
                }

                if (widget_context[i].update_timestamp + ITEM_UPDATE_INTERVAL < millis())
                {
                    // update widget from current remote openhab state
                    widget_context[i].update_timestamp = millis();
                    widget_context[i].refresh_request = false;
                    int result = widget_context[i].item->update(widget_context[i].item->getLink());
                    if (result > 0)
                    {
                        // item value changed
                        update_icon(&widget_context[i]);
                        update_state_widget(&widget_context[i]);
                        statistics.update_success_cnt++;
                    }
                    else if (result == 0)
                    {
                        statistics.update_success_cnt++;
                    }
                    else
                    {
                        statistics.update_fail_cnt++;
                    }
                }
            }
        }
    }

    if (update_ntp_next_timestamp < millis())
    {
        update_ntp_next_timestamp = millis() + NTP_TIME_UPDATE_INTERVAL;

#if DEBUG_OPENHAB_UI
        Serial.print("openhab_ui_loop: update time using ntp");
#endif
        configTime(current_config->item.ntp.gmt_offset * 3600, current_config->item.ntp.daylightsaving == true ? 3600 : 0, current_config->item.ntp.hostname);
    }

    header_update();

    if (millis() > connection_error_handling_timestamp + (CONNECTION_ERROR_TIMEOUT_S * 1000))
    {
        connection_error_handling_timestamp = millis();

        if ((statistics.update_fail_cnt > statistics.update_success_cnt) || (statistics.sitemap_fail_cnt > statistics.sitemap_success_cnt))
        {
            // reboot device
            ESP.restart();
        }

#if DEBUG_OPENHAB_UI
        Serial.print("STATISTICS reset\r\n");
#endif
        statistics.update_fail_cnt = 0;
        statistics.update_success_cnt = 0;
        statistics.sitemap_fail_cnt = 0;
        statistics.sitemap_success_cnt = 0;
    }

#if DEBUG_OPENHAB_UI
    if (millis() > statistics_timestamp + (10 * 1000))
    {
        statistics_timestamp = millis();

        uptime::calculateUptime();

        Serial.printf("STATISTICS Uptime: %lu days, %02lu:%02lu:%02lu UpdSucc: %u UpdFail: %u SiteSucc: %u SiteFail: %u\r\n",
                      uptime::getDays(), uptime::getHours(), uptime::getMinutes(), uptime::getSeconds(),
                      statistics.update_success_cnt, statistics.update_fail_cnt, statistics.sitemap_success_cnt, statistics.sitemap_fail_cnt);
    }
#endif
}
