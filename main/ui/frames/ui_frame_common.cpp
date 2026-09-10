/**
 * @file ui_frame_common.cpp
 *
 * The pieces every family's frame is built from.
 */
#include "ui_frame.hpp"

#include "ui/ui_beep.hpp"
#include "ui/ui_motion.hpp"
#include "ui/ui_settings.hpp"

lv_obj_t *ui_frame_container(lv_obj_t *parent)
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

static void settings_event(lv_event_t *e)
{
    LV_UNUSED(e);

    if (ui_settings_is_open() == false)
    {
        BEEPER_EVENT_WINDOW();
        /* The index, not the Info tab. Info was the sensible landing place
         * while the sections were tabs you could see from anywhere; now that
         * they are behind an index, dropping the user into one of them and
         * making them go back is a step for nothing. */
        ui_settings_open(SETTINGS_TAB_COUNT);
    }
}

void ui_frame_settings_target(lv_obj_t *obj)
{
    if (obj == NULL)
        return;

    lv_obj_add_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(obj, settings_event, LV_EVENT_CLICKED, NULL);
}

void ui_frame_clock_steady(char *dst, size_t size, const char *text)
{
    size_t i = 0;

    if (dst == NULL || size == 0)
        return;

    for (; text[i] != '\0' && i < size - 1; i++)
        dst[i] = (text[i] == ' ') ? ':' : text[i];

    dst[i] = '\0';
}

lv_obj_t *ui_frame_block(lv_obj_t *parent, int16_t x, int16_t y, int16_t w, int16_t h,
                         uint32_t color, int16_t radius)
{
    lv_obj_t *obj = lv_obj_create(parent);

    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_radius(obj, radius, 0);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);

    return obj;
}

/* ------------------------------------------------------------ the classic frame
 *
 * A flex row of clock, title, signal and WLAN glyph, a third of an inch tall,
 * over a content area filling the rest. This is what every family looked like
 * before they were given frames of their own, and it stays as the one a family
 * uses until its own arrives.
 *
 * The height is fixed rather than LV_SIZE_CONTENT, because centring children
 * inside a content-sized parent would be circular. */

#define CLASSIC_HEADER_H (LV_DPI_DEF / 3)

static struct
{
    lv_obj_t *bar;
    lv_obj_t *clock;
    lv_obj_t *title;
    lv_obj_t *signal;
    lv_obj_t *wifi;
} classic;

static void classic_build(lv_obj_t *parent)
{
    classic.bar = ui_frame_container(parent);
    lv_obj_set_size(classic.bar, lv_pct(100), CLASSIC_HEADER_H);
    lv_obj_set_pos(classic.bar, 0, 0);
    lv_obj_set_style_pad_hor(classic.bar, LV_DPI_DEF / 10, 0);
    lv_obj_set_style_pad_column(classic.bar, LV_DPI_DEF / 20, 0);
    lv_obj_set_flex_flow(classic.bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(classic.bar, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    ui_frame_settings_target(classic.bar);

    classic.clock = lv_label_create(classic.bar);
    lv_label_set_text(classic.clock, "--:--");

    classic.title = lv_label_create(classic.bar);
    lv_label_set_text(classic.title, "Welcome to OhEzTouch");
    lv_label_set_long_mode(classic.title, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(classic.title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_flex_grow(classic.title, 1);

    classic.signal = lv_label_create(classic.bar);
    lv_label_set_text(classic.signal, "  %");

    classic.wifi = lv_label_create(classic.bar);
    lv_label_set_text(classic.wifi, LV_SYMBOL_POWER);
}

static void classic_destroy(void)
{
    if (classic.bar != NULL)
        lv_obj_delete(classic.bar);

    classic = {};
}

static lv_area_t classic_content_area(void)
{
    lv_area_t a;

    a.x1 = 0;
    a.y1 = CLASSIC_HEADER_H;
    a.x2 = lv_display_get_horizontal_resolution(NULL) - 1;
    a.y2 = lv_display_get_vertical_resolution(NULL) - 1;

    return a;
}

static void classic_set_title(const char *title)
{
    if (classic.title != NULL)
        lv_label_set_text(classic.title, title);
}

static void classic_set_clock(const char *text)
{
    if (classic.clock != NULL)
        lv_label_set_text(classic.clock, text);
}

static void classic_set_link(bool online, int rssi)
{
    if (classic.wifi != NULL)
        lv_label_set_text(classic.wifi, online ? LV_SYMBOL_WIFI : LV_SYMBOL_REFRESH);

    if (classic.signal == NULL)
        return;

    /* A wired link has no signal strength and a percentage would be invented. */
    if (rssi < 0)
        lv_label_set_text(classic.signal, LV_SYMBOL_SHUFFLE);
    else
        lv_label_set_text_fmt(classic.signal, "%02d%%", rssi);
}

const struct ui_frame_ops_s ui_frame_classic = {
    classic_build, classic_destroy, classic_content_area,
    classic_set_title, classic_set_clock, classic_set_link, NULL};
