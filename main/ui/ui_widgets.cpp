/**
 * @file ui_widgets.cpp
 *
 * See ui_widgets.hpp.
 */
#include "ui_widgets.hpp"

#include "ui_motion.hpp"
#include "ui_style.hpp"

lv_obj_t *ui_plain_container(lv_obj_t *parent)
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

    lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
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

    lv_obj_add_flag(bar, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(bar, cb, LV_EVENT_CLICKED, NULL);
    ui_motion_pressable(bar);

    lv_obj_t *chevron = lv_label_create(bar);

    lv_label_set_text(chevron, symbol);

    lv_obj_t *label = lv_label_create(bar);

    lv_label_set_text(label, title);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_obj_set_flex_grow(label, 1);

    /* The bar's children must not eat the tap: the whole bar is the target. */
    lv_obj_remove_flag(chevron, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(label, LV_OBJ_FLAG_CLICKABLE);

    return bar;
}
