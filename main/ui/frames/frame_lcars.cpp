/**
 * @file frame_lcars.cpp
 *
 * The LCARS frame: a spine down the left edge sweeping into a bar across the
 * top, with the tile grid inset in the crook of it.
 *
 * ui_style.cpp used to say real elbows were not drawable, because LVGL v9 has
 * one uniform radius property rather than four and this build has neither arcs
 * nor a canvas. It is drawable: with one radius you get exactly one rounded
 * corner by covering the other three with square-cornered rects of the same
 * colour, and the concave inner corner is a rounded rect in the *background*
 * colour laid over the join.
 *
 * The whole frame is nine objects and every one of them is a flat fill, which
 * is the cheapest thing the software renderer draws.
 *
 *      0   44      80 84                  248 252      316 320
 *    0 +---+--------+  +--------------------+  +--------+
 *      |####### r22 |  |  PAGE TITLE        |  | 21:47  |   34   bar
 *   34 |####+-------+  +--------------------+  +--------+
 *      |####|  <- the notch's top-left fillet is the inner sweep
 *   90 |####|
 *   96 +----+
 *      |####|  wifi + RSSI, and the way into the settings screen        68
 *  164 +----+
 *  170 +----+
 *      |####|  a cell tag, and the rounded cap the spine ends on        70
 *  240 +----+
 *
 * What is left for the tiles is (52, 42) to (315, 235): 264 x 194, which at a
 * 6 px gutter is six 84 x 94 tiles. The floor for a finger is 73 x 71.
 */
#include "ui_frame.hpp"

#include "ui/ui_style.hpp"

#include <stdio.h>

#define BAR_H     34
#define SPINE_W   44
#define ELBOW_W   80
#define ELBOW_H   90
#define R_OUTER   22
#define R_NOTCH   14
#define GUTTER    4
#define SPINE_GAP 6

#define STATUS_Y  96
#define STATUS_H  68
#define DECO_Y    170
#define DECO_H    70

static struct
{
    lv_obj_t *root;
    lv_obj_t *title;
    lv_obj_t *clock;
    lv_obj_t *wifi;
    lv_obj_t *signal;
} lcars;

/* Black on a colour block, which is what LCARS text always is. */
static lv_obj_t *block_label(lv_obj_t *parent, const char *text, const lv_font_t *font,
                             uint32_t colour)
{
    lv_obj_t *label = lv_label_create(parent);

    lv_label_set_text(label, text);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_color(label, lv_color_hex(colour), 0);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_letter_space(label, ui_style_theme()->letter_space, 0);
    lv_obj_remove_flag(label, LV_OBJ_FLAG_CLICKABLE);

    return label;
}

static void lcars_build(lv_obj_t *parent)
{
    const struct ui_theme_s *t = ui_style_theme();

    uint32_t primary = t->header.bg;   /* orange  */
    uint32_t second  = t->link.color;  /* sky     */
    uint32_t third   = t->btn.bg;      /* salmon  */
    uint32_t ink     = t->header.text; /* black   */
    uint32_t ground  = t->screen.bg;   /* black   */

    lcars.root = ui_frame_container(parent);
    lv_obj_set_pos(lcars.root, 0, 0);
    lv_obj_set_size(lcars.root, lv_pct(100), lv_pct(100));
    /* The frame is behind the tiles and must never eat a touch meant for one. */
    lv_obj_remove_flag(lcars.root, LV_OBJ_FLAG_CLICKABLE);

    /* The elbow. One rounded plate, three patches that un-round the corners
     * which have to stay square, and a notch in the background colour that
     * cuts the L out of it and leaves the concave sweep behind. */
    ui_frame_block(lcars.root, 0, 0, ELBOW_W, ELBOW_H, primary, R_OUTER);
    ui_frame_block(lcars.root, ELBOW_W - R_OUTER, 0, R_OUTER, R_OUTER, primary, 0);
    ui_frame_block(lcars.root, ELBOW_W - R_OUTER, ELBOW_H - R_OUTER, R_OUTER, R_OUTER, primary, 0);
    ui_frame_block(lcars.root, 0, ELBOW_H - R_OUTER, R_OUTER, R_OUTER, primary, 0);

    /* Overhangs the plate by its own radius on the right and the bottom, so
     * three of its four rounded corners land on ground that is already this
     * colour and only the top-left one is ever seen. */
    ui_frame_block(lcars.root, SPINE_W, BAR_H, ELBOW_W - SPINE_W + R_NOTCH,
                   ELBOW_H - BAR_H + R_NOTCH, ground, R_NOTCH);

    /* The bar's two blocks. LV_RADIUS_CIRCLE clamps to half the height, so
     * these are true stadiums -- the shape LCARS ends a run of blocks with. */
    int16_t title_x = ELBOW_W + GUTTER;
    int16_t clock_w = 64;
    int16_t clock_x = LV_HOR_RES - GUTTER - clock_w;
    int16_t title_w = (int16_t)(clock_x - GUTTER - title_x);

    lv_obj_t *title_block =
        ui_frame_block(lcars.root, title_x, 0, title_w, BAR_H, primary, LV_RADIUS_CIRCLE);
    lv_obj_set_style_pad_hor(title_block, 14, 0);
    lcars.title = block_label(title_block, "", t->font_normal, ink);
    /* Both, or LONG_DOT has nothing to clip against: with a content-sized
     * height the label grows a second line and spills out of a block that
     * neither scrolls nor clips. One line of the face it is set in is the
     * height that makes it dot instead. */
    lv_obj_set_width(lcars.title, title_w - 28);
    lv_obj_set_height(lcars.title, lv_font_get_line_height(t->font_normal));
    lv_obj_center(lcars.title);

    lv_obj_t *clock_block =
        ui_frame_block(lcars.root, clock_x, 0, clock_w, BAR_H, second, LV_RADIUS_CIRCLE);
    lcars.clock = block_label(clock_block, "--:--", t->font_normal, ink);
    lv_obj_center(lcars.clock);

    /* The spine's cells. The status one is where the settings screen is
     * reached from, which is what the whole status bar used to be. */
    lv_obj_t *status =
        ui_frame_block(lcars.root, 0, STATUS_Y, SPINE_W, STATUS_H, second, 0);
    lv_obj_set_flex_flow(status, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(status, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(status, 2, 0);
    ui_frame_settings_target(status);

    lcars.wifi   = block_label(status, LV_SYMBOL_POWER, t->font_small, ink);
    lcars.signal = block_label(status, "--", t->font_small, ink);

    /* The bottom cell, and the rounded cap the spine ends on: the block is
     * rounded and a square patch flattens the top where it meets the gap. */
    ui_frame_block(lcars.root, 0, DECO_Y, SPINE_W, DECO_H, third, R_NOTCH);
    ui_frame_block(lcars.root, 0, DECO_Y, SPINE_W, R_NOTCH, third, 0);

    /* The numeric tags LCARS panels are covered in. Decoration, and it says
     * so: a fixed string rather than anything pretending to be a reading. */
    lv_obj_t *tag = block_label(lcars.root, "47-1701\nD", t->font_small, ink);
    lv_obj_set_style_text_align(tag, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(tag, LV_ALIGN_BOTTOM_LEFT, 6, -10);
}

static void lcars_destroy(void)
{
    if (lcars.root != NULL)
        lv_obj_delete(lcars.root);

    lcars = {};
}

static lv_area_t lcars_content_area(void)
{
    lv_area_t a;

    a.x1 = SPINE_W + 8;
    a.y1 = BAR_H + 8;
    a.x2 = LV_HOR_RES - GUTTER - 1;
    a.y2 = LV_VER_RES - GUTTER - 1;

    return a;
}

static void lcars_set_title(const char *title)
{
    if (lcars.title != NULL)
        lv_label_set_text(lcars.title, title);
}

static void lcars_set_clock(const char *text)
{
    if (lcars.clock != NULL)
        lv_label_set_text(lcars.clock, text);
}

static void lcars_set_link(bool online, int rssi)
{
    if (lcars.wifi != NULL)
        lv_label_set_text(lcars.wifi, online ? LV_SYMBOL_WIFI : LV_SYMBOL_REFRESH);

    if (lcars.signal == NULL)
        return;

    if (rssi < 0)
        lv_label_set_text(lcars.signal, LV_SYMBOL_SHUFFLE);
    else
        lv_label_set_text_fmt(lcars.signal, "%d", rssi);
}

/* Colour is semantics here, which is both more authentic than one tile colour
 * for everything and cheaper: a flat fill with no gradient and no border is
 * the fastest path the renderer has.
 *
 * The four colours are pulled from the theme rather than named, so the night
 * variant follows without a second table. */
static void lcars_decorate_tile(lv_obj_t *tile, enum ItemType type, uint8_t slot)
{
    LV_UNUSED(slot);

    if (tile == NULL)
        return;

    const struct ui_theme_s *t = ui_style_theme();
    lv_color_t               fill;

    switch (type)
    {
    case ItemType::type_parent_link:
    case ItemType::type_link:
        fill = lv_color_hex(t->link.color); /* sky    */
        break;

    case ItemType::type_group:
        fill = lv_color_hex(t->btn.bg); /* salmon */
        break;

    case ItemType::type_string:
    case ItemType::type_number:
        /* Nothing to operate, so neither the control colour nor the navigation
         * one. Mixed rather than named, to keep this off the theme table. */
        fill = lv_color_mix(lv_color_hex(t->tile.bg), lv_color_hex(t->link.color), 128);
        break;

    default:
        fill = lv_color_hex(t->tile.bg); /* lilac  */
        break;
    }

    lv_obj_set_style_bg_color(tile, fill, 0);
}

const struct ui_frame_ops_s ui_frame_lcars = {
    lcars_build,     lcars_destroy,  lcars_content_area,  lcars_set_title,
    lcars_set_clock, lcars_set_link, lcars_decorate_tile};
