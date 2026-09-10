/**
 * @file frame_jarvis.cpp
 *
 * "Reticle": the instrument panel. Every reading is framed by the apparatus
 * that measures it, so nothing here is a bar or a border -- it is brackets,
 * hairlines, ticks and a ring.
 *
 *      0                                                        320
 *    0 | 21:47      KITCHEN                       ||||| (((      |  22  strip
 *   22 +----------------------------------------------------------+ hairline
 *      |  r--------,  r--------,  r--------,                      |
 *      |  | LIVING |  | CEILING|  | OUTSIDE|      98 x 91         |
 *      |  |  (23.5)|  |   ON   |  |  -4.2  |                      |
 *      |  '--------J  '--------J  '--------J                      |
 *      |  r--------,  r--------,  r--------,                      |
 *      |  |        |  |        |  |        |                      |
 *  222 +----------------------------------------------------------+ hairline
 *      |  . . . . # . . . .            SYS NOMINAL                |  18  rail
 *  240 +----------------------------------------------------------+
 *
 * The brackets and the ring are drawn from an LV_EVENT_DRAW_MAIN_END handler
 * rather than built out of objects. That is eight rects and an arc per tile as
 * draw calls, against ten more objects per tile if they were widgets -- and
 * for the ring it is the difference between +1.6 KB of flash for
 * lv_draw_arc.c and +7.9 KB for lv_arc.c plus six widget instances. The
 * software arc renderer is already linked in either way: lv_draw_sw.c
 * references it unconditionally, so LV_USE_ARC can stay off.
 *
 * The scan dot on the bottom rail advances one cell a second. Sixteen pixels
 * of invalidation per second is the cheapest "this thing is alive" signal
 * there is, and it is the only thing on the screen that moves at rest.
 */
#include "ui_frame.hpp"

#include "ui/ui_style.hpp"

#include "draw/lv_draw_arc.h"

#include <stdio.h>

#define STRIP_H   22
#define RAIL_H    18
#define EDGE      6
#define HAIRLINE  1

#define BRACKET_ARM 14
#define BRACKET_W   2

#define RING_R    28
#define RING_W    4
#define RING_FROM 135 /* 0 is 3 o'clock, so this sweeps the lower three quarters */
#define RING_ARC  270

#define TICKS       10
#define TICK_W      2
#define TICK_H      5
#define SCAN_CELLS  40

static struct
{
    lv_obj_t *root;
    lv_obj_t *clock;
    lv_obj_t *title;
    lv_obj_t *link;
    lv_obj_t *scan;
    lv_timer_t *scan_timer;
    uint8_t   scan_cell;
} jarvis;

/* What a tile needs to draw itself. Squeezed into the object's user data
 * rather than allocated: one pointer per tile, and it dies with the tile. */
struct tile_gauge_s
{
    uint8_t  has_ring;
    uint8_t  fraction; /* 0..100, the value's place in its range */
};

static struct tile_gauge_s gauges[6];

static lv_obj_t *strip_label(lv_obj_t *parent, const char *text, lv_opa_t opa)
{
    const struct ui_theme_s *t = ui_style_theme();
    lv_obj_t                *label = lv_label_create(parent);

    lv_label_set_text(label, text);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(label, t->font_small, 0);
    lv_obj_set_style_text_opa(label, opa, 0);
    lv_obj_remove_flag(label, LV_OBJ_FLAG_CLICKABLE);

    return label;
}

/* One cell a second, wrapping. */
static void scan_tick(lv_timer_t *timer)
{
    LV_UNUSED(timer);

    if (jarvis.scan == NULL)
        return;

    jarvis.scan_cell = (uint8_t)((jarvis.scan_cell + 1) % SCAN_CELLS);
    lv_obj_set_x(jarvis.scan, EDGE + jarvis.scan_cell * 4);
}

static void jarvis_build(lv_obj_t *parent)
{
    const struct ui_theme_s *t = ui_style_theme();

    uint32_t line = t->link.color; /* the hairline and bracket colour */
    uint32_t ink  = t->screen.text;

    jarvis.root = ui_frame_container(parent);
    lv_obj_set_pos(jarvis.root, 0, 0);
    lv_obj_set_size(jarvis.root, lv_pct(100), lv_pct(100));
    lv_obj_remove_flag(jarvis.root, LV_OBJ_FLAG_CLICKABLE);

    /* The top strip: clock, page name, link. */
    lv_obj_t *strip = ui_frame_container(jarvis.root);

    lv_obj_set_pos(strip, 0, 0);
    lv_obj_set_size(strip, lv_pct(100), STRIP_H);
    lv_obj_set_style_pad_hor(strip, EDGE + 2, 0);
    lv_obj_set_flex_flow(strip, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(strip, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    ui_frame_settings_target(strip);

    jarvis.clock = strip_label(strip, "--:--", LV_OPA_COVER);

    jarvis.title = strip_label(strip, "", LV_OPA_80);
    lv_obj_set_flex_grow(jarvis.title, 1);
    lv_obj_set_style_text_align(jarvis.title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_letter_space(jarvis.title, 1, 0);

    jarvis.link = strip_label(strip, LV_SYMBOL_POWER, LV_OPA_COVER);

    /* Two hairlines, which is all the structure this family has. */
    ui_frame_block(jarvis.root, 0, STRIP_H, LV_HOR_RES, HAIRLINE, line, 0);
    ui_frame_block(jarvis.root, 0, LV_VER_RES - RAIL_H, LV_HOR_RES, HAIRLINE, line, 0);

    /* The bottom rail: a scan dot, and a word that is honest about being
     * decoration rather than a reading. */
    jarvis.scan = ui_frame_block(jarvis.root, EDGE, LV_VER_RES - RAIL_H + 7, 4, 4, line, 0);
    jarvis.scan_cell = 0;

    lv_obj_t *rail = strip_label(jarvis.root, "SYS NOMINAL", LV_OPA_50);
    /* Centred in the rail rather than pinned to the bottom edge, or the
     * descenders sit on the glass and the hairline cuts through the caps. */
    lv_obj_set_pos(rail, 0, LV_VER_RES - RAIL_H);
    lv_obj_set_size(rail, LV_HOR_RES - EDGE - 2, RAIL_H);
    lv_obj_set_style_text_align(rail, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_letter_space(rail, 1, 0);
    lv_obj_set_style_text_color(rail, lv_color_hex(ink), 0);

    jarvis.scan_timer = lv_timer_create(scan_tick, 1000, NULL);
}

static void jarvis_destroy(void)
{
    if (jarvis.scan_timer != NULL)
        lv_timer_delete(jarvis.scan_timer);

    if (jarvis.root != NULL)
        lv_obj_delete(jarvis.root);

    jarvis = {};
}

static lv_area_t jarvis_content_area(void)
{
    lv_area_t a;

    a.x1 = 0;
    a.y1 = STRIP_H + HAIRLINE;
    a.x2 = LV_HOR_RES - 1;
    a.y2 = LV_VER_RES - RAIL_H - 1;

    return a;
}

static void jarvis_set_title(const char *title)
{
    if (jarvis.title == NULL)
        return;

    char   upper[40];
    size_t i = 0;

    for (; title[i] != '\0' && i < sizeof(upper) - 1; i++)
        upper[i] = (char)((title[i] >= 'a' && title[i] <= 'z') ? title[i] - 32 : title[i]);

    upper[i] = '\0';

    lv_label_set_text(jarvis.title, upper);
}

static void jarvis_set_clock(const char *text)
{
    if (jarvis.clock != NULL)
        lv_label_set_text(jarvis.clock, text);
}

/* Five segments rather than a percentage: a bar is read at a glance and a
 * two-digit number is not, and this is meant to look like instrumentation. */
static void jarvis_set_link(bool online, int rssi)
{
    if (jarvis.link == NULL)
        return;

    if (online == false)
    {
        lv_label_set_text(jarvis.link, LV_SYMBOL_REFRESH);
        return;
    }

    if (rssi < 0)
    {
        lv_label_set_text(jarvis.link, LV_SYMBOL_SHUFFLE);
        return;
    }

    char bars[16];
    int  lit = (rssi + 19) / 20; /* 0..5 */
    int  n = 0;

    for (int i = 0; i < 5 && n < (int)sizeof(bars) - 1; i++)
        bars[n++] = (i < lit) ? '|' : '.';

    bars[n] = '\0';

    lv_label_set_text(jarvis.link, bars);
}

/* ------------------------------------------------------------- the tile draw */

static void bracket(lv_layer_t *layer, lv_draw_rect_dsc_t *dsc, int32_t x, int32_t y,
                    int32_t w, int32_t h)
{
    lv_area_t a;

    a.x1 = x;
    a.y1 = y;
    a.x2 = x + w - 1;
    a.y2 = y + h - 1;

    lv_draw_rect(layer, dsc, &a);
}

/* Corner brackets instead of a border, and a ring where there is a number with
 * a range behind it. Both from a draw event: as objects this would be ten more
 * per tile, and the widget that draws rings costs five times the flash of the
 * function that draws them. */
static void tile_draw_event(lv_event_t *e)
{
    lv_obj_t   *tile  = (lv_obj_t *)lv_event_get_target(e);
    lv_layer_t *layer = lv_event_get_layer(e);
    uintptr_t   slot  = (uintptr_t)lv_obj_get_user_data(tile);

    if (layer == NULL || slot >= 6)
        return;

    const struct ui_theme_s *t = ui_style_theme();
    lv_area_t                c;

    lv_obj_get_coords(tile, &c);

    lv_draw_rect_dsc_t dsc;

    lv_draw_rect_dsc_init(&dsc);
    dsc.bg_color = lv_color_hex(t->link.color);
    dsc.bg_opa   = LV_OPA_80;

    int32_t x1 = c.x1, y1 = c.y1, x2 = c.x2, y2 = c.y2;

    /* Four corners, two rects each: the arms of an L. */
    bracket(layer, &dsc, x1, y1, BRACKET_ARM, BRACKET_W);
    bracket(layer, &dsc, x1, y1, BRACKET_W, BRACKET_ARM);
    bracket(layer, &dsc, x2 - BRACKET_ARM + 1, y1, BRACKET_ARM, BRACKET_W);
    bracket(layer, &dsc, x2 - BRACKET_W + 1, y1, BRACKET_W, BRACKET_ARM);
    bracket(layer, &dsc, x1, y2 - BRACKET_W + 1, BRACKET_ARM, BRACKET_W);
    bracket(layer, &dsc, x1, y2 - BRACKET_ARM + 1, BRACKET_W, BRACKET_ARM);
    bracket(layer, &dsc, x2 - BRACKET_ARM + 1, y2 - BRACKET_W + 1, BRACKET_ARM, BRACKET_W);
    bracket(layer, &dsc, x2 - BRACKET_W + 1, y2 - BRACKET_ARM + 1, BRACKET_W, BRACKET_ARM);

    if (gauges[slot].has_ring == 0)
        return;

    /* The ring encircles the reading rather than sitting beside it, so the
     * number is inside its own dial. Beside it, at this tile width, the two
     * simply collided -- and a gauge that frames what it measures is the whole
     * idea of the family. The 270 degrees open at the bottom, which is where
     * the sweep starts and ends. */
    lv_draw_arc_dsc_t arc;

    lv_draw_arc_dsc_init(&arc);
    arc.center.x = (x1 + x2) / 2;
    arc.center.y = y2 - RING_R - 2;
    arc.radius   = RING_R;
    arc.width    = RING_W;
    arc.opa      = LV_OPA_COVER;
    arc.rounded  = 0;

    /* The track has to be visible against the tile or the indicator reads as a
     * stray crescent rather than as part of a dial. Mixed with the bracket
     * colour rather than taken from the slider, which is tuned for a surface
     * two shades lighter than this one. */
    arc.color = lv_color_mix(lv_color_hex(t->link.color), lv_color_hex(t->tile.bg), 60);
    arc.start_angle = RING_FROM;
    arc.end_angle   = RING_FROM + RING_ARC;
    lv_draw_arc(layer, &arc);

    if (gauges[slot].fraction == 0)
        return;

    arc.color       = lv_color_hex(t->accent);
    arc.start_angle = RING_FROM;
    arc.end_angle   = RING_FROM + (RING_ARC * gauges[slot].fraction) / 100;
    lv_draw_arc(layer, &arc);
}

/* Only where a number has a range behind it. A switch or a page link has
 * nothing to put on a dial, and six rings on one screen would be noise as well
 * as cost. */
static bool type_has_ring(enum ItemType type)
{
    /* Not type_number. A dial needs a range, and the parser gives a plain Text
     * item the 0..100 default when openHAB sends no minimum or maximum -- so a
     * temperature would sit at 3% of a scale that means nothing. */
    return type == ItemType::type_slider || type == ItemType::type_setpoint;
}

static void jarvis_decorate_tile(lv_obj_t *tile, enum ItemType type, uint8_t slot)
{
    if (tile == NULL || slot >= 6)
        return;

    gauges[slot].has_ring = type_has_ring(type) ? 1 : 0;
    gauges[slot].fraction = 0;

    lv_obj_set_user_data(tile, (void *)(uintptr_t)slot);
    lv_obj_add_event_cb(tile, tile_draw_event, LV_EVENT_DRAW_MAIN_END, NULL);

    /* The brackets are the tile's outline, so the style's own border would be
     * a second one drawn underneath them. Removed locally rather than from the
     * theme table, because the same border is what the item screens and the
     * settings rows use. */
    lv_obj_set_style_border_width(tile, 0, 0);
}

const struct ui_frame_ops_s ui_frame_jarvis = {
    jarvis_build,     jarvis_destroy,  jarvis_content_area,  jarvis_set_title,
    jarvis_set_clock, jarvis_set_link, jarvis_decorate_tile};

/* Told by the page whenever a ring's item moves. Not part of the frame
 * interface: only this family has rings, so only this family's page hook needs
 * to exist, and the others pay nothing for it. */
void ui_frame_jarvis_set_gauge(uint8_t slot, uint8_t percent)
{
    if (slot >= 6 || gauges[slot].has_ring == 0)
        return;

    gauges[slot].fraction = (percent > 100) ? 100 : percent;
}
