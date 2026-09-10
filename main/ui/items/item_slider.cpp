/**
 * @file item_slider.cpp
 *
 * Sliders and dimmers: one big field you drag anywhere, and five presets.
 */
#include "item_screen.hpp"

#include "ui/ui_motion.hpp"
#include "ui/ui_style.hpp"

/* Percentages of the item's range, so they read literally for a Dimmer and
 * still mean something for a Slider openHAB gave a narrower range. */
static const uint8_t preset_percent[] = {0, 25, 50, 75, 100};

#define PRESET_COUNT (sizeof(preset_percent) / sizeof(preset_percent[0]))

/* Tall enough to hit without looking. The old slider was LV_DPI_DEF/3 -- 33 px,
 * five millimetres -- with a knob you had to find first.
 *
 * These have to add up: 4 + 54 + 6 + 56 + 6 + 48 + 4 = 178 against the 184 the
 * body has. There is no scrolling here on purpose, so anything that does not
 * fit is simply clipped off the bottom of the glass. */
#define FIELD_H  56
#define VALUE_H  54
#define PRESET_H 48
#define KNOB_W   6

static int16_t preset_value(Item *item, uint8_t percent)
{
    float min_val = item->getMinVal();
    float max_val = item->getMaxVal();

    return (int16_t)(min_val + (max_val - min_val) * percent / 100.0f);
}

/* Mark whichever preset the slider is sitting on, if any. A value between two
 * of them leaves all five unmarked, which is the common case while dragging. */
static void presets_refresh(struct item_view_s *v)
{
    if (v->extra[0] == NULL || v->control == NULL)
        return;

    int32_t value = lv_slider_get_value(v->control);

    for (size_t i = 0; i < PRESET_COUNT; i++)
    {
        lv_obj_t *btn = lv_obj_get_child(v->extra[0], (int32_t)i);

        if (btn == NULL)
            continue;

        if (preset_value(v->item, preset_percent[i]) == value)
            lv_obj_add_state(btn, LV_STATE_CHECKED);
        else
            lv_obj_remove_state(btn, LV_STATE_CHECKED);
    }
}

/* Dragging previews; it does not publish. Holding the bus open with a command
 * per pixel would flood the queue and the lamp would chase the finger. */
static void drag_event(lv_event_t *e)
{
    struct item_view_s *v = (struct item_view_s *)lv_event_get_user_data(e);
    int32_t value = lv_slider_get_value(v->control);

    v->item->setStateNumber(value);
    item_screen_set_pattern(v->value, v->item, (float)value);
    presets_refresh(v);
}

static void release_event(lv_event_t *e)
{
    struct item_view_s *v = (struct item_view_s *)lv_event_get_user_data(e);

    item_screen_publish(v);
    presets_refresh(v);
}

static void preset_event(lv_event_t *e)
{
    struct item_view_s *v = (struct item_view_s *)lv_event_get_user_data(e);
    lv_obj_t *btn = (lv_obj_t *)lv_event_get_target(e);
    const uint8_t *percent = (const uint8_t *)lv_obj_get_user_data(btn);
    int16_t value = preset_value(v->item, *percent);

    lv_slider_set_value(v->control, value, LV_ANIM_ON);
    v->item->setStateNumber(value);
    item_screen_set_pattern(v->value, v->item, (float)value);

    item_screen_publish(v);
    presets_refresh(v);
}

static void build(struct item_view_s *v)
{
    lv_obj_set_flex_flow(v->body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(v->body, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(v->body, 4, 0);
    lv_obj_set_style_pad_row(v->body, 6, 0);

    /* The number, and it is the point of the screen. */
    v->value = lv_label_create(v->body);
    lv_obj_add_style(v->value, &ui_style_label_large, LV_PART_MAIN);
    lv_obj_set_width(v->value, lv_pct(100));
    lv_obj_set_height(v->value, VALUE_H);
    lv_obj_set_style_text_align(v->value, LV_TEXT_ALIGN_CENTER, 0);
    item_screen_set_pattern(v->value, v->item, v->item->getStateNumber());

    /* The field. An lv_slider whose knob is the full height of the track, so
     * there is nothing to aim at -- pressing anywhere in the bar takes the
     * value there and dragging moves it. */
    v->control = lv_slider_create(v->body);
    lv_obj_add_style(v->control, &ui_style_slider, LV_PART_MAIN);
    lv_obj_add_style(v->control, &ui_style_slider_indicator, LV_PART_INDICATOR);
    lv_obj_add_style(v->control, &ui_style_slider_knob, LV_PART_KNOB);
    lv_slider_set_range(v->control, v->item->getMinVal(), v->item->getMaxVal());
    lv_slider_set_value(v->control, v->item->getStateNumber(), LV_ANIM_OFF);
    lv_obj_set_width(v->control, lv_pct(100));
    lv_obj_set_height(v->control, FIELD_H);
    /* A thin marker at the fill edge rather than a grab handle, because there
     * is nothing to grab: the whole bar is the target.
     *
     * It has to be done with negative padding. lv_slider takes the knob's size
     * from the track -- knob_size = lv_obj_get_height(obj) for a horizontal
     * one -- and then adds the four pads; LV_STYLE_WIDTH on LV_PART_KNOB is
     * never read. So a 56 px track gives a 56 px square block unless 25 px is
     * taken off each side. */
    lv_obj_set_style_pad_all(v->control, 0, LV_PART_KNOB);
    lv_obj_set_style_pad_hor(v->control, -(FIELD_H - KNOB_W) / 2, LV_PART_KNOB);
    lv_obj_set_style_radius(v->control, 6, LV_PART_MAIN);
    lv_obj_set_style_radius(v->control, 6, LV_PART_INDICATOR);
    lv_obj_add_event_cb(v->control, drag_event, LV_EVENT_VALUE_CHANGED, v);
    lv_obj_add_event_cb(v->control, release_event, LV_EVENT_RELEASED, v);

    /* Five pads along the bottom. flex_grow shares the width out, so they stay
     * as wide as the screen allows however many there are. */
    v->extra[0] = item_screen_container(v->body);
    lv_obj_set_size(v->extra[0], lv_pct(100), PRESET_H);
    lv_obj_set_flex_flow(v->extra[0], LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(v->extra[0], LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(v->extra[0], 4, 0);

    for (size_t i = 0; i < PRESET_COUNT; i++)
    {
        char text[8];

        lv_snprintf(text, sizeof(text), "%u%%", (unsigned)preset_percent[i]);

        lv_obj_t *btn = item_screen_button(v->extra[0], text);

        lv_obj_set_flex_grow(btn, 1);
        lv_obj_set_height(btn, lv_pct(100));
        lv_obj_set_style_pad_all(btn, 0, 0);
        lv_obj_set_user_data(btn, (void *)&preset_percent[i]);
        lv_obj_add_event_cb(btn, preset_event, LV_EVENT_CLICKED, v);
    }

    presets_refresh(v);
}

/* The server moved the item while this screen was up. The old windows never
 * did this, so a dimmer changed from a phone left a stale number on the glass. */
static void refresh(struct item_view_s *v)
{
    if (v->control == NULL)
        return;

    lv_slider_set_value(v->control, v->item->getStateNumber(), LV_ANIM_ON);
    item_screen_set_pattern(v->value, v->item, v->item->getStateNumber());
    presets_refresh(v);
}

const struct item_screen_dsc_s item_screen_slider = {
    ItemType::type_slider, build, refresh, NULL};
