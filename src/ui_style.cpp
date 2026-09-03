#include "ui_style.hpp"

lv_style_t ui_style_tile;
lv_style_t ui_style_tile_pressed;
lv_style_t ui_style_tile_link;
lv_style_t ui_style_tile_active;
lv_style_t ui_style_label;
lv_style_t ui_style_label_state;
lv_style_t ui_style_label_large;
lv_style_t ui_style_win_header;
lv_style_t ui_style_btn;
lv_style_t ui_style_btn_checked;
lv_style_t ui_style_slider;
lv_style_t ui_style_slider_knob;

/* Carried over from the v7 theme: LV_THEME_DEFAULT_COLOR_PRIMARY was
 * LV_COLOR_MAKE(0x00, 0x80, 0xFF) and the borders were a near-black navy. */
#define COLOR_PRIMARY  lv_color_make(0x00, 0x80, 0xFF)
#define COLOR_BORDER   lv_color_make(0x0b, 0x19, 0x28)
#define COLOR_SILVER   lv_color_make(0xc0, 0xc0, 0xc0)
#define COLOR_GRAY     lv_color_make(0x80, 0x80, 0x80)

/* The theme derived most of its metrics from LV_DPI. LV_DPI_DEF is pinned to
 * the v7 value of 100 in lv_conf.h so these keep producing the same geometry. */
#define BORDER_THIN    (LV_DPI_DEF / 50 >= 1 ? LV_DPI_DEF / 50 : 1)

/* White-to-silver vertical gradient with a thin dark border: the look every
 * raised surface in this UI shares. */
static void style_raised(lv_style_t *style)
{
    lv_style_set_bg_opa(style, LV_OPA_COVER);
    lv_style_set_bg_color(style, lv_color_white());
    lv_style_set_bg_grad_color(style, COLOR_SILVER);
    lv_style_set_bg_grad_dir(style, LV_GRAD_DIR_VER);
    lv_style_set_border_color(style, COLOR_BORDER);
    lv_style_set_border_width(style, BORDER_THIN);
    lv_style_set_border_opa(style, LV_OPA_70);
}

void ui_style_init(void)
{
    /* ---- tiles ---- */
    lv_style_init(&ui_style_tile);
    style_raised(&ui_style_tile);
    lv_style_set_radius(&ui_style_tile, LV_DPI_DEF / 15);
    lv_style_set_text_color(&ui_style_tile, lv_color_black());
    lv_style_set_text_font(&ui_style_tile, &custom_font_roboto_16);
    lv_style_set_pad_all(&ui_style_tile, LV_DPI_DEF / 20);
    /* The tile's own border overrides the raised default: thicker, black and
     * fainter, so that the coloured link/active borders stand out against it. */
    lv_style_set_border_width(&ui_style_tile, 2);
    lv_style_set_border_color(&ui_style_tile, lv_color_black());
    lv_style_set_border_opa(&ui_style_tile, LV_OPA_30);

    lv_style_init(&ui_style_tile_pressed);
    lv_style_set_bg_color(&ui_style_tile_pressed, lv_color_darken(COLOR_PRIMARY, LV_OPA_60));
    lv_style_set_bg_grad_color(&ui_style_tile_pressed, lv_color_darken(COLOR_PRIMARY, LV_OPA_30));
    lv_style_set_bg_grad_dir(&ui_style_tile_pressed, LV_GRAD_DIR_VER);
    lv_style_set_text_color(&ui_style_tile_pressed, COLOR_SILVER);

    /* Navigation tiles (links and groups) get a blue border. */
    lv_style_init(&ui_style_tile_link);
    lv_style_set_border_color(&ui_style_tile_link, lv_palette_main(LV_PALETTE_BLUE));
    lv_style_set_border_width(&ui_style_tile_link, 4);
    lv_style_set_border_opa(&ui_style_tile_link, LV_OPA_COVER);

    /* Controllable items (switch, slider, ...) get a thicker neutral border. */
    lv_style_init(&ui_style_tile_active);
    lv_style_set_border_width(&ui_style_tile_active, 4);

    /* ---- text ---- */
    lv_style_init(&ui_style_label);
    lv_style_set_text_font(&ui_style_label, &custom_font_roboto_16);

    lv_style_init(&ui_style_label_state);
    lv_style_set_text_font(&ui_style_label_state, &custom_font_roboto_22);
    lv_style_set_text_line_space(&ui_style_label_state, 0);

    lv_style_init(&ui_style_label_large);
    lv_style_set_text_font(&ui_style_label_large, &custom_font_roboto_36);

    /* ---- item windows ---- */
    lv_style_init(&ui_style_win_header);
    lv_style_set_radius(&ui_style_win_header, 5);
    lv_style_set_bg_opa(&ui_style_win_header, LV_OPA_COVER);
    lv_style_set_bg_color(&ui_style_win_header, COLOR_PRIMARY);
    lv_style_set_bg_grad_dir(&ui_style_win_header, LV_GRAD_DIR_NONE);
    lv_style_set_text_color(&ui_style_win_header, lv_color_white());
    lv_style_set_text_font(&ui_style_win_header, &custom_font_roboto_22);
    lv_style_set_border_width(&ui_style_win_header, 0);
    lv_style_set_pad_hor(&ui_style_win_header, 10);
    lv_style_set_pad_ver(&ui_style_win_header, 5);

    lv_style_init(&ui_style_btn);
    style_raised(&ui_style_btn);
    lv_style_set_radius(&ui_style_btn, LV_DPI_DEF / 15);
    lv_style_set_text_color(&ui_style_btn, lv_color_black());
    lv_style_set_text_font(&ui_style_btn, &custom_font_roboto_16);
    lv_style_set_pad_all(&ui_style_btn, LV_DPI_DEF / 12);

    /* v7 marked the active choice by forcing the button into LV_BTN_STATE_PRESSED.
     * v9 has no such call, so the buttons are checkable and this is the
     * LV_STATE_CHECKED look -- deliberately the old pressed colours. */
    lv_style_init(&ui_style_btn_checked);
    lv_style_set_bg_color(&ui_style_btn_checked, lv_color_darken(COLOR_PRIMARY, LV_OPA_60));
    lv_style_set_bg_grad_color(&ui_style_btn_checked, lv_color_darken(COLOR_PRIMARY, LV_OPA_30));
    lv_style_set_bg_grad_dir(&ui_style_btn_checked, LV_GRAD_DIR_VER);
    lv_style_set_text_color(&ui_style_btn_checked, COLOR_SILVER);

    lv_style_init(&ui_style_slider);
    style_raised(&ui_style_slider);
    lv_style_set_radius(&ui_style_slider, LV_DPI_DEF / 20);
    lv_style_set_pad_all(&ui_style_slider, LV_DPI_DEF / 20);

    lv_style_init(&ui_style_slider_knob);
    style_raised(&ui_style_slider_knob);
    lv_style_set_radius(&ui_style_slider_knob, LV_DPI_DEF / 20);
}
