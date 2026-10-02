/**
 * @file item_rollershutter.cpp
 *
 * Rollershutters: where it is, a field to send it anywhere, and the three
 * commands.
 *
 * A Rollershutter's state is a percent -- 0 open, 100 closed -- and it takes
 * one as a command as well as UP, DOWN and STOP. The screen used to offer the
 * three commands and show nothing at all, so a blind could be sent down but
 * not to half way, and nobody could see from the panel where it had stopped.
 * The same holds for a Group:Rollershutter, whose state is its members'
 * aggregate.
 */
#include "item_screen.hpp"

#include "ui/ui_style.hpp"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* 4 + 48 + 6 + 48 + 6 + 64 + 4 = 180 against the 184 the body has in
 * landscape. Up, stop and down share one row: they are three commands of one
 * kind, and the row is wider than it needs to be for any of them. */
#define VALUE_H 48
#define FIELD_H 48
#define KEYS_H  64

/* Its own unit when openHAB gave it no pattern: a bare "30" on a blind reads
 * as nothing in particular. */
static const char *pattern(Item *item)
{
    const char *own = item->getNumberPattern();

    return (strstr(own, "%%") != NULL) ? own : "%d %%";
}

static void reading_set(struct item_view_s *v, float value)
{
    ui_reading_set_pattern(v->value, pattern(v->item), value);
}

/* NULL, UNDEF -- or the UP an older binding reported -- read as no position
 * rather than as 0, which would be "open". */
static bool position(Item *item, float *out)
{
    const char *state = item->getStateText();
    char       *end;
    float       value = strtof(state, &end);

    if (end == state)
        return false;

    *out = value;

    return true;
}

static void command_event(lv_event_t *e)
{
    struct item_view_s *v = (struct item_view_s *)lv_event_get_user_data(e);
    lv_obj_t *btn = (lv_obj_t *)lv_event_get_target(e);
    const char *command = (const char *)lv_obj_get_user_data(btn);

    if (command == NULL)
        return;

    /* A command, not a position: the blind reports where it is as it
     * moves, and the readout follows that. */
    item_screen_send(v, command);
}

static void drag_event(lv_event_t *e)
{
    struct item_view_s *v = (struct item_view_s *)lv_event_get_user_data(e);

    reading_set(v, (float)lv_slider_get_value(v->control));
}

static void release_event(lv_event_t *e)
{
    struct item_view_s *v = (struct item_view_s *)lv_event_get_user_data(e);
    char text[8];

    int32_t value = lv_slider_get_value(v->control);

    openhab_format_number(text, sizeof(text), (float)value, 1.0f);

    /* Kept as the target, so that the next state that differs from it --
     * the blind on its way there -- moves the readout. */
    v->item->setStateText(text);
    item_screen_publish(v);
}

/* The command string is a literal, so it outlives everything here. */
static lv_obj_t *key_create(struct item_view_s *v, lv_obj_t *parent, const char *symbol,
                            const char *command)
{
    lv_obj_t *btn = item_screen_button(parent, symbol);

    lv_obj_set_flex_grow(btn, 1);
    lv_obj_set_height(btn, lv_pct(100));
    lv_obj_set_user_data(btn, (void *)command);
    lv_obj_add_event_cb(btn, command_event, LV_EVENT_CLICKED, v);

    item_screen_glyph(btn);

    return btn;
}

static void build(struct item_view_s *v)
{
    float value = 0.0f;
    bool  known = position(v->item, &value);

    lv_obj_set_flex_flow(v->body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(v->body, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(v->body, 4, 0);
    lv_obj_set_style_pad_row(v->body, 6, 0);

    v->value = ui_reading_create(v->body, &ui_style_label_large, &ui_style_label_state,
                                 VALUE_H);

    if (known == true)
        reading_set(v, value);
    else
        ui_reading_set_text(v->value, "--");

    /* Left is open, right is closed, which is the way the percent runs. */
    v->control = item_screen_field(v->body, FIELD_H);
    lv_slider_set_range(v->control, 0, 100);
    lv_slider_set_value(v->control, (int32_t)lroundf(value), LV_ANIM_OFF);
    lv_obj_add_event_cb(v->control, drag_event, LV_EVENT_VALUE_CHANGED, v);
    lv_obj_add_event_cb(v->control, release_event, LV_EVENT_RELEASED, v);

    lv_obj_t *row = item_screen_container(v->body);

    lv_obj_set_size(row, lv_pct(100), KEYS_H);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 8, 0);

    v->extra[0] = key_create(v, row, LV_SYMBOL_UP, "UP");
    v->extra[1] = key_create(v, row, LV_SYMBOL_STOP, "STOP");
    v->extra[2] = key_create(v, row, LV_SYMBOL_DOWN, "DOWN");
}

/* The blind moving, reported by the server. Left alone under a finger. */
static void refresh(struct item_view_s *v)
{
    float value;

    if (position(v->item, &value) == false)
        return;

    if (lv_obj_has_state(v->control, LV_STATE_PRESSED) == false)
    {
        lv_slider_set_value(v->control, (int32_t)lroundf(value), LV_ANIM_ON);
        reading_set(v, value);
    }
}

const struct item_screen_dsc_s item_screen_rollershutter = {
    ItemType::type_rollershutter, build, refresh, NULL, false};
