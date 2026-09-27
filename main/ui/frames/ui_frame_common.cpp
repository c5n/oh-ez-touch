/**
 * @file ui_frame_common.cpp
 *
 * The pieces every family's frame is built from.
 */
#include "ui_frame.hpp"

#include "ui/ui_beep.hpp"
#include "ui/ui_messagebox.hpp"
#include "ui/ui_motion.hpp"
#include "ui/ui_settings.hpp"
#include "ui/ui_style.hpp"
#include "ui/ui_widgets.hpp"

lv_obj_t *ui_frame_container(lv_obj_t *parent)
{
    /* The shared one. Kept under this name because it is what the frame
     * interface offers its implementations. */
    return ui_plain_container(parent);
}

static void settings_event(lv_event_t *e)
{
    LV_UNUSED(e);

    if (ui_settings_is_open() == false)
    {
        BEEPER_EVENT_SCREEN();
        /* The index, not the Info tab. Info was the sensible landing place
         * while the sections were tabs you could see from anywhere; now that
         * they are behind an index, dropping the user into one of them and
         * making them go back is a step for nothing. */
        ui_settings_open(SETTINGS_TAB_COUNT);
    }
}

void ui_frame_settings_target(lv_obj_t *obj)
{
    if (obj == NULL)
        return;

    lv_obj_set_clickable(obj, true);
    lv_obj_add_event_cb(obj, settings_event, LV_EVENT_CLICKED, NULL);
}

/* ------------------------------------------------------ the banner indicator
 *
 * The one thing on the chrome that is not a reading: it says a message box is
 * waiting, and touching it brings back one that has been folded away.
 *
 * It is a label rather than a button because every family already has a row or
 * a cell of labels to put it in, and a glyph that appears and disappears in
 * one of those costs nothing to lay out -- LVGL skips a hidden child, so the
 * row closes up again by itself. The extended click area is what makes a
 * glyph-sized indicator a finger-sized target. */

static void notice_event(lv_event_t *e)
{
    LV_UNUSED(e);

    Messagebox::unfold();
}

lv_obj_t *ui_frame_notice(lv_obj_t *parent)
{
    lv_obj_t *label = lv_label_create(parent);

    lv_label_set_text(label, "");
    lv_obj_set_hidden(label, true);
    lv_obj_set_clickable(label, true);
    lv_obj_set_ext_click_area(label, 10);
    lv_obj_add_event_cb(label, notice_event, LV_EVENT_CLICKED, NULL);

    return label;
}

const char *ui_frame_notice_glyph(enum ui_notice_e notice)
{
    switch (notice)
    {
    /* A bell, a warning triangle and a cross. Three glyphs rather than one in
     * three colours, because the families disagree about how much colour the
     * chrome may carry -- Material's band is one dim ink and LCARS's cells are
     * all colour -- and a shape reads the same in both. */
    case UI_NOTICE_INFO:    return LV_SYMBOL_BELL;
    case UI_NOTICE_WARNING: return LV_SYMBOL_WARNING;
    case UI_NOTICE_ERROR:   return LV_SYMBOL_CLOSE;
    default:                return NULL;
    }
}

uint32_t ui_frame_notice_color(enum ui_notice_e notice)
{
    const struct ui_theme_s *t = ui_style_theme();

    switch (notice)
    {
    case UI_NOTICE_WARNING: return t->info_warning_bg;
    case UI_NOTICE_ERROR:   return t->info_error_bg;
    default:                break;
    }

    /* The edge of an info box, which is the colour that variant has already
     * chosen to mean "something is being said". A variant that leaves it at
     * UI_COLOR_KEEP is saying it wants the box's own text colour, so the
     * indicator takes the screen's ink rather than painting itself black. */
    return (t->info.border == UI_COLOR_KEEP) ? t->screen.text : t->info.border;
}

void ui_frame_notice_set(lv_obj_t *label, enum ui_notice_e notice)
{
    const char *glyph = ui_frame_notice_glyph(notice);

    if (label == NULL)
        return;

    if (glyph == NULL)
    {
        lv_obj_set_hidden(label, true);
        return;
    }

    lv_label_set_text(label, glyph);
    lv_obj_set_style_text_color(label, lv_color_hex(ui_frame_notice_color(notice)), 0);
    lv_obj_set_hidden(label, false);
}

/* ------------------------------------------------------------------------- */

/* ------------------------------------------------- the radios' drawn icons */

/* Both icons are a dot and three arcs, drawn from a LV_EVENT_DRAW_MAIN_END
 * handler rather than carried as glyphs: the arcs are the payload -- the
 * wifi fan's carry the strength, and the MQTT mark's are the shape of
 * mqtt.org's own logo -- and no face this panel carries grades a fan. The
 * widget route (lv_arc) was rejected for the same reason frame_jarvis.cpp
 * rejects it for its rings: six widget instances against one function. */

/* One arc of a fan, lit or as its track. The track is the ink mixed toward
 * the screen's ground, the same mix frame_jarvis.cpp gives a gauge ring's
 * track, so the unlit arcs read as the meter's empty part rather than as
 * three stray crescents. */
static void fan_arc(lv_layer_t *layer, const lv_point_t *center, int32_t radius,
                    int32_t start_angle, int32_t end_angle, lv_color_t ink, lv_opa_t opa,
                    bool lit)
{
    lv_draw_arc_dsc_t arc;

    lv_draw_arc_dsc_init(&arc);
    arc.center     = *center;
    arc.radius     = radius;
    arc.width      = 2;
    arc.start_angle = start_angle;
    arc.end_angle  = end_angle;
    arc.rounded    = 0;

    if (lit == true)
    {
        arc.color = ink;
        arc.opa   = opa;
    }
    else
    {
        arc.color = lv_color_mix(ink, lv_color_hex(ui_style_theme()->screen.bg), 60);
        arc.opa   = (lv_opa_t)(opa / 2);
    }

    lv_draw_arc(layer, &arc);
}

static void fan_dot(lv_layer_t *layer, const lv_point_t *center, lv_color_t ink, lv_opa_t opa)
{
    lv_draw_rect_dsc_t dot;
    lv_area_t          a;

    lv_draw_rect_dsc_init(&dot);
    dot.bg_color = ink;
    dot.bg_opa   = opa;
    dot.radius   = 2;

    a.x1 = center->x - 2;
    a.y1 = center->y - 2;
    a.x2 = center->x + 1;
    a.y2 = center->y + 1;

    lv_draw_rect(layer, &dot, &a);
}

/* The wifi fan, opening upward: 225 to 315 in LVGL's angles (0 at three
 * o'clock, clockwise) is up-left through the top to up-right, around a
 * centre on the object's bottom edge. */
static void wifi_fan_draw_event(lv_event_t *e)
{
    lv_obj_t   *obj   = (lv_obj_t *)lv_event_get_target(e);
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_area_t    c;
    lv_point_t   center;
    int          level = (int)(uintptr_t)lv_obj_get_user_data(obj);

    lv_obj_get_coords(obj, &c);

    center.x = c.x1 + 11;
    center.y = c.y2 - 2;

    lv_color_t ink = lv_obj_get_style_text_color(obj, LV_PART_MAIN);
    lv_opa_t   opa = lv_obj_get_style_text_opa(obj, LV_PART_MAIN);

    fan_dot(layer, &center, ink, opa);

    static const int32_t radii[] = {4, 7, 10};

    for (size_t i = 0; i < sizeof(radii) / sizeof(radii[0]); i++)
        fan_arc(layer, &center, radii[i], 225, 315, ink, opa, level >= 1 + (int)i);
}

lv_obj_t *ui_frame_wifi_fan(lv_obj_t *parent, uint32_t color, lv_opa_t opa)
{
    lv_obj_t *fan = ui_frame_container(parent);

    lv_obj_set_size(fan, 22, 18);
    lv_obj_set_style_text_color(fan, lv_color_hex(color), 0);
    lv_obj_set_style_text_opa(fan, opa, 0);
    lv_obj_add_event_cb(fan, wifi_fan_draw_event, LV_EVENT_DRAW_MAIN_END, NULL);
    lv_obj_add_flag(fan, LV_OBJ_FLAG_HIDDEN);

    return fan;
}

void ui_frame_wifi_fan_set(lv_obj_t *fan, int level)
{
    if (fan == NULL)
        return;

    if (level < 1)
    {
        lv_obj_add_flag(fan, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    if (level > 3)
        level = 3;

    lv_obj_set_user_data(fan, (void *)(uintptr_t)level);
    lv_obj_clear_flag(fan, LV_OBJ_FLAG_HIDDEN);
}

/* The MQTT mark is deliberately absent. A corner-origin quarter-fan, the
 * shape mqtt.org's own logo is, was tried here and taken down again: beside
 * the wifi fan it read as a second strength meter, and on a 30 px band the
 * two arcs-for-a-purpose side by each other were more confusion than a
 * badge is worth. MQTT stays what it always was -- a client with topics,
 * not a radio the status row owes an icon to. */

void ui_frame_clock_steady(char *dst, size_t size, const char *text)
{
    size_t i = 0;

    if (dst == NULL || size == 0)
        return;

    for (; text[i] != '\0' && i < size - 1; i++)
        dst[i] = (text[i] == ' ') ? ':' : text[i];

    dst[i] = '\0';
}

lv_obj_t *ui_frame_block(lv_obj_t *parent, int16_t x, int16_t y, int16_t w, int16_t h,
                         uint32_t color, int16_t radius)
{
    lv_obj_t *obj = lv_obj_create(parent);

    lv_obj_set_scrollable(obj, false);
    lv_obj_set_clickable(obj, false);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_radius(obj, radius, 0);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);

    return obj;
}

/* ------------------------------------------------------------ the classic frame
 *
 * A flex row of clock, title, signal and WLAN glyph, a third of an inch tall,
 * over a content area filling the rest. This is what every family looked like
 * before they were given frames of their own, and it stays as the one a family
 * uses until its own arrives.
 *
 * The height is fixed rather than LV_SIZE_CONTENT, because centring children
 * inside a content-sized parent would be circular. */

#define CLASSIC_HEADER_H (LV_DPI_DEF / 3)

static struct
{
    lv_obj_t *bar;
    lv_obj_t *clock;
    lv_obj_t *title;
    lv_obj_t *fan;
    lv_obj_t *wifi;
    lv_obj_t *bt;
    lv_obj_t *notice;
} classic;

static void classic_build(lv_obj_t *parent)
{
    classic.bar = ui_frame_container(parent);
    lv_obj_set_size(classic.bar, lv_pct(100), CLASSIC_HEADER_H);
    lv_obj_set_pos(classic.bar, 0, 0);
    lv_obj_set_style_pad_hor(classic.bar, LV_DPI_DEF / 10, 0);
    lv_obj_set_style_pad_column(classic.bar, LV_DPI_DEF / 20, 0);
    lv_obj_set_flex_flow(classic.bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(classic.bar, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    ui_frame_settings_target(classic.bar);

    classic.clock = lv_label_create(classic.bar);
    lv_label_set_text(classic.clock, "--:--");

    classic.title = lv_label_create(classic.bar);
    lv_label_set_text(classic.title, "Welcome to OhEzTouch");
    lv_label_set_long_mode(classic.title, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(classic.title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_flex_grow(classic.title, 1);

    /* The link, in the order it is read: the strength fan, the glyph for the
     * states with no strength to show (REFRESH while down, SHUFFLE on a
     * wire), then the BLE badge. The fan and the glyph are never both
     * visible -- set_link() below sees to that -- so the row never carries
     * an empty slot for the one that is not showing. */
    const struct ui_theme_s *t = ui_style_theme();

    classic.fan = ui_frame_wifi_fan(classic.bar, t->screen.text, LV_OPA_COVER);

    classic.wifi = lv_label_create(classic.bar);
    lv_label_set_text(classic.wifi, LV_SYMBOL_POWER);

    classic.bt = lv_label_create(classic.bar);
    lv_label_set_text(classic.bt, LV_SYMBOL_BLUETOOTH);
    lv_obj_add_flag(classic.bt, LV_OBJ_FLAG_HIDDEN);

    /* Last in the row, past the link readout: the corner a message is least
     * likely to be confused with a reading. */
    classic.notice = ui_frame_notice(classic.bar);
}

static void classic_destroy(void)
{
    if (classic.bar != NULL)
        lv_obj_delete(classic.bar);

    classic = {};
}

static lv_area_t classic_content_area(void)
{
    lv_area_t a;

    a.x1 = 0;
    a.y1 = CLASSIC_HEADER_H;
    a.x2 = lv_display_get_horizontal_resolution(NULL) - 1;
    a.y2 = lv_display_get_vertical_resolution(NULL) - 1;

    return a;
}

static void classic_set_title(const char *title)
{
    if (classic.title != NULL)
        lv_label_set_text(classic.title, title);
}

static void classic_set_clock(const char *text)
{
    if (classic.clock == NULL)
        return;

    /* A steady colon, unlike the header clock: the blink there is a matter of
     * taste and the families differ, and this one wants none of it. Barlow
     * is proportional, so the steady rewrite is not optional either -- passed
     * through, the minutes would step sideways once a second by the
     * difference between the face's colon and its space. */
    char steady[8];

    ui_frame_clock_steady(steady, sizeof(steady), text);
    lv_label_set_text(classic.clock, steady);
}

static void classic_set_link(bool online, int rssi)
{
    if (classic.wifi == NULL || classic.fan == NULL)
        return;

    /* On the radio the fan carries the strength and the glyph goes; the two
     * are never both up, so the row closes over whichever is hidden. A wired
     * link has no strength and the fan would be inventing it, so there it is
     * the glyph that stays. */
    if (online == true && rssi >= 0)
    {
        ui_frame_wifi_fan_set(classic.fan, 1 + (rssi >= 34) + (rssi >= 67));
        lv_obj_add_flag(classic.wifi, LV_OBJ_FLAG_HIDDEN);
    }
    else
    {
        lv_label_set_text(classic.wifi, online ? LV_SYMBOL_SHUFFLE : LV_SYMBOL_REFRESH);
        lv_obj_clear_flag(classic.wifi, LV_OBJ_FLAG_HIDDEN);
        ui_frame_wifi_fan_set(classic.fan, 0);
    }
}

static void classic_set_radios(bool ble)
{
    if (classic.bt != NULL)
        (ble == true) ? lv_obj_clear_flag(classic.bt, LV_OBJ_FLAG_HIDDEN)
                      : lv_obj_add_flag(classic.bt, LV_OBJ_FLAG_HIDDEN);
}

static void classic_set_notice(enum ui_notice_e notice)
{
    ui_frame_notice_set(classic.notice, notice);
}

const struct ui_frame_ops_s ui_frame_classic = {
    classic_build,     classic_destroy,  classic_content_area, classic_set_title,
    classic_set_clock, classic_set_link, classic_set_radios,   classic_set_notice,
    NULL};
