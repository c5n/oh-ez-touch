/**
 * @file frame_default.cpp
 *
 * "Slate": the theme you leave on. One reading per card, big enough to see
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
 * status bar always was. It is edge-anchored and 320 px wide, so being 30 px
 * tall rather than the 44 a tap target usually wants costs nothing: there is
 * no way to overshoot the top of the glass.
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
} slate;

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
    lv_obj_remove_flag(label, LV_OBJ_FLAG_CLICKABLE);

    return label;
}

static void slate_build(lv_obj_t *parent)
{
    slate.root = ui_frame_container(parent);
    lv_obj_set_pos(slate.root, 0, 0);
    lv_obj_set_size(slate.root, lv_pct(100), BAND_H);
    lv_obj_set_style_pad_hor(slate.root, EDGE, 0);
    lv_obj_set_flex_flow(slate.root, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(slate.root, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    ui_frame_settings_target(slate.root);

    slate.clock = band_label(slate.root, "--:--", LV_OPA_70);

    slate.title = band_label(slate.root, "", LV_OPA_60);
    lv_obj_set_flex_grow(slate.title, 1);
    lv_obj_set_style_text_align(slate.title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_letter_space(slate.title, 1, 0);

    slate.link = band_label(slate.root, LV_SYMBOL_POWER, LV_OPA_60);
}

static void slate_destroy(void)
{
    if (slate.root != NULL)
        lv_obj_delete(slate.root);

    slate = {};
}

static lv_area_t slate_content_area(void)
{
    lv_area_t a;

    a.x1 = 0;
    a.y1 = BAND_H;
    a.x2 = LV_HOR_RES - 1;
    a.y2 = LV_VER_RES - 1;

    return a;
}

static void slate_set_title(const char *title)
{
    if (slate.title == NULL)
        return;

    /* Upper case, because at 16 px and 60% opacity a page name reads better as
     * a label than as a sentence, and it stops competing with the captions. */
    char upper[40];
    size_t i = 0;

    for (; title[i] != '\0' && i < sizeof(upper) - 1; i++)
        upper[i] = (char)((title[i] >= 'a' && title[i] <= 'z') ? title[i] - 32 : title[i]);

    upper[i] = '\0';

    lv_label_set_text(slate.title, upper);
}

static void slate_set_clock(const char *text)
{
    if (slate.clock == NULL)
        return;

    /* Slate does not tick. The blinking colon is a habit from a panel that had
     * nothing else to say it was alive; this one has a whole grid of readings,
     * and a calm theme should not have something flashing in the corner of it.
     * The seventh character is the separator the caller alternates. */
    char steady[8];
    size_t i = 0;

    for (; text[i] != '\0' && i < sizeof(steady) - 1; i++)
        steady[i] = (text[i] == ' ') ? ':' : text[i];

    steady[i] = '\0';

    lv_label_set_text(slate.clock, steady);
}

static void slate_set_link(bool online, int rssi)
{
    if (slate.link == NULL)
        return;

    if (online == false)
        lv_label_set_text(slate.link, LV_SYMBOL_REFRESH);
    else if (rssi < 0)
        lv_label_set_text(slate.link, LV_SYMBOL_SHUFFLE);
    else
        lv_label_set_text_fmt(slate.link, "%d%% " LV_SYMBOL_WIFI, rssi);
}

const struct ui_frame_ops_s ui_frame_default = {
    slate_build,     slate_destroy,  slate_content_area, slate_set_title,
    slate_set_clock, slate_set_link, NULL};
