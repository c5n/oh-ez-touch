/**
 * @file ui_widgets.cpp
 *
 * See ui_widgets.hpp.
 */
#include "ui_widgets.hpp"

#include "ui_motion.hpp"
#include "ui_style.hpp"

#include <string.h>

lv_obj_t *ui_plain_container(lv_obj_t *parent)
{
    lv_obj_t *obj = lv_obj_create(parent);

    lv_obj_set_scrollable(obj, false);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_set_style_pad_gap(obj, 0, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_radius(obj, 0, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);

    return obj;
}

lv_obj_t *ui_themed_button(lv_obj_t *parent, const char *text)
{
    lv_obj_t *btn = lv_button_create(parent);

    lv_obj_add_style(btn, &ui_style_btn, LV_PART_MAIN);
    lv_obj_add_style(btn, &ui_style_btn_checked,
                     ui_style_selector(LV_PART_MAIN, LV_STATE_CHECKED));
    lv_obj_add_style(btn, &ui_style_btn_checked,
                     ui_style_selector(LV_PART_MAIN, LV_STATE_PRESSED));

    /* After the pressed style, so the transition governs what it sets. */
    ui_motion_pressable(btn);

    lv_obj_set_size(btn, LV_SIZE_CONTENT, LV_SIZE_CONTENT);

    lv_obj_t *label = lv_label_create(btn);

    lv_label_set_text(label, text);
    lv_obj_center(label);

    return btn;
}

lv_obj_t *ui_back_bar(lv_obj_t *parent, const char *symbol, const char *title,
                      lv_event_cb_t cb)
{
    lv_obj_t *bar = lv_obj_create(parent);

    lv_obj_set_scrollable(bar, false);
    lv_obj_set_size(bar, lv_pct(100), UI_BAR_H);
    lv_obj_set_pos(bar, 0, 0);
    lv_obj_add_style(bar, &ui_style_win_header, LV_PART_MAIN);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_set_style_pad_hor(bar, 10, 0);
    lv_obj_set_style_pad_ver(bar, 0, 0);
    lv_obj_set_style_pad_column(bar, 10, 0);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    lv_obj_set_clickable(bar, true);
    lv_obj_add_event_cb(bar, cb, LV_EVENT_CLICKED, NULL);
    ui_motion_pressable(bar);

    lv_obj_t *chevron = lv_label_create(bar);

    lv_label_set_text(chevron, symbol);

    lv_obj_t *label = lv_label_create(bar);

    lv_label_set_text(label, title);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_obj_set_flex_grow(label, 1);

    /* The bar's children must not eat the tap: the whole bar is the target. */
    lv_obj_set_clickable(chevron, false);
    lv_obj_set_clickable(label, false);

    return bar;
}

/* ------------------------------------------------------------- the reading */

/* Child order is the contract between the setters and reading_arrange(). */
#define READING_VALUE 0
#define READING_UNIT  1

/* Centre the pair and level the unit at the bottom of the value's line.
 *
 * Called from the setters and from the box's own SIZE_CHANGED: the width is
 * lv_pct(100), which resolves a layout pass after creation, and until then
 * there is nothing to centre in -- the event asks again once there is. */
static void reading_arrange(lv_obj_t *reading)
{
    lv_obj_t *value = lv_obj_get_child(reading, READING_VALUE);
    lv_obj_t *unit  = lv_obj_get_child(reading, READING_UNIT);

    if (value == NULL || unit == NULL)
        return;

    int32_t avail = lv_obj_get_content_width(reading);

    if (avail <= 0)
        return;

    const char *vtext = lv_label_get_text(value);
    const char *utext = lv_label_get_text(unit);

    const lv_font_t *vfont = lv_obj_get_style_text_font(value, LV_PART_MAIN);
    const lv_font_t *ufont = lv_obj_get_style_text_font(unit, LV_PART_MAIN);

    lv_point_t vsize;
    lv_point_t usize;

    lv_text_get_size(&vsize, vtext, vfont,
                     lv_obj_get_style_text_letter_space(value, LV_PART_MAIN),
                     0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    lv_text_get_size(&usize, utext, ufont,
                     lv_obj_get_style_text_letter_space(unit, LV_PART_MAIN),
                     0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);

    int32_t uw = (utext[0] == '\0') ? 0 : usize.x;
    int32_t vw = vsize.x;

    /* A value too wide for the box loses its end to the dot, as it always
     * has; the unit keeps its place. */
    if (vw > avail - uw)
        vw = (avail > uw) ? avail - uw : 0;

    lv_obj_set_width(value, vw);

    int32_t x = (avail - vw - uw) / 2;

    lv_obj_set_pos(value, x, 0);
    /* Baselines level, not box bottoms: the smaller face carries
     * proportionally less descent, and levelling the boxes sets its baseline
     * a pixel or two below the value's. */
    lv_obj_set_pos(unit, x + vw,
                   (vfont->line_height - vfont->base_line) -
                   (ufont->line_height - ufont->base_line));
}

static void reading_size_event(lv_event_t *e)
{
    reading_arrange((lv_obj_t *)lv_event_get_target(e));
}

lv_obj_t *ui_reading_create(lv_obj_t *parent, lv_style_t *value_style,
                            lv_style_t *unit_style, int32_t height)
{
    lv_obj_t *reading = ui_plain_container(parent);

    lv_obj_set_size(reading, lv_pct(100), height);
    lv_obj_add_event_cb(reading, reading_size_event, LV_EVENT_SIZE_CHANGED, NULL);

    lv_obj_t *value = lv_label_create(reading);

    lv_obj_add_style(value, value_style, LV_PART_MAIN);
    lv_label_set_long_mode(value, LV_LABEL_LONG_DOT);
    lv_label_set_text(value, "");

    lv_obj_t *unit = lv_label_create(reading);

    lv_obj_add_style(unit, unit_style, LV_PART_MAIN);
    lv_label_set_text(unit, "");

    /* Neither label is a target: a tap belongs to whatever the reading sits
     * on -- the tile, not the text. */
    lv_obj_set_clickable(value, false);
    lv_obj_set_clickable(unit, false);

    return reading;
}

void ui_reading_set_text(lv_obj_t *reading, const char *text)
{
    lv_obj_t *value = lv_obj_get_child(reading, READING_VALUE);
    lv_obj_t *unit  = lv_obj_get_child(reading, READING_UNIT);

    if (value == NULL || unit == NULL)
        return;

    lv_label_set_text(value, text);
    lv_label_set_text(unit, "");

    reading_arrange(reading);
}

void ui_reading_set_pattern(lv_obj_t *reading, const char *pattern, float value)
{
    lv_obj_t *value_label = lv_obj_get_child(reading, READING_VALUE);
    lv_obj_t *unit_label  = lv_obj_get_child(reading, READING_UNIT);

    if (value_label == NULL || unit_label == NULL)
        return;

    /* Find the end of the conversion: skip over "%%" escapes, the flags, the
     * width and the precision; what is left is the conversion character.
     * Everything before and including it is the value's format, everything
     * after it is the unit. */
    const char *p = pattern;

    while (*p != '\0')
    {
        if (*p++ != '%')
            continue;

        if (*p == '%')
        {
            p++;
            continue;
        }

        while (strchr("-+ #0", *p) != NULL)
            p++;
        while (*p >= '0' && *p <= '9')
            p++;
        if (*p == '.')
        {
            p++;
            while (*p >= '0' && *p <= '9')
                p++;
        }

        break;
    }

    /* No conversion -- a pattern openHAB should not send. Show it whole
     * rather than guess. */
    if (*p == '\0' || p - pattern >= 31)
    {
        ui_reading_set_text(reading, pattern);
        return;
    }

    char fmt[32];

    memcpy(fmt, pattern, (size_t)(p - pattern) + 1);
    fmt[p - pattern + 1] = '\0';

    /* "%d" -- what openHAB sends when it has no pattern of its own -- needs an
     * integer argument; everything else is fed the float. */
    if (*p == 'd')
        lv_label_set_text_fmt(value_label, fmt, (uint16_t)value);
    else
        lv_label_set_text_fmt(value_label, fmt, value);

    /* The unit, with "%%" collapsed back to the per cent it means. */
    char unit_text[32];
    size_t u = 0;

    for (p++; p[0] != '\0' && u < sizeof(unit_text) - 1; p++)
    {
        if (p[0] == '%' && p[1] == '%')
            p++;
        unit_text[u++] = *p;
    }
    unit_text[u] = '\0';

    lv_label_set_text(unit_label, unit_text);

    reading_arrange(reading);
}
