/**
 * @file item_selection.cpp
 *
 * Selections: one row per choice, and the whole row is the target.
 */
#include "item_screen.hpp"

#include "ui/ui_style.hpp"

#include <string.h>

/* A wrapping row of small buttons is what this was, which meant the labels
 * decided the hit areas and a mapping called "Off" was a third the size of one
 * called "Comfort". A list gives every choice the same 46 px. */
#define ROW_H 46

static void choose_event(lv_event_t *e)
{
    struct item_view_s *v = (struct item_view_s *)lv_event_get_user_data(e);
    lv_obj_t *row = (lv_obj_t *)lv_event_get_target(e);
    const char *command = (const char *)lv_obj_get_user_data(row);

    if (command == NULL)
        return;

    v->item->setStateText(command);

    /* A radio group, so the rows are not LV_OBJ_FLAG_CHECKABLE: a second tap on
     * the active choice must not clear it. */
    lv_obj_t *list = lv_obj_get_parent(row);

    for (uint32_t i = 0; i < lv_obj_get_child_count(list); i++)
        lv_obj_remove_state(lv_obj_get_child(list, i), LV_STATE_CHECKED);

    lv_obj_add_state(row, LV_STATE_CHECKED);

    item_screen_publish(v);
}

static void mark_active(struct item_view_s *v)
{
    if (v->control == NULL)
        return;

    for (uint32_t i = 0; i < lv_obj_get_child_count(v->control); i++)
    {
        lv_obj_t   *row = lv_obj_get_child(v->control, i);
        const char *cmd = (const char *)lv_obj_get_user_data(row);

        if (cmd != NULL && strcmp(cmd, v->item->getStateText()) == 0)
            lv_obj_add_state(row, LV_STATE_CHECKED);
        else
            lv_obj_remove_state(row, LV_STATE_CHECKED);
    }
}

static void build(struct item_view_s *v)
{
    v->control = item_screen_container(v->body);
    lv_obj_set_size(v->control, lv_pct(100), lv_pct(100));
    lv_obj_add_flag(v->control, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(v->control, LV_DIR_VER);
    lv_obj_set_flex_flow(v->control, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(v->control, 6, 0);
    lv_obj_set_style_pad_row(v->control, 4, 0);

    for (size_t i = 0; i < v->item->getSelectionCount(); i++)
    {
        lv_obj_t *row = item_screen_button(v->control, v->item->getSelectionLabel(i));

        lv_obj_set_width(row, lv_pct(100));
        lv_obj_set_height(row, ROW_H);

        /* The command string lives in the Item, which outlives this screen --
         * the Sitemap owns it and a page change dismisses us first. */
        lv_obj_set_user_data(row, (void *)v->item->getSelectionCommand(i));
        lv_obj_add_event_cb(row, choose_event, LV_EVENT_CLICKED, v);

        /* Left-aligned: a list of words reads as a list, a row of centred words
         * reads as buttons that happen to be stacked. */
        lv_obj_t *label = lv_obj_get_child(row, 0);

        if (label != NULL)
        {
            lv_obj_align(label, LV_ALIGN_LEFT_MID, 8, 0);
            lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
            lv_obj_set_width(label, lv_pct(90));
        }
    }

    mark_active(v);
}

static void refresh(struct item_view_s *v)
{
    mark_active(v);
}

const struct item_screen_dsc_s item_screen_selection = {
    ItemType::type_selection, build, refresh, NULL};
