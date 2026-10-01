/**
 * @file item_color.cpp
 *
 * Colorpickers: a swatch and three fields you can drag anywhere.
 *
 * LVGL v9 has no colour wheel -- lv_cpicker went in v8 and lv_colorwheel was
 * dropped in v9, with nothing in core taking their place -- so this is HSV as
 * three bars. What changed here is their size: they were LV_SIZE_CONTENT rows
 * with a label beside a thin track, and they are now three 48 px fields with the
 * label above, which is the difference between adjusting a colour and hunting
 * for a knob.
 *
 * Each field's track is the range it sweeps, with the other two held where
 * they are: hue round the wheel at the current S and V, saturation from grey
 * to full, value from black to full. Dragging one field repaints the other
 * two, so the tracks always show what moving the knob there would give.
 *
 * Every theme gets the same tracks. The field's indicator is cleared, because
 * a filled bar from the left edge to the knob would hide the half of the
 * gradient the knob has passed, and on these fields where the knob sits is
 * the reading, not how much of something there is.
 */
#include "item_screen.hpp"

#include "ui/ui_beep.hpp"

#include "ui/ui_style.hpp"

#include <stdlib.h>

enum
{
    HSV_H = 0,
    HSV_S,
    HSV_V,
    HSV_COUNT
};

/* 4 + 36 + 3*(4 + 40) + 4 = 176 against the body's 184. The label sits beside
 * its field rather than above it, which is what buys the three fields room to
 * be 40 px each instead of the 20 they would get stacked. */
#define FIELD_H  40
#define SWATCH_H 36
#define LABEL_W  28

/* One per field, alive as long as the style property pointing at it: LVGL
 * keeps the pointer, not a copy. One colour screen is open at a time. */
static lv_grad_dsc_t track_grad[HSV_COUNT];

/* The hue wheel's six sectors and the red that closes it. */
#define HUE_STOPS 7

/* The item's state as HSV. Item::getStateHsv() owns the format and the
 * bounds; a state it refuses leaves black, which is a colour the three fields
 * below can show and be dragged away from. */
static lv_color_hsv_t hsv_parse(Item *item)
{
    lv_color_hsv_t hsv = {};

    item->getStateHsv(&hsv.h, &hsv.s, &hsv.v);

    return hsv;
}

static lv_color_hsv_t hsv_read(struct item_view_s *v)
{
    lv_color_hsv_t hsv;

    hsv.h = (uint16_t)lv_slider_get_value(v->extra[HSV_H]);
    hsv.s = (uint8_t)lv_slider_get_value(v->extra[HSV_S]);
    hsv.v = (uint8_t)lv_slider_get_value(v->extra[HSV_V]);

    return hsv;
}

/* Rewrite one track's stops in place and repaint it. Only the stops change, so
 * the style property can keep pointing at the same descriptor. */
static void track_set(lv_obj_t *field, lv_grad_dsc_t *grad, const lv_color_t colors[],
                      int count)
{
    lv_grad_init_stops(grad, colors, NULL, NULL, count);
    lv_obj_invalidate(field);
}

static void tracks_update(struct item_view_s *v, int skip)
{
    lv_color_hsv_t hsv = hsv_read(v);

    /* A field's own track does not depend on its own value, so the one being
     * dragged is left alone -- a third less to repaint on every move. */
    if (skip != HSV_H)
    {
        lv_color_t hue[HUE_STOPS];

        for (int i = 0; i < HUE_STOPS; i++)
            hue[i] = lv_color_hsv_to_rgb((uint16_t)((i * 60) % 360), hsv.s, hsv.v);

        track_set(v->extra[HSV_H], &track_grad[HSV_H], hue, HUE_STOPS);
    }

    if (skip != HSV_S)
    {
        lv_color_t sat[2] = {lv_color_hsv_to_rgb(hsv.h, 0, hsv.v),
                             lv_color_hsv_to_rgb(hsv.h, 100, hsv.v)};

        track_set(v->extra[HSV_S], &track_grad[HSV_S], sat, 2);
    }

    if (skip != HSV_V)
    {
        lv_color_t val[2] = {lv_color_hsv_to_rgb(hsv.h, hsv.s, 0),
                             lv_color_hsv_to_rgb(hsv.h, hsv.s, 100)};

        track_set(v->extra[HSV_V], &track_grad[HSV_V], val, 2);
    }
}

/* The swatch is the feedback the colour disc used to give. */
static void swatch_update(struct item_view_s *v)
{
    lv_color_hsv_t hsv = hsv_read(v);

    lv_obj_set_style_bg_color(v->control, lv_color_hsv_to_rgb(hsv.h, hsv.s, hsv.v), 0);
    lv_obj_set_style_bg_opa(v->control, LV_OPA_COVER, 0);
}

static void drag_event(lv_event_t *e)
{
    struct item_view_s *v     = (struct item_view_s *)lv_event_get_user_data(e);
    lv_obj_t           *field = (lv_obj_t *)lv_event_get_target(e);
    int                 skip  = HSV_COUNT;

    for (int i = 0; i < HSV_COUNT; i++)
        if (v->extra[i] == field)
            skip = i;

    swatch_update(v);
    tracks_update(v, skip);
}

static void release_event(lv_event_t *e)
{
    struct item_view_s *v = (struct item_view_s *)lv_event_get_user_data(e);
    lv_color_hsv_t      hsv = hsv_read(v);
    char                text[24];

    lv_snprintf(text, sizeof(text), "%u,%u,%u", (unsigned)hsv.h, (unsigned)hsv.s,
                (unsigned)hsv.v);
    v->item->setStateText(text);

    item_screen_publish(v);
}

static lv_obj_t *field_create(struct item_view_s *v, int index, const char *name,
                              int32_t max, int32_t value)
{
    lv_obj_t *row = item_screen_container(v->body);

    lv_obj_set_size(row, lv_pct(100), FIELD_H);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 6, 0);

    lv_obj_t *label = lv_label_create(row);

    lv_label_set_text(label, name);
    lv_obj_add_style(label, &ui_style_label_state, LV_PART_MAIN);
    lv_obj_set_width(label, LABEL_W);

    lv_obj_t *field = lv_slider_create(row);

    lv_obj_add_style(field, &ui_style_slider, LV_PART_MAIN);
    lv_obj_add_style(field, &ui_style_slider_indicator, LV_PART_INDICATOR);
    lv_obj_add_style(field, &ui_style_slider_knob, LV_PART_KNOB);
    lv_slider_set_range(field, 0, max);
    lv_slider_set_value(field, value, LV_ANIM_OFF);
    lv_obj_set_flex_grow(field, 1);
    lv_obj_set_height(field, lv_pct(100));
    /* Negative, for the reason spelled out in item_slider.cpp: the knob's size
     * comes from the track's height and the pads are the only lever on it. */
    lv_obj_set_style_pad_all(field, 0, LV_PART_KNOB);
    lv_obj_set_style_pad_hor(field, -(FIELD_H - 6) / 2, LV_PART_KNOB);
    lv_obj_set_style_radius(field, 6, LV_PART_MAIN);
    lv_obj_set_style_radius(field, 6, LV_PART_INDICATOR);

    /* The track is the gradient; the stops are written by tracks_update(). */
    lv_grad_horizontal_init(&track_grad[index]);
    lv_obj_set_style_bg_opa(field, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_grad(field, &track_grad[index], LV_PART_MAIN);
    lv_obj_set_style_bg_opa(field, LV_OPA_TRANSP, LV_PART_INDICATOR);
    lv_obj_add_event_cb(field, drag_event, LV_EVENT_VALUE_CHANGED, v);
    lv_obj_add_event_cb(field, release_event, LV_EVENT_RELEASED, v);

    /* The contact tick, and deliberately no detent tick to go with it -- which
     * is the one place this screen differs from the dimmer.
     *
     * Three fields are dragged here one after another, and one sound would
     * have to stand for all three; worse, hue is a wheel, so "more" and "less"
     * do not mean anything for it and the rising and falling ticks would be
     * arbitrary. Contact is still worth acknowledging, so that is all this
     * takes. */
    ui_beep_attach_press(field);

    return field;
}

static void build(struct item_view_s *v)
{
    lv_color_hsv_t hsv = hsv_parse(v->item);

    lv_obj_set_flex_flow(v->body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(v->body, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_all(v->body, 4, 0);
    lv_obj_set_style_pad_row(v->body, 4, 0);

    v->control = lv_obj_create(v->body);
    lv_obj_set_scrollable(v->control, false);
    lv_obj_set_size(v->control, lv_pct(100), SWATCH_H);
    lv_obj_add_style(v->control, &ui_style_swatch, LV_PART_MAIN);

    v->extra[HSV_H] = field_create(v, HSV_H, "H", 359, hsv.h);
    v->extra[HSV_S] = field_create(v, HSV_S, "S", 100, hsv.s);
    v->extra[HSV_V] = field_create(v, HSV_V, "V", 100, hsv.v);

    swatch_update(v);
    tracks_update(v, HSV_COUNT);
}

static void refresh(struct item_view_s *v)
{
    lv_color_hsv_t hsv = hsv_parse(v->item);

    lv_slider_set_value(v->extra[HSV_H], hsv.h, LV_ANIM_ON);
    lv_slider_set_value(v->extra[HSV_S], hsv.s, LV_ANIM_ON);
    lv_slider_set_value(v->extra[HSV_V], hsv.v, LV_ANIM_ON);

    /* lv_slider_get_value() answers an animating knob's target, so the tracks
     * are drawn for where the knobs are going rather than partway there. */
    swatch_update(v);
    tracks_update(v, HSV_COUNT);
}

const struct item_screen_dsc_s item_screen_color = {
    ItemType::type_colorpicker, build, refresh, NULL};
