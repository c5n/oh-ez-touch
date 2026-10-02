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
#include "ui/ui_widgets.hpp"

#include <stdio.h>
#include <string.h>

/* One at a time. A second open would strand the first with no way back to it,
 * so item_screen_open() closes what is up rather than stacking. */
static struct item_view_s view;

static item_screen_changed_cb_t changed_cb;
static item_screen_page_cb_t    page_cb;

/* The table that used to be a switch in openhab_ui.cpp's event_handler().
 * Function pointers to statics are link-time constants, so this is .rodata --
 * the same reason the theme table can hold font pointers. */
static const struct item_screen_dsc_s *const registry[] = {
    &item_screen_slider,   &item_screen_setpoint, &item_screen_selection,
    &item_screen_rollershutter, &item_screen_player, &item_screen_color,
    &item_screen_image,
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

void item_screen_set_page_cb(item_screen_page_cb_t cb)
{
    page_cb = cb;
}

Item *item_screen_page_item(uint8_t slot)
{
    return (page_cb != NULL) ? page_cb(slot) : NULL;
}

/* ------------------------------------------------------- builder utilities */

/* `text` to `item`'s link, and its tile marked for a poll. */
static void send(Item *item, uint8_t slot, const char *text)
{

    /* Fire and forget, as it has always effectively been: the tile's own state
     * is already set locally, and the state openHAB sends back -- pushed, or the
 * poll the tile schedules for five seconds later -- is what reconciles it.
     *
     * Forgotten, but not unsaid. A full request queue is reachable on a slow
     * link -- six tiles with an icon and a state outstanding is exactly its
     * depth -- and a command dropped there looks from the glass like one that
     * was sent: the control has already moved, and the next poll quietly puts
     * it back. The same line item_publish() prints for a tap on a tile. */
    if (openhab_client_command(item->getLink(), text) == false)
        printf("item_screen: command queue full; \"%s\" not sent to %s\r\n",
               text, item->getLink());

    if (changed_cb != NULL)
        changed_cb(slot);
}

void item_screen_publish_quiet(struct item_view_s *v)
{
    if (v == NULL || v->item == NULL)
        return;

    send(v->item, v->slot, v->item->getStateText());
}

void item_screen_publish(struct item_view_s *v)
{
    item_screen_publish_quiet(v);

    if (v != NULL && v->item != NULL)
        BEEPER_EVENT_CHANGE();
}

void item_screen_send_quiet(struct item_view_s *v, const char *command)
{
    if (v == NULL || v->item == NULL || command == NULL)
        return;

    send(v->item, v->slot, command);
}

void item_screen_send(struct item_view_s *v, const char *command)
{
    if (v == NULL || v->item == NULL || command == NULL)
        return;

    send(v->item, v->slot, command);
    BEEPER_EVENT_CHANGE();
}

void item_screen_send_to(Item *item, uint8_t slot, const char *command)
{
    if (item == NULL || command == NULL)
        return;

    send(item, slot, command);
    BEEPER_EVENT_CHANGE();
}

lv_obj_t *item_screen_container(lv_obj_t *parent)
{
    /* The shared one, under the name the builders in this directory use. */
    return ui_plain_container(parent);
}

lv_obj_t *item_screen_button(lv_obj_t *parent, const char *text)
{
    lv_obj_t *btn = ui_themed_button(parent, text);

    /* The one thing these want that a settings button does not: the caption
     * face on the label, which item_screen_glyph() then replaces with the
     * large one on the controls whose whole content is a symbol. */
    lv_obj_add_style(lv_obj_get_child(btn, 0), &ui_style_label, LV_PART_MAIN);

    return btn;
}

void item_screen_glyph(lv_obj_t *btn)
{
    lv_obj_t *label = lv_obj_get_child(btn, 0);

    if (label != NULL)
        lv_obj_set_style_text_font(label, ui_style_theme()->font_large, 0);
}

/* Tall enough to hit without looking, and with nothing to aim at: the knob is
 * the full height of the track, so pressing anywhere in the bar takes the
 * value there and dragging moves it.
 *
 * A thin marker at the fill edge rather than a grab handle, because there is
 * nothing to grab. It has to be done with negative padding. lv_slider takes
 * the knob's size from the track -- knob_size = lv_obj_get_height(obj) for a
 * horizontal one -- and then adds the four pads; LV_STYLE_WIDTH on
 * LV_PART_KNOB is never read. So a 56 px track gives a 56 px square block
 * unless 25 px is taken off each side. */
#define FIELD_KNOB_W 6

lv_obj_t *item_screen_field(lv_obj_t *parent, int32_t height)
{
    lv_obj_t *field = lv_slider_create(parent);

    lv_obj_add_style(field, &ui_style_slider, LV_PART_MAIN);
    lv_obj_add_style(field, &ui_style_slider_indicator, LV_PART_INDICATOR);
    lv_obj_add_style(field, &ui_style_slider_knob, LV_PART_KNOB);
    lv_obj_set_width(field, lv_pct(100));
    lv_obj_set_height(field, height);
    lv_obj_set_style_pad_all(field, 0, LV_PART_KNOB);
    lv_obj_set_style_pad_hor(field, -(height - FIELD_KNOB_W) / 2, LV_PART_KNOB);
    lv_obj_set_style_radius(field, 6, LV_PART_MAIN);
    lv_obj_set_style_radius(field, 6, LV_PART_INDICATOR);

    /* The contact tick without the plate deformation: a field is dragged
     * rather than pressed, so ui_motion_pressable()'s visual half would be
     * wrong on it while the acknowledgement is still right. */
    ui_beep_attach_press(field);

    return field;
}

void item_screen_set_pattern(lv_obj_t *label, Item *item, float value)
{
    ui_reading_set_pattern(label, item->getNumberPattern(), value);
}

/* ------------------------------------------------------------------ the frame */

static void back_event(lv_event_t *e)
{
    LV_UNUSED(e);
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

    /* No swipe-to-go-back. This was the firmware's only LV_EVENT_GESTURE
     * handler, and taking it away is half of the panel's no-swipes policy --
     * ui_input.c is the other half and explains both. The back bar below was
     * always the reliable way out: these panels are resistive, where a swipe
     * was never dependable, and the same gesture meant three different things
     * in the three themes. */

    /* Always a chevron: there is always a page under an item screen. */
    ui_back_bar(view.screen, LV_SYMBOL_LEFT, item->getLabel(), ITEM_BAR_H, back_event);

    view.body = item_screen_container(view.screen);
    lv_obj_set_size(view.body, lv_pct(100),
                    lv_display_get_vertical_resolution(NULL) - ITEM_BAR_H);
    lv_obj_set_pos(view.body, 0, ITEM_BAR_H);

    dsc->build(&view);

    BEEPER_EVENT_SCREEN();

    /* Pushed with no screen animation: a whole-screen slide is 30 ms of SPI per
     * frame and would be six visible steps. The screen simply appears and its
     * contents stagger in behind the bar, which costs a fraction of that and
     * reads as more considered, not less. */
    ui_screen_push(view.screen, UI_SCREEN_ITEM, 0);

    ui_motion_enter(view.body);
}

/* Take the open screen down. The two callers differ only in whether this is
 * something the user did, and so in whether it makes a sound.
 *
 * ui_screen_pop() owns the delete -- close() is reached from an event on one
 * of the screen's own descendants, which has to outlive its handler. */
static void view_teardown(void)
{
    if (view.screen == NULL)
        return;

    if (view.dsc != NULL && view.dsc->destroy != NULL)
        view.dsc->destroy(&view);

    ui_screen_pop(0);

    memset(&view, 0, sizeof(view));
}

void item_screen_close(void)
{
    if (view.screen == NULL)
        return;

    view_teardown();

    BEEPER_EVENT_SCREEN_OUT();
}

void item_screen_dismiss(void)
{
    /* No sound: a theme change is not a gesture, and the screen is about to
     * be built again from nothing. */
    view_teardown();
}

void item_screen_refresh(uint8_t slot)
{
    if (view.screen == NULL || view.dsc == NULL)
        return;

    if (view.slot != slot && view.dsc->follows_page == false)
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
