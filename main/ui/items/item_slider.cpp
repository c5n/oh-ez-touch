/**
 * @file item_slider.cpp
 *
 * Sliders and dimmers: one big field you drag anywhere, and five presets.
 */
#include "item_screen.hpp"

#include "ui/ui_beep.hpp"

#include "ui/ui_motion.hpp"
#include "ui/ui_style.hpp"

#include <math.h>

/* Percentages of the item's range, so they read literally for a Dimmer and
 * still mean something for a Slider openHAB gave a narrower range. */
static const uint8_t preset_percent[] = {0, 25, 50, 75, 100};

#define PRESET_COUNT (sizeof(preset_percent) / sizeof(preset_percent[0]))

/* These have to add up: 4 + 54 + 6 + 56 + 6 + 48 + 4 = 178 against the 184
 * the body has. There is no scrolling here on purpose, so anything that does
 * not fit is simply clipped off the bottom of the glass. */
#define FIELD_H  56
#define VALUE_H  54
#define PRESET_H 48

/* The field counts steps, not values.
 *
 * lv_slider is an integer, so it used to run from the item's minimum to its
 * maximum and ignore the step: a Setpoint-like Slider from 15 to 26 in 0.5
 * could only be set to whole degrees, and a 1..1000 one in steps of 50 to
 * anything. With the field in steps, every position is a value openHAB would
 * accept, and the value is the minimum plus that many steps. A step that
 * would make the field finer than a pixel is ignored for a hundredth of the
 * range, which is as fine as a finger gets anyway. */
static float field_step(Item *item)
{
    float span = item->getMaxVal() - item->getMinVal();
    float step = item->getStep();

    if (span <= 0.0f)
        return 1.0f;

    if (!(step > 0.0f) || span / step > 1000.0f)
        step = span / 100.0f;

    return step;
}

static int32_t field_count(Item *item)
{
    float   span  = item->getMaxVal() - item->getMinVal();
    int32_t count = (int32_t)lroundf(span / field_step(item));

    return (count < 1) ? 1 : count;
}

static float value_at(Item *item, int32_t index)
{
    float value = item->getMinVal() + (float)index * field_step(item);

    return (value > item->getMaxVal()) ? item->getMaxVal() : value;
}

static int32_t index_of(Item *item, float value)
{
    int32_t index = (int32_t)lroundf((value - item->getMinVal()) / field_step(item));

    if (index < 0)
        return 0;

    return (index > field_count(item)) ? field_count(item) : index;
}

static int32_t preset_index(Item *item, uint8_t percent)
{
    return (int32_t)lroundf((float)field_count(item) * percent / 100.0f);
}

/* Set the item to the field's position, and show it. */
static float take_field(struct item_view_s *v)
{
    float value = value_at(v->item, lv_slider_get_value(v->control));

    v->item->setStateNumber(value);
    item_screen_set_pattern(v->value, v->item, value);

    return value;
}

/* And send it, in the step's own precision. */
static void send_value(struct item_view_s *v, float value)
{
    char text[24];

    openhab_format_number(text, sizeof(text), value, field_step(v->item));
    item_screen_send(v, text);
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

        if (preset_index(v->item, preset_percent[i]) == value)
            lv_obj_add_state(btn, LV_STATE_CHECKED);
        else
            lv_obj_remove_state(btn, LV_STATE_CHECKED);
    }
}

/* The drag tick is a detent, not a pixel.
 *
 * Ten of them across the range, which is roughly a notch every three
 * millimetres of travel on this panel -- enough to feel the value moving
 * without the sound becoming a texture. This is the one control where the
 * finger covers the number it is setting, which is the whole argument for
 * making it audible at all; the colour picker deliberately does not do this,
 * for the reasons in item_color.cpp. */
#define DETENTS 10

/* Dragging previews; it does not publish. Holding the bus open with a command
 * per pixel would flood the queue and the lamp would chase the finger. */
static void drag_event(lv_event_t *e)
{
    /* One item screen is open at a time, so a file static is the whole of the
     * state. -1 is "no detent yet", which is what stops the first
     * VALUE_CHANGED of a drag ticking before anything has moved. */
    static int8_t detent = -1;

    struct item_view_s *v = (struct item_view_s *)lv_event_get_user_data(e);
    int32_t index = lv_slider_get_value(v->control);

    take_field(v);
    presets_refresh(v);

    int8_t now = (int8_t)((index * DETENTS) / field_count(v->item));

    if (detent >= 0 && now != detent)
    {
        /* Direction, so that a drag sounds like a value going somewhere. A
         * fast swipe crosses several detents inside ui_beep's tick gap and the
         * surplus is dropped, which is right: you hear the movement, not every
         * notch. */
        if (now > detent)
            BEEPER_EVENT_TICK();
        else
            BEEPER_EVENT_TICK_BACK();
    }

    detent = now;
}

static void release_event(lv_event_t *e)
{
    struct item_view_s *v = (struct item_view_s *)lv_event_get_user_data(e);

    send_value(v, take_field(v));
    presets_refresh(v);
}

static void preset_event(lv_event_t *e)
{
    struct item_view_s *v = (struct item_view_s *)lv_event_get_user_data(e);
    lv_obj_t *btn = (lv_obj_t *)lv_event_get_target(e);
    const uint8_t *percent = (const uint8_t *)lv_obj_get_user_data(btn);

    lv_slider_set_value(v->control, preset_index(v->item, *percent), LV_ANIM_ON);

    send_value(v, take_field(v));
    presets_refresh(v);
}

static void build(struct item_view_s *v)
{
    lv_obj_set_flex_flow(v->body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(v->body, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(v->body, 4, 0);
    lv_obj_set_style_pad_row(v->body, 6, 0);

    /* The number, and it is the point of the screen. The unit keeps out of
     * its way a face below. */
    v->value = ui_reading_create(v->body, &ui_style_label_large, &ui_style_label_state,
                                 VALUE_H);
    item_screen_set_pattern(v->value, v->item, v->item->getStateNumber());

    /* The field, in steps; see field_step(). */
    v->control = item_screen_field(v->body, FIELD_H);
    lv_slider_set_range(v->control, 0, field_count(v->item));
    lv_slider_set_value(v->control, index_of(v->item, v->item->getStateNumber()),
                        LV_ANIM_OFF);
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

    /* Not under a finger, which a state arriving mid drag would take the
     * knob away from. */
    if (lv_obj_has_state(v->control, LV_STATE_PRESSED) == true)
        return;

    lv_slider_set_value(v->control, index_of(v->item, v->item->getStateNumber()),
                        LV_ANIM_ON);
    item_screen_set_pattern(v->value, v->item, v->item->getStateNumber());
    presets_refresh(v);
}

const struct item_screen_dsc_s item_screen_slider = {
    ItemType::type_slider, build, refresh, NULL, false};
