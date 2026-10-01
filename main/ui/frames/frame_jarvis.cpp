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
 * The brackets also say what a tile is, which the style's border used to say
 * before the brackets replaced it: a tile that leads somewhere has brackets in
 * arms that breathe, a tile that operates something has still ones, and a
 * bare reading has short, faint ones -- all in the link colour.
 *
 * The scan dot on the bottom rail advances one cell a second. Sixteen pixels
 * of invalidation per second is the cheapest "this thing is alive" signal
 * there is. The breathing arms are the only other thing that moves at rest,
 * and they repaint only the four corner squares, only when the arm length
 * crosses a whole pixel: about eight times a second at most 1.6 k px a tile,
 * far inside ui_motion.hpp's sustained budget.
 *
 * Portrait is the same drawing between the same strip and rail: both are
 * LV_HOR_RES wide, the 160 px scan path still fits a 240 px screen, and the
 * theme table's portrait grid packs the middle as two columns of three
 * 111 x 85 tiles -- wider than they are tall now, which the brackets and the
 * R28 ring absorb without a second layout.
 */
#include "ui_frame.hpp"

#include "ui/ui_motion.hpp"
#include "ui/ui_style.hpp"

#include "lvgl.h"

#include <stdio.h>

#define STRIP_H   22
#define RAIL_H    18
#define EDGE      6
#define HAIRLINE  1

#define BRACKET_ARM 14
#define BRACKET_W   2

/* A navigation tile's arms breathe between these, a reading's are short. */
#define BRACKET_ARM_MIN     10
#define BRACKET_ARM_MAX     20
#define BRACKET_ARM_READOUT 8
#define BREATH_MS           1200 /* each way */
#define BREATH_SUB          16   /* animated in 1/16 px, rounded to a pixel */

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
    lv_obj_t *bt;
    lv_obj_t *notice;
    lv_obj_t *scan;
    lv_timer_t *scan_timer;
    uint8_t   scan_cell;
} jarvis;

/* What a tile needs to draw itself. Squeezed into the object's user data
 * rather than allocated: one pointer per tile, and it dies with the tile. */
enum tile_kind_e
{
    TILE_OPERABLE = 0,
    TILE_NAV,     /* leads to another page */
    TILE_READOUT, /* nothing to press */
};

struct tile_gauge_s
{
    uint8_t  has_ring;
    uint8_t  fraction; /* 0..100, the value's place in its range */
    uint8_t  kind;     /* enum tile_kind_e */
    uint8_t  arm;      /* bracket arm length in px; animated on a TILE_NAV */
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
    lv_obj_set_clickable(label, false);

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
    lv_obj_set_clickable(jarvis.root, false);

    /* The top strip: clock, page name, link. */
    lv_obj_t *strip = ui_frame_container(jarvis.root);

    lv_obj_set_pos(strip, 0, 0);
    lv_obj_set_size(strip, lv_pct(100), STRIP_H);
    lv_obj_set_style_pad_hor(strip, EDGE + 2, 0);
    /* Only ever seen between the signal bars and the notice glyph; everything
     * else in the strip is spaced apart by the row itself. */
    lv_obj_set_style_pad_column(strip, 6, 0);
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

    /* The other radios' badge, past the signal bars. Same ink as the rest of
     * the strip: this family already says the link's strength with its bars,
     * which is its instrumentation look -- see jarvis_set_link() -- and the
     * badge is a label of state, not a reading, so it joins the strip rather
     * than the apparatus below it. */
    jarvis.bt = strip_label(strip, LV_SYMBOL_BLUETOOTH, LV_OPA_COVER);
    lv_obj_add_flag(jarvis.bt, LV_OBJ_FLAG_HIDDEN);

    /* The right end of the strip, past the signal bars. Reticle frames every
     * reading in the apparatus that measures it; an alert is the one thing on
     * the panel that nothing measures, so it gets no bracket and no rule -- a
     * bare glyph in its own colour. */
    jarvis.notice = ui_frame_notice(strip);

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
    if (jarvis.clock == NULL)
        return;

    /* Steady, and not as a matter of taste: Rajdhani's colon is 19/16 px
     * narrower than its space, and the strip is a left-anchored flex row, so
     * a blinking separator makes the minutes hop a pixel sideways once a
     * second. Reticle already has a second hand -- the scan dot on the bottom
     * rail -- and it is the one this family designed for the job. (LCARS keeps
     * its blink: Antonio's colon and space differ by 1/16 px, which never
     * crosses a pixel boundary.) */
    char steady[8];

    ui_frame_clock_steady(steady, sizeof(steady), text);
    lv_label_set_text(jarvis.clock, steady);
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

static void jarvis_set_radios(bool ble)
{
    if (jarvis.bt != NULL)
        (ble == true) ? lv_obj_clear_flag(jarvis.bt, LV_OBJ_FLAG_HIDDEN)
                      : lv_obj_add_flag(jarvis.bt, LV_OBJ_FLAG_HIDDEN);
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

    /* The kind of tile is the bracket's motion, opacity and reach -- the type
     * marker the removed border used to carry. One colour for all of them:
     * the active colour on the brackets read as a second palette fighting
     * the first. */
    int32_t arm = gauges[slot].arm;

    dsc.bg_color = lv_color_hex(t->link.color);
    dsc.bg_opa   = (gauges[slot].kind == TILE_READOUT) ? LV_OPA_40 : LV_OPA_80;

    int32_t x1 = c.x1, y1 = c.y1, x2 = c.x2, y2 = c.y2;

    /* Four corners, two rects each: the arms of an L. */
    bracket(layer, &dsc, x1, y1, arm, BRACKET_W);
    bracket(layer, &dsc, x1, y1, BRACKET_W, arm);
    bracket(layer, &dsc, x2 - arm + 1, y1, arm, BRACKET_W);
    bracket(layer, &dsc, x2 - BRACKET_W + 1, y1, BRACKET_W, arm);
    bracket(layer, &dsc, x1, y2 - BRACKET_W + 1, arm, BRACKET_W);
    bracket(layer, &dsc, x1, y2 - arm + 1, BRACKET_W, arm);
    bracket(layer, &dsc, x2 - arm + 1, y2 - BRACKET_W + 1, arm, BRACKET_W);
    bracket(layer, &dsc, x2 - BRACKET_W + 1, y2 - arm + 1, BRACKET_W, arm);

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

/* The same grouping widget_create() uses to pick a tile's marker style. */
static enum tile_kind_e type_kind(enum ItemType type)
{
    switch (type)
    {
    case ItemType::type_link:
    case ItemType::type_parent_link:
    case ItemType::type_group:
        return TILE_NAV;

    case ItemType::type_number:
    case ItemType::type_string:
        return TILE_READOUT;

    default:
        return TILE_OPERABLE;
    }
}

/* One breath step. Repaints the four corner squares the arms can reach, and
 * only when the length has crossed a whole pixel -- the curve spends most of a
 * frame period between two integers near either end.
 *
 * Sub-pixel values, rounded here: lv_anim truncates each step, so animated in
 * whole pixels the arm sits at MIN for a long stretch but reaches MAX on the
 * last frame of the way out only, and drops back on the next -- a one-frame
 * blip at the top of every breath. Rounding holds both ends equally long. */
static void breathe_exec(void *var, int32_t value)
{
    lv_obj_t *tile = (lv_obj_t *)var;
    uintptr_t slot = (uintptr_t)lv_obj_get_user_data(tile);
    uint8_t   arm  = (uint8_t)((value + BREATH_SUB / 2) / BREATH_SUB);

    if (slot >= 6 || gauges[slot].arm == arm)
        return;

    gauges[slot].arm = arm;

    lv_area_t c;

    lv_obj_get_coords(tile, &c);

    const int32_t r = BRACKET_ARM_MAX - 1;
    lv_area_t     corner[4] = {
        {c.x1, c.y1, c.x1 + r, c.y1 + r},
        {c.x2 - r, c.y1, c.x2, c.y1 + r},
        {c.x1, c.y2 - r, c.x1 + r, c.y2},
        {c.x2 - r, c.y2 - r, c.x2, c.y2},
    };

    for (int i = 0; i < 4; i++)
        lv_obj_invalidate_area(tile, &corner[i]);
}

/* Keyed on the tile, per ui_motion.hpp: deleting the tile deletes the breath,
 * so a page change or a theme change needs no cleanup here. */
static void breathe_start(lv_obj_t *tile)
{
    lv_anim_t a;

    lv_anim_init(&a);
    lv_anim_set_var(&a, tile);
    lv_anim_set_exec_cb(&a, breathe_exec);
    lv_anim_set_values(&a, BRACKET_ARM_MIN * BREATH_SUB, BRACKET_ARM_MAX * BREATH_SUB);
    lv_anim_set_duration(&a, BREATH_MS);
    lv_anim_set_reverse_duration(&a, BREATH_MS);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&a, ui_motion_path(UI_EASE_IN_OUT_CUBIC));
    lv_anim_start(&a);
}

static void jarvis_decorate_tile(lv_obj_t *tile, enum ItemType type, uint8_t slot)
{
    if (tile == NULL || slot >= 6)
        return;

    gauges[slot].has_ring = type_has_ring(type) ? 1 : 0;
    gauges[slot].fraction = 0;
    gauges[slot].kind     = (uint8_t)type_kind(type);
    gauges[slot].arm      = (gauges[slot].kind == TILE_READOUT) ? BRACKET_ARM_READOUT
                                                                : BRACKET_ARM;

    lv_obj_set_user_data(tile, (void *)(uintptr_t)slot);
    lv_obj_add_event_cb(tile, tile_draw_event, LV_EVENT_DRAW_MAIN_END, NULL);

    /* With motion off a navigation tile keeps still brackets, the same as an
     * operable one's -- the breath is the only thing that tells them apart. */
    if (gauges[slot].kind == TILE_NAV && ui_motion_enabled() == true)
        breathe_start(tile);

    /* The brackets are the tile's outline, so the style's own border would be
     * a second one drawn underneath them. Removed locally rather than from the
     * theme table, because the same border is what the item screens and the
     * settings rows use. That also removes the link and active markers
     * widget_create() put on the tile, which is why the brackets carry the
     * tile's kind instead -- see tile_draw_event(). */
    lv_obj_set_style_border_width(tile, 0, 0);
}

static void jarvis_set_notice(enum ui_notice_e notice)
{
    ui_frame_notice_set(jarvis.notice, notice);
}

const struct ui_frame_ops_s ui_frame_jarvis = {
    jarvis_build,     jarvis_destroy,  jarvis_content_area, jarvis_set_title,
    jarvis_set_clock, jarvis_set_link, jarvis_set_radios,   jarvis_set_notice,
    jarvis_decorate_tile};

/* Told by the page whenever a ring's item moves. Not part of the frame
 * interface: only this family has rings, so only this family's page hook needs
 * to exist, and the others pay nothing for it. */
void ui_frame_jarvis_set_gauge(uint8_t slot, uint8_t percent)
{
    if (slot >= 6 || gauges[slot].has_ring == 0)
        return;

    gauges[slot].fraction = (percent > 100) ? 100 : percent;
}
