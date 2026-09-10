/**
 * @file item_screen.cpp
 *
 * The frame every item screen wears, the registry that replaces the switch, and
 * the one open screen's lifetime.
 */
#include "item_screen.hpp"

#include "openhab/openhab_client.hpp"
#include "ui/ui_beep.hpp"
#include "ui/ui_motion.hpp"
#include "ui/ui_screen.hpp"
#include "ui/ui_style.hpp"

#include <stdio.h>
#include <string.h>

/* One at a time. A second open would strand the first with no way back to it,
 * so item_screen_open() closes what is up rather than stacking. */
static struct item_view_s view;

static item_screen_changed_cb_t changed_cb;

/* The table that used to be a switch in openhab_ui.cpp's event_handler().
 * Function pointers to statics are link-time constants, so this is .rodata --
 * the same reason the theme table can hold font pointers. */
static const struct item_screen_dsc_s *const registry[] = {
    &item_screen_slider,   &item_screen_setpoint, &item_screen_selection,
    &item_screen_rollershutter, &item_screen_player, &item_screen_color,
};

#define REGISTRY_COUNT (sizeof(registry) / sizeof(registry[0]))

const struct item_screen_dsc_s *item_screen_find(enum ItemType type)
{
    for (size_t i = 0; i < REGISTRY_COUNT; i++)
        if (registry[i]->type == type)
            return registry[i];

    return NULL;
}

void item_screen_set_changed_cb(item_screen_changed_cb_t cb)
{
    changed_cb = cb;
}

/* ------------------------------------------------------- builder utilities */

void item_screen_publish(struct item_view_s *v)
{
    if (v == NULL || v->item == NULL)
        return;

    /* Fire and forget, as it has always effectively been: the tile's own state
     * is already set locally and the five-second poll is what reconciles it. */
    openhab_client_command(v->item->getLink(), v->item->getStateText());

    if (changed_cb != NULL)
        changed_cb(v->slot);

    BEEPER_EVENT_CHANGE();
}

lv_obj_t *item_screen_container(lv_obj_t *parent)
{
    lv_obj_t *obj = lv_obj_create(parent);

    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_set_style_pad_gap(obj, 0, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_radius(obj, 0, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);

    return obj;
}

lv_obj_t *item_screen_button(lv_obj_t *parent, const char *text)
{
    lv_obj_t *btn = lv_button_create(parent);

    lv_obj_add_style(btn, &ui_style_btn, LV_PART_MAIN);
    lv_obj_add_style(btn, &ui_style_btn_checked,
                     ui_style_selector(LV_PART_MAIN, LV_STATE_CHECKED));
    ui_motion_pressable(btn);

    lv_obj_t *label = lv_label_create(btn);

    lv_label_set_text(label, text);
    lv_obj_add_style(label, &ui_style_label, LV_PART_MAIN);
    lv_obj_center(label);

    return btn;
}

void item_screen_glyph(lv_obj_t *btn)
{
    lv_obj_t *label = lv_obj_get_child(btn, 0);

    if (label != NULL)
        lv_obj_set_style_text_font(label, ui_style_theme()->font_normal, 0);
}

void item_screen_set_pattern(lv_obj_t *label, Item *item, float value)
{
    const char *pattern = item->getNumberPattern();

    /* "%d" -- what openHAB sends when it has no pattern of its own -- needs an
     * integer argument; everything else is fed the float. */
    if (strncmp(pattern, "%d", 2) == 0)
        lv_label_set_text_fmt(label, pattern, (uint16_t)value);
    else
        lv_label_set_text_fmt(label, pattern, value);
}

/* ------------------------------------------------------------------ the frame */

static void back_event(lv_event_t *e)
{
    LV_UNUSED(e);
    item_screen_close();
}

/* A bar across the whole top edge, and all of it is the way back.
 *
 * Edge-anchored and full width, so it is the easiest thing on the screen to
 * hit -- the opposite of the glyph-sized close button it replaces. 56 px is
 * about 8.5 mm on the 2.4" panel. */
static lv_obj_t *back_bar_create(lv_obj_t *screen, const char *title)
{
    lv_obj_t *bar = lv_obj_create(screen);

    lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(bar, lv_pct(100), ITEM_BAR_H);
    lv_obj_set_pos(bar, 0, 0);
    lv_obj_add_style(bar, &ui_style_win_header, LV_PART_MAIN);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_set_style_pad_hor(bar, 10, 0);
    lv_obj_set_style_pad_ver(bar, 0, 0);
    lv_obj_set_style_pad_column(bar, 10, 0);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    lv_obj_add_flag(bar, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(bar, back_event, LV_EVENT_CLICKED, NULL);
    ui_motion_pressable(bar);

    lv_obj_t *chevron = lv_label_create(bar);

    lv_label_set_text(chevron, LV_SYMBOL_LEFT);

    lv_obj_t *label = lv_label_create(bar);

    lv_label_set_text(label, title);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_obj_set_flex_grow(label, 1);

    /* The bar's children must not eat the tap: the whole bar is the target. */
    lv_obj_remove_flag(chevron, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(label, LV_OBJ_FLAG_CLICKABLE);

    return bar;
}

/* Swipe right to go back, as well as the bar.
 *
 * Additive only. The ArduiTouch panels are resistive, where a swipe is
 * unreliable, so this is never the only way out -- the bar above is. */
static void gesture_event(lv_event_t *e)
{
    LV_UNUSED(e);

    if (lv_indev_get_gesture_dir(lv_indev_active()) == LV_DIR_RIGHT)
        item_screen_close();
}

void item_screen_open(Item *item, uint8_t slot)
{
    const struct item_screen_dsc_s *dsc;

    if (item == NULL)
        return;

    dsc = item_screen_find(item->getType());

    if (dsc == NULL)
        return;

    if (view.screen != NULL)
        item_screen_dismiss();

    memset(&view, 0, sizeof(view));
    view.item = item;
    view.slot = slot;
    view.dsc  = dsc;

    view.screen = ui_screen_create();

    lv_obj_add_event_cb(view.screen, gesture_event, LV_EVENT_GESTURE, NULL);

    back_bar_create(view.screen, item->getLabel());

    view.body = item_screen_container(view.screen);
    lv_obj_set_size(view.body, lv_pct(100),
                    lv_display_get_vertical_resolution(NULL) - ITEM_BAR_H);
    lv_obj_set_pos(view.body, 0, ITEM_BAR_H);

    dsc->build(&view);

    BEEPER_EVENT_WINDOW();

    /* Pushed with no screen animation: a whole-screen slide is 30 ms of SPI per
     * frame and would be six visible steps. The screen simply appears and its
     * contents stagger in behind the bar, which costs a fraction of that and
     * reads as more considered, not less. */
    ui_screen_push(view.screen, UI_SCREEN_ITEM, 0);

    ui_motion_enter(view.body);
}

void item_screen_close(void)
{
    if (view.screen == NULL)
        return;

    if (view.dsc != NULL && view.dsc->destroy != NULL)
        view.dsc->destroy(&view);

    /* ui_screen_pop() owns the delete -- this is reached from an event on one of
     * the screen's own descendants. */
    ui_screen_pop(0);

    memset(&view, 0, sizeof(view));

    BEEPER_EVENT_WINDOW_CLOSE();
}

void item_screen_dismiss(void)
{
    if (view.screen == NULL)
        return;

    if (view.dsc != NULL && view.dsc->destroy != NULL)
        view.dsc->destroy(&view);

    ui_screen_pop(0);

    memset(&view, 0, sizeof(view));
}

void item_screen_refresh(uint8_t slot)
{
    if (view.screen == NULL || view.dsc == NULL || view.slot != slot)
        return;

    if (view.dsc->refresh != NULL)
        view.dsc->refresh(&view);
}

bool item_screen_is_open(void)
{
    return view.screen != NULL;
}

enum ItemType item_screen_open_type(void)
{
    return (view.screen != NULL && view.item != NULL) ? view.item->getType()
                                                      : ItemType::type_unknown;
}

uint8_t item_screen_open_slot(void)
{
    return view.slot;
}
