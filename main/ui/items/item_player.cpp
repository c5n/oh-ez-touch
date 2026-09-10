/**
 * @file item_player.cpp
 *
 * Players: transport, with play the size it deserves.
 */
#include "item_screen.hpp"

#include "ui/ui_style.hpp"

#include <string.h>

/* Play and pause are what anyone reaches for, so they get the top of the screen
 * to themselves; previous and next share the row below. The old window put all
 * four in one wrapping row of content-sized buttons. */
#define PRIMARY_H   80
#define SECONDARY_H 88

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

static lv_obj_t *key_create(struct item_view_s *v, lv_obj_t *parent, const char *symbol,
                            const char *command)
{
    lv_obj_t *btn = item_screen_button(parent, symbol);

    lv_obj_set_user_data(btn, (void *)command);
    lv_obj_add_event_cb(btn, command_event, LV_EVENT_CLICKED, v);

    item_screen_glyph(btn);

    return btn;
}

/* PLAY when it is not playing, PAUSE when it is. One key rather than two,
 * because the state already says which one it would be. */
static void primary_sync(struct item_view_s *v)
{
    if (v->control == NULL)
        return;

    bool playing = (strcmp(v->item->getStateText(), "PLAY") == 0);
    lv_obj_t *label = lv_obj_get_child(v->control, 0);

    lv_obj_set_user_data(v->control, (void *)(playing ? "PAUSE" : "PLAY"));

    if (label != NULL)
        lv_label_set_text(label, playing ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
}

static void build(struct item_view_s *v)
{
    lv_obj_set_flex_flow(v->body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(v->body, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(v->body, 8, 0);
    lv_obj_set_style_pad_row(v->body, 8, 0);

    v->control = key_create(v, v->body, LV_SYMBOL_PLAY, "PLAY");
    lv_obj_set_width(v->control, lv_pct(100));
    lv_obj_set_height(v->control, PRIMARY_H);

    lv_obj_t *row = item_screen_container(v->body);

    lv_obj_set_size(row, lv_pct(100), SECONDARY_H);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 8, 0);

    v->extra[0] = key_create(v, row, LV_SYMBOL_PREV, "PREVIOUS");
    v->extra[1] = key_create(v, row, LV_SYMBOL_NEXT, "NEXT");

    for (int i = 0; i < 2; i++)
    {
        lv_obj_set_flex_grow(v->extra[i], 1);
        lv_obj_set_height(v->extra[i], lv_pct(100));
    }

    primary_sync(v);
}

static void refresh(struct item_view_s *v)
{
    primary_sync(v);
}

const struct item_screen_dsc_s item_screen_player = {
    ItemType::type_player, build, refresh, NULL};
