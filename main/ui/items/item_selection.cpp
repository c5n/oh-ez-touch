/**
 * @file item_selection.cpp
 *
 * Selections: one row per choice, the whole row is the target, and a second
 * column rather than a scrollbar when they stop fitting in the height.
 */
#include "item_screen.hpp"

#include "ui/ui_geometry.hpp"
#include "ui/ui_style.hpp"

#include <string.h>

/* A wrapping row of small buttons is what this was, which meant the labels
 * decided the hit areas and a mapping called "Off" was a third the size of one
 * called "Comfort". A list gives every choice the same height.
 *
 * 46 px is the floor and twice that the ceiling: below the first a choice is
 * not reachable with a finger, above the second it has stopped being a row of
 * a list and become a slab -- which is what a screen with one choice on it
 * would otherwise be. Between them the choices stretch to fill the column. */
#define ROW_MIN_H 46
#define ROW_MAX_H (2 * ROW_MIN_H)

/* Between two choices, and between the list and the edge of the body. */
#define GAP 4
#define PAD 6

/* A radio group, so the rows are not LV_OBJ_FLAG_CHECKABLE: a second tap on
 * the active choice must not clear it. The check follows the item's state
 * rather than the tap, which is also what a poll that disagrees needs.
 *
 * Two levels deep now: v->control holds the columns and a column holds the
 * choices. */
static void mark_active(struct item_view_s *v)
{
    if (v->control == NULL)
        return;

    for (uint32_t c = 0; c < lv_obj_get_child_count(v->control); c++)
    {
        lv_obj_t *column = lv_obj_get_child(v->control, c);

        for (uint32_t i = 0; i < lv_obj_get_child_count(column); i++)
        {
            lv_obj_t   *row = lv_obj_get_child(column, i);
            const char *cmd = (const char *)lv_obj_get_user_data(row);

            if (cmd != NULL && strcmp(cmd, v->item->getStateText()) == 0)
                lv_obj_add_state(row, LV_STATE_CHECKED);
            else
                lv_obj_remove_state(row, LV_STATE_CHECKED);
        }
    }
}

static void choose_event(lv_event_t *e)
{
    struct item_view_s *v = (struct item_view_s *)lv_event_get_user_data(e);
    lv_obj_t *row = (lv_obj_t *)lv_event_get_target(e);
    const char *command = (const char *)lv_obj_get_user_data(row);

    if (command == NULL)
        return;

    v->item->setStateText(command);

    mark_active(v);

    item_screen_publish(v);
}

static void build(struct item_view_s *v)
{
    uint8_t count = (uint8_t)v->item->getSelectionCount();

    /* A column of choices, and a second column once they stop fitting in the
     * height -- never a scrollbar. ui_columns_pack() is the whole of that
     * decision, and it is tested on the host. */
    struct ui_columns_s pack =
        ui_columns_pack(count, (int16_t)(ITEM_BODY_H - 2 * PAD), ROW_MIN_H, ROW_MAX_H, GAP);

    v->control = item_screen_container(v->body);
    lv_obj_set_size(v->control, lv_pct(100), lv_pct(100));
    lv_obj_set_flex_flow(v->control, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_all(v->control, PAD, 0);
    lv_obj_set_style_pad_column(v->control, GAP, 0);

    for (uint8_t c = 0; c < pack.cols; c++)
    {
        lv_obj_t *column = item_screen_container(v->control);

        /* Equal shares of the width, whatever is left after the gaps, and the
         * same pitch down every one of them -- so the columns line up as rows
         * across, and it is the last column that ends early. */
        lv_obj_set_height(column, lv_pct(100));
        lv_obj_set_flex_grow(column, 1);
        lv_obj_set_flex_flow(column, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(column, GAP, 0);

        uint8_t first = ui_columns_first(count, pack.cols, c);
        uint8_t here  = ui_columns_count(count, pack.cols, c);

        for (uint8_t i = first; i < first + here; i++)
        {
            lv_obj_t *row = item_screen_button(column, v->item->getSelectionLabel(i));

            lv_obj_set_width(row, lv_pct(100));
            lv_obj_set_height(row, pack.item_h);

            /* The themed button's 8 px of side padding is there to give a
             * content-sized button room around its label. These are sized
             * from the column instead, and in four columns those 16 px are
             * the difference between "Reading" and "Read...". */
            if (pack.cols > 1)
                lv_obj_set_style_pad_hor(row, 2, 0);

            /* The command string lives in the Item, which outlives this screen
             * -- the Sitemap owns it and a page change dismisses us first. */
            lv_obj_set_user_data(row, (void *)v->item->getSelectionCommand(i));
            lv_obj_add_event_cb(row, choose_event, LV_EVENT_CLICKED, v);

            /* One column reads as a list, so its words start where the eye
             * looks for them. Two or more is a grid of buttons rather than a
             * list, and there the indent is 8 px a 74 px column cannot spare:
             * the label is centred and takes the whole row instead. */
            lv_obj_t *label = lv_obj_get_child(row, 0);

            if (label != NULL)
            {
                bool listed = (pack.cols == 1);

                lv_obj_align(label, listed ? LV_ALIGN_LEFT_MID : LV_ALIGN_CENTER,
                             listed ? 8 : 0, 0);
                lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
                lv_obj_set_width(label, listed ? lv_pct(90) : lv_pct(100));
                lv_obj_set_style_text_align(label,
                                            listed ? LV_TEXT_ALIGN_LEFT : LV_TEXT_ALIGN_CENTER, 0);
                /* One line of it, and dotted past that. A column is narrow
                 * enough for "Foxtrot" to be too wide for it, and
                 * LV_LABEL_LONG_DOT only dots once it has run out of *height*
                 * -- left to size itself the label wraps instead, which is
                 * how a four-column list came out reading "Foxtro/t". */
                lv_obj_set_height(label, lv_font_get_line_height(ui_style_theme()->font_small));
            }
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
