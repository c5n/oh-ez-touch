/**
 * @file ui_frame_common.cpp
 *
 * The pieces every family's frame is built from.
 */
#include "ui_frame.hpp"

#include "ui/ui_beep.hpp"
#include "ui/ui_messagebox.hpp"
#include "ui/ui_motion.hpp"
#include "ui/ui_settings.hpp"
#include "ui/ui_style.hpp"
#include "ui/ui_widgets.hpp"

lv_obj_t *ui_frame_container(lv_obj_t *parent)
{
    /* The shared one. Kept under this name because it is what the frame
     * interface offers its implementations. */
    return ui_plain_container(parent);
}

static void settings_event(lv_event_t *e)
{
    LV_UNUSED(e);

    if (ui_settings_is_open() == false)
    {
        BEEPER_EVENT_SCREEN();
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

    lv_obj_set_clickable(obj, true);
    lv_obj_add_event_cb(obj, settings_event, LV_EVENT_CLICKED, NULL);
}

/* ------------------------------------------------------ the banner indicator
 *
 * The one thing on the chrome that is not a reading: it says a message box is
 * waiting, and touching it brings back one that has been folded away.
 *
 * It is a label rather than a button because every family already has a row or
 * a cell of labels to put it in, and a glyph that appears and disappears in
 * one of those costs nothing to lay out -- LVGL skips a hidden child, so the
 * row closes up again by itself. The extended click area is what makes a
 * glyph-sized indicator a finger-sized target. */

static void notice_event(lv_event_t *e)
{
    LV_UNUSED(e);

    Messagebox::unfold();
}

lv_obj_t *ui_frame_notice(lv_obj_t *parent)
{
    lv_obj_t *label = lv_label_create(parent);

    lv_label_set_text(label, "");
    lv_obj_set_hidden(label, true);
    lv_obj_set_clickable(label, true);
    lv_obj_set_ext_click_area(label, 10);
    lv_obj_add_event_cb(label, notice_event, LV_EVENT_CLICKED, NULL);

    return label;
}

const char *ui_frame_notice_glyph(enum ui_notice_e notice)
{
    switch (notice)
    {
    /* A bell, a warning triangle and a cross. Three glyphs rather than one in
     * three colours, because the families disagree about how much colour the
     * chrome may carry -- Material's band is one dim ink and LCARS's cells are
     * all colour -- and a shape reads the same in both. */
    case UI_NOTICE_INFO:    return LV_SYMBOL_BELL;
    case UI_NOTICE_WARNING: return LV_SYMBOL_WARNING;
    case UI_NOTICE_ERROR:   return LV_SYMBOL_CLOSE;
    default:                return NULL;
    }
}

uint32_t ui_frame_notice_color(enum ui_notice_e notice)
{
    const struct ui_theme_s *t = ui_style_theme();

    switch (notice)
    {
    case UI_NOTICE_WARNING: return t->info_warning_bg;
    case UI_NOTICE_ERROR:   return t->info_error_bg;
    default:                break;
    }

    /* The edge of an info box, which is the colour that variant has already
     * chosen to mean "something is being said". A variant that leaves it at
     * UI_COLOR_KEEP is saying it wants the box's own text colour, so the
     * indicator takes the screen's ink rather than painting itself black. */
    return (t->info.border == UI_COLOR_KEEP) ? t->screen.text : t->info.border;
}

void ui_frame_notice_set(lv_obj_t *label, enum ui_notice_e notice)
{
    const char *glyph = ui_frame_notice_glyph(notice);

    if (label == NULL)
        return;

    if (glyph == NULL)
    {
        lv_obj_set_hidden(label, true);
        return;
    }

    lv_label_set_text(label, glyph);
    lv_obj_set_style_text_color(label, lv_color_hex(ui_frame_notice_color(notice)), 0);
    lv_obj_set_hidden(label, false);
}

/* ------------------------------------------------------------------------- */

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

    lv_obj_set_scrollable(obj, false);
    lv_obj_set_clickable(obj, false);
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
    lv_obj_t *notice;
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

    /* Last in the row, past the link readout: the corner a message is least
     * likely to be confused with a reading. */
    classic.notice = ui_frame_notice(classic.bar);
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

static void classic_set_notice(enum ui_notice_e notice)
{
    ui_frame_notice_set(classic.notice, notice);
}

const struct ui_frame_ops_s ui_frame_classic = {
    classic_build,     classic_destroy,  classic_content_area, classic_set_title,
    classic_set_clock, classic_set_link, classic_set_notice,   NULL};
