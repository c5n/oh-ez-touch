/**
 * @file frame_material.cpp
 *
 * "Material": the theme you leave on. One reading per card, big enough to see
 * from the doorway, and nothing else competing with it.
 *
 * Its structural idea is that there is no chrome *object* at all. The top
 * 30 px are bare background carrying three unadorned labels at low opacity --
 * the clock, the page name, the link -- so the status row stops being a bar
 * with a colour and an edge, and the grid gets the whole of the rest of the
 * screen instead of everything below a rule.
 *
 * That is the cheapest frame of the three by a wide margin: three labels, no
 * fills, nothing to repaint when a tile changes.
 *
 *      0                                                        320
 *    0 +----------------------------------------------------------+
 *      | 21:47            KITCHEN                   87% (((        |  30
 *   30 +----------------------------------------------------------+
 *      |   +----------+   +----------+   +----------+              |
 *      |   |  96 x 93 |   |          |   |          |              |
 *      |   +----------+   +----------+   +----------+              |
 *      |   +----------+   +----------+   +----------+              |
 *      |   |          |   |          |   |          |              |
 *  240 +----------------------------------------------------------+
 *
 * The whole band is the way into the settings screen, which is what the
 * status bar always was. It is edge-anchored and the full width of the glass,
 * so being 30 px tall rather than the 44 a tap target usually wants costs
 * nothing: there is no way to overshoot the top of the screen.
 *
 * Portrait needs nothing from this file beyond what LV_HOR_RES/LV_VER_RES
 * already say: the band stays 30 px of bare ground across the top, and the
 * content area below is 240 x 290, which the portrait grid of the theme table
 * packs as two columns of three 108 x 86 cards.
 */
#include "ui_frame.hpp"

#include "ui/ui_style.hpp"

#define BAND_H 30
#define EDGE   10

static struct
{
    lv_obj_t *root;
    lv_obj_t *clock;
    lv_obj_t *title;
    lv_obj_t *link;
    lv_obj_t *notice;
} material;

/* Quiet: this is context, not content. The tiles are what the eye should
 * land on, so everything up here is the caption face at partial opacity. */
static lv_obj_t *band_label(lv_obj_t *parent, const char *text, lv_opa_t opa)
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

static void material_build(lv_obj_t *parent)
{
    material.root = ui_frame_container(parent);
    lv_obj_set_pos(material.root, 0, 0);
    lv_obj_set_size(material.root, lv_pct(100), BAND_H);
    lv_obj_set_style_pad_hor(material.root, EDGE, 0);
    /* Only ever seen between the link readout and the notice glyph: the clock
     * and the title are spaced apart by the row itself. Without it those two
     * touch, and "wired, and something is wrong" reads as one glyph. */
    lv_obj_set_style_pad_column(material.root, 6, 0);
    lv_obj_set_flex_flow(material.root, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(material.root, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    ui_frame_settings_target(material.root);

    material.clock = band_label(material.root, "--:--", LV_OPA_70);

    material.title = band_label(material.root, "", LV_OPA_60);
    lv_obj_set_flex_grow(material.title, 1);
    lv_obj_set_style_text_align(material.title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_letter_space(material.title, 1, 0);

    material.link = band_label(material.root, LV_SYMBOL_POWER, LV_OPA_60);

    /* At the end of the band, and the only thing up here at full opacity. The
     * band is deliberately quiet -- context, not content -- so the one glyph
     * that is content has to break that rule to be seen at all. */
    material.notice = ui_frame_notice(material.root);
}

static void material_destroy(void)
{
    if (material.root != NULL)
        lv_obj_delete(material.root);

    material = {};
}

static lv_area_t material_content_area(void)
{
    lv_area_t a;

    a.x1 = 0;
    a.y1 = BAND_H;
    a.x2 = LV_HOR_RES - 1;
    a.y2 = LV_VER_RES - 1;

    return a;
}

static void material_set_title(const char *title)
{
    if (material.title == NULL)
        return;

    /* Upper case, because at 16 px and 60% opacity a page name reads better as
     * a label than as a sentence, and it stops competing with the captions. */
    char upper[40];
    size_t i = 0;

    for (; title[i] != '\0' && i < sizeof(upper) - 1; i++)
        upper[i] = (char)((title[i] >= 'a' && title[i] <= 'z') ? title[i] - 32 : title[i]);

    upper[i] = '\0';

    lv_label_set_text(material.title, upper);
}

static void material_set_clock(const char *text)
{
    if (material.clock == NULL)
        return;

    /* Material does not tick. The blinking colon is a habit from a panel that
     * had nothing else to say it was alive; this one has a whole grid of
     * readings, and a calm theme should not have something flashing in the
     * corner of it. */
    char steady[8];

    ui_frame_clock_steady(steady, sizeof(steady), text);
    lv_label_set_text(material.clock, steady);
}

static void material_set_link(bool online, int rssi)
{
    if (material.link == NULL)
        return;

    if (online == false)
        lv_label_set_text(material.link, LV_SYMBOL_REFRESH);
    else if (rssi < 0)
        lv_label_set_text(material.link, LV_SYMBOL_SHUFFLE);
    else
        lv_label_set_text_fmt(material.link, "%d%% " LV_SYMBOL_WIFI, rssi);
}

static void material_set_notice(enum ui_notice_e notice)
{
    ui_frame_notice_set(material.notice, notice);
}

const struct ui_frame_ops_s ui_frame_material = {
    material_build,     material_destroy,  material_content_area, material_set_title,
    material_set_clock, material_set_link, material_set_notice,   NULL};
