/**
 * @file item_rollershutter.cpp
 *
 * Rollershutters: three commands, three full-width bars.
 */
#include "item_screen.hpp"

#include "ui/ui_style.hpp"

#define ROW_H 56

static void command_event(lv_event_t *e)
{
    struct item_view_s *v = (struct item_view_s *)lv_event_get_user_data(e);
    lv_obj_t *btn = (lv_obj_t *)lv_event_get_target(e);
    const char *command = (const char *)lv_obj_get_user_data(btn);

    if (command == NULL)
        return;

    v->item->setStateText(command);
    item_screen_publish(v);
}

/* The command string is a literal, so it outlives everything here. */
static lv_obj_t *bar_create(struct item_view_s *v, const char *symbol, const char *command)
{
    lv_obj_t *btn = item_screen_button(v->body, symbol);

    lv_obj_set_width(btn, lv_pct(100));
    lv_obj_set_height(btn, ROW_H);
    lv_obj_set_user_data(btn, (void *)command);
    lv_obj_add_event_cb(btn, command_event, LV_EVENT_CLICKED, v);

    item_screen_glyph(btn);

    return btn;
}

static void build(struct item_view_s *v)
{
    lv_obj_set_flex_flow(v->body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(v->body, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(v->body, 8, 0);
    lv_obj_set_style_pad_row(v->body, 6, 0);

    v->control  = bar_create(v, LV_SYMBOL_UP, "UP");
    v->extra[0] = bar_create(v, LV_SYMBOL_STOP, "STOP");
    v->extra[1] = bar_create(v, LV_SYMBOL_DOWN, "DOWN");
}

const struct item_screen_dsc_s item_screen_rollershutter = {
    ItemType::type_rollershutter, build, NULL, NULL};
