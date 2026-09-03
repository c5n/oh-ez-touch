#ifndef UI_STYLE_HPP
#define UI_STYLE_HPP

#include <lvgl.h>

/* The shared styles of the OhEzTouch UI.
 *
 * These replace the hand-forked LVGL v7 theme that used to live in
 * src/themes/. A theme is machinery for styling widgets you do not control;
 * this application creates every widget it draws, and only about eight kinds of
 * them, so the styles are simply applied at the creation sites. LVGL's built-in
 * "simple" theme supplies the defaults underneath (see main.cpp).
 *
 * Call ui_style_init() once, before creating any widget. */
void ui_style_init(void);

/* Build a style selector out of a part and a state. lv_obj_add_style() takes an
 * lv_style_selector_t, but LV_PART_* and LV_STATE_* are two distinct enums, and
 * C++20 deprecates combining those with `|` directly. */
static inline lv_style_selector_t ui_style_selector(lv_part_t part, lv_state_t state)
{
    return (lv_style_selector_t)part | (lv_style_selector_t)state;
}

/* Widget tiles on the main page. `link` and `active` are additive: they are
 * applied on top of ui_style_tile to mark navigation and controllable items. */
extern lv_style_t ui_style_tile;
extern lv_style_t ui_style_tile_pressed;
extern lv_style_t ui_style_tile_link;
extern lv_style_t ui_style_tile_active;

/* Text. `label` is the tile caption, `state` the state line along the bottom
 * edge, `large` the oversized text in item windows. */
extern lv_style_t ui_style_label;
extern lv_style_t ui_style_label_state;
extern lv_style_t ui_style_label_large;

/* Item windows. */
extern lv_style_t ui_style_win_header;
extern lv_style_t ui_style_btn;
extern lv_style_t ui_style_btn_checked;
extern lv_style_t ui_style_slider;
extern lv_style_t ui_style_slider_knob;

#endif
