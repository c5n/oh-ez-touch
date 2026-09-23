/**
 * @file item_setpoint.cpp
 *
 * Setpoints: the value, and two pads big enough to hit without looking.
 */
#include "item_screen.hpp"

#include "ui/ui_beep.hpp"
#include "ui/ui_style.hpp"

/* Half the screen each, near enough. The button matrix this replaces was 100%
 * wide and 40% tall with two cells in it, which was already usable -- what it
 * was not was reachable without looking, because the cells had no separation
 * and no acknowledgement of their own. */
/* 4 + 60 + 6 + 106 + 4 = 180 against the body's 184. The pads take everything
 * the value does not, because they are what the finger is for. */
#define VALUE_H 60
#define PADS_H  106
#define PAD_GAP 8

/* Move the value, and say whether it actually moved.
 *
 * `quiet` is for the repeat: holding a pad publishes about ten times a second,
 * and ten CHANGE chimes a second is what this used to do. The caller plays a
 * tick instead, and the CLICKED that LVGL still sends on release ends the hold
 * with a proper commit.
 *
 * The early return when nothing changed is a fix rather than part of that: at
 * either end of the range this went on publishing the same value to openHAB
 * for as long as the finger stayed down. */
static bool step_by(struct item_view_s *v, float delta, bool quiet)
{
    float current = v->item->getStateNumber();
    float next    = current + delta;

    if (next > v->item->getMaxVal())
        next = v->item->getMaxVal();

    if (next < v->item->getMinVal())
        next = v->item->getMinVal();

    if (next == current)
        return false;

    v->item->setStateNumber(next);
    item_screen_set_pattern(v->value, v->item, next);

    if (quiet == true)
        item_screen_publish_quiet(v);
    else
        item_screen_publish(v);

    return true;
}

static void minus_event(lv_event_t *e)
{
    struct item_view_s *v = (struct item_view_s *)lv_event_get_user_data(e);

    step_by(v, -v->item->getStep(), false);
}

static void plus_event(lv_event_t *e)
{
    struct item_view_s *v = (struct item_view_s *)lv_event_get_user_data(e);

    step_by(v, v->item->getStep(), false);
}

/* The hold. One tick per step rather than one chime per step, and a tick with
 * a direction so that a hold sounds like a value moving rather than like a
 * stutter.
 *
 * The guard is local and 150 ms rather than leaning on ui_beep's own 100 ms
 * tick gap, which ties with LVGL's 100 ms repeat period and would drop roughly
 * every other tick in a pattern nobody can predict. At 150 the rhythm is
 * regular and ui_beep's guard is what it should be: a backstop. */
static void repeat_step(struct item_view_s *v, float delta)
{
    /* One screen is open at a time -- item_screen guarantees it -- so a file
     * static is the whole of the state this needs. */
    static uint32_t last_tick;

    if (step_by(v, delta, true) == false)
        return;

    if (lv_tick_elaps(last_tick) < 150)
        return;

    last_tick = lv_tick_get();

    if (delta < 0)
        BEEPER_EVENT_TICK_BACK();
    else
        BEEPER_EVENT_TICK();
}

static void minus_repeat_event(lv_event_t *e)
{
    struct item_view_s *v = (struct item_view_s *)lv_event_get_user_data(e);

    repeat_step(v, -v->item->getStep());
}

static void plus_repeat_event(lv_event_t *e)
{
    struct item_view_s *v = (struct item_view_s *)lv_event_get_user_data(e);

    repeat_step(v, v->item->getStep());
}

static lv_obj_t *pad_create(struct item_view_s *v, lv_obj_t *parent, const char *symbol,
                            lv_event_cb_t cb, lv_event_cb_t repeat_cb)
{
    lv_obj_t *pad = item_screen_button(parent, symbol);

    lv_obj_set_flex_grow(pad, 1);
    lv_obj_set_height(pad, lv_pct(100));

    /* CLICKED for the single step, and LONG_PRESSED_REPEAT so that holding it
     * walks the value instead of asking for one tap per degree. LVGL's defaults
     * are a 400 ms hold and a 100 ms repeat, which is about right here.
     *
     * Two callbacks rather than one, and that is the point: the repeat used to
     * be the same handler, so holding a pad played the commit chime ten times
     * a second. */
    lv_obj_add_event_cb(pad, cb, LV_EVENT_CLICKED, v);
    lv_obj_add_event_cb(pad, repeat_cb, LV_EVENT_LONG_PRESSED_REPEAT, v);

    /* The glyph, not the caption font: this is the only thing on the pad. */
    item_screen_glyph(pad);

    return pad;
}

static void build(struct item_view_s *v)
{
    lv_obj_set_flex_flow(v->body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(v->body, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(v->body, 4, 0);
    lv_obj_set_style_pad_row(v->body, 6, 0);

    /* The value in the large face, its unit one below it. */
    v->value = ui_reading_create(v->body, &ui_style_label_large, &ui_style_label_state,
                                 VALUE_H);
    item_screen_set_pattern(v->value, v->item, v->item->getStateNumber());

    /* A row of its own, so the two pads can be a percentage of something with
     * a known height rather than of the whole body. */
    lv_obj_t *row = item_screen_container(v->body);

    lv_obj_set_size(row, lv_pct(100), PADS_H);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, PAD_GAP, 0);

    v->control  = pad_create(v, row, LV_SYMBOL_MINUS, minus_event, minus_repeat_event);
    v->extra[0] = pad_create(v, row, LV_SYMBOL_PLUS, plus_event, plus_repeat_event);
}

static void refresh(struct item_view_s *v)
{
    item_screen_set_pattern(v->value, v->item, v->item->getStateNumber());
}

const struct item_screen_dsc_s item_screen_setpoint = {
    ItemType::type_setpoint, build, refresh, NULL};
