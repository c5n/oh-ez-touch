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
 *    0 .---+--------+  +--------------------+  +--------.
 *      |####### r22 |  |  PAGE TITLE        |  | 21:47  |   34   bar
 *   34 |####+-------+  +--------------------+  +--------'
 *      ^ the sweep the run              the run's one cap ^
 *        starts from
 *      |####|  <- the notch's top-left fillet is the inner sweep
 *   90 |####|
 *   96 +----+
 *      |####|  wifi + RSSI, and the way into the settings screen        68
 *  164 +----+
 *  170 +----+
 *      |####|  a cell tag -- or the alert glyph, when there is one --   70
 *      |####|  and the rounded cap the spine ends on
 *  240 +----+
 *
 * What is left for the tiles is (52, 42) to (315, 235): 264 x 194, which at a
 * 6 px gutter is six 84 x 94 tiles. The floor for a finger is 73 x 71.
 *
 * Portrait is the same drawing on a 240x320 screen, and a spine suits a tall
 * screen better than a wide one: the bar's title block narrows to 84 px, and
 * the spine's bottom cell simply runs on to the new bottom edge (DECO_H is
 * measured, not fixed). What is left for the tiles is then (52, 42) to
 * (235, 315): 184 x 274, six 89 x 87 tiles.
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
/* Half the bar's height, which is what LV_RADIUS_CIRCLE would clamp to. Spelled
 * out because the patch that squares the clock's other end is exactly this
 * wide, and the two must not drift apart. */
#define R_CAP     (BAR_H / 2)
#define GUTTER    4
#define SPINE_GAP 6

#define STATUS_Y  96
#define STATUS_H  68
#define DECO_Y    170
/* Measured rather than fixed: the cell runs to the bottom edge, which is what
 * makes the spine fit a 320 px tall portrait screen with no second layout. */
#define DECO_H    (LV_VER_RES - DECO_Y)

static struct
{
    lv_obj_t *root;
    lv_obj_t *title;
    lv_obj_t *clock;
    lv_obj_t *wifi;
    lv_obj_t *signal;
    lv_obj_t *tag;
    lv_obj_t *notice;
    lv_obj_t *notice_cell[2];
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
    lv_obj_set_clickable(label, false);

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
    lv_obj_set_clickable(lcars.root, false);

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

    /* The bar's two blocks. A run of LCARS blocks is capped where it *ends* and
     * butts square everywhere else, and this run ends once -- at the right edge
     * of the screen. So the title is square at both ends, the clock is square
     * where it faces the title and round where it faces the edge, and the only
     * other curve in the bar is the elbow's, at the far left, which is the
     * sweep the run starts from rather than a cap.
     *
     * Both blocks were stadiums before, which read as three separate lozenges
     * laid on the bar instead of one run across it. */
    /* The clock gets what five digits need and the title the rest: on a
     * portrait screen the bar is 80 px narrower, and spending it on the
     * clock's padding would dot the page name down to nothing. */
    int16_t title_x = ELBOW_W + GUTTER;
    int16_t clock_w = (LV_HOR_RES > 240) ? 64 : 56;
    int16_t clock_x = LV_HOR_RES - GUTTER - clock_w;
    int16_t title_w = (int16_t)(clock_x - GUTTER - title_x);
    int16_t title_pad = (LV_HOR_RES > 240) ? 14 : 8;

    lv_obj_t *title_block = ui_frame_block(lcars.root, title_x, 0, title_w, BAR_H, primary, 0);
    lv_obj_set_style_pad_hor(title_block, title_pad, 0);
    lcars.title = block_label(title_block, "", t->font_normal, ink);
    /* Both, or LONG_DOT has nothing to clip against: with a content-sized
     * height the label grows a second line and spills out of a block that
     * neither scrolls nor clips. One line of the face it is set in is the
     * height that makes it dot instead. */
    lv_obj_set_width(lcars.title, title_w - 2 * title_pad);
    lv_obj_set_height(lcars.title, lv_font_get_line_height(t->font_normal));
    lv_obj_center(lcars.title);

    /* One radius property per object, so one rounded end costs two blocks: the
     * plate, and a square patch of the same colour over the end that has to
     * stay flat. The same trick as the elbow above and the spine's cap below.
     *
     * Which is why the clock's label is not a child of the plate. The patch is
     * a sibling drawn after it, so a label inside the plate would be drawn
     * first and the patch would cover its left-hand digits. It goes on the root
     * afterwards and is centred by arithmetic, exactly as the notice cell's
     * does and for the same reason. */
    ui_frame_block(lcars.root, clock_x, 0, clock_w, BAR_H, second, R_CAP);
    ui_frame_block(lcars.root, clock_x, 0, R_CAP, BAR_H, second, 0);

    lcars.clock = block_label(lcars.root, "--:--", t->font_normal, ink);
    lv_obj_set_style_text_align(lcars.clock, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(lcars.clock, clock_w);
    lv_obj_set_pos(lcars.clock, clock_x,
                   (BAR_H - lv_font_get_line_height(t->font_normal)) / 2);

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
     * rounded and a square patch flattens the top where it meets the gap.
     *
     * It is also the alert cell. It was pure decoration and it is still the
     * spine's third colour when nothing is wrong -- but a cell that lights up
     * is how an LCARS panel says anything at all, so the banner indicator is
     * this cell changing colour rather than a coloured glyph laid on it. Two
     * blocks, so both have to be repainted; see lcars_set_notice(). */
    lcars.notice_cell[0] = ui_frame_block(lcars.root, 0, DECO_Y, SPINE_W, DECO_H, third, R_NOTCH);
    lcars.notice_cell[1] = ui_frame_block(lcars.root, 0, DECO_Y, SPINE_W, R_NOTCH, third, 0);

    /* Centred on the cell rather than laid out in it: the cell is two absolute
     * blocks and has no layout to join. One line of the face it is set in, so
     * the arithmetic is the label's own height and not a guess. */
    lcars.notice = ui_frame_notice(lcars.root);
    lv_obj_set_style_text_color(lcars.notice, lv_color_hex(ink), 0);
    lv_obj_set_style_text_font(lcars.notice, t->font_normal, 0);
    lv_obj_set_style_text_align(lcars.notice, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(lcars.notice, SPINE_W);
    lv_obj_set_pos(lcars.notice, 0,
                   DECO_Y + (DECO_H - lv_font_get_line_height(t->font_normal)) / 2);

    /* The numeric tags LCARS panels are covered in. Decoration, and it says
     * so: a fixed string rather than anything pretending to be a reading --
     * which is exactly why it is what gives way when the cell has something
     * real to show. */
    lcars.tag = block_label(lcars.root, "47-1701\nD", t->font_small, ink);
    lv_obj_set_style_text_align(lcars.tag, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(lcars.tag, LV_ALIGN_BOTTOM_LEFT, 6, -10);
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

/* Black on a colour block, like everything else in this family: the severity
 * is the cell, not the glyph. A coloured symbol on a coloured cell would be
 * two colours fighting, and LCARS has no neutral surface to lay one on. */
static void lcars_set_notice(enum ui_notice_e notice)
{
    const char *glyph = ui_frame_notice_glyph(notice);
    uint32_t    fill;

    if (lcars.notice == NULL)
        return;

    if (glyph == NULL)
    {
        lv_obj_set_hidden(lcars.notice, true);
        lv_obj_set_hidden(lcars.tag, false);
        fill = ui_style_theme()->btn.bg; /* back to the spine's third colour */
    }
    else
    {
        lv_label_set_text(lcars.notice, glyph);
        lv_obj_set_hidden(lcars.notice, false);
        lv_obj_set_hidden(lcars.tag, true);
        fill = ui_frame_notice_color(notice);
    }

    for (lv_obj_t *block : lcars.notice_cell)
        lv_obj_set_style_bg_color(block, lv_color_hex(fill), 0);
}

const struct ui_frame_ops_s ui_frame_lcars = {
    lcars_build,     lcars_destroy,  lcars_content_area, lcars_set_title,
    lcars_set_clock, lcars_set_link, lcars_set_notice,   lcars_decorate_tile};
