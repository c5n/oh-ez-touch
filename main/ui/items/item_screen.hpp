#ifndef ITEM_SCREEN_HPP
#define ITEM_SCREEN_HPP

#include "openhab/openhab_connector.hpp"
#include "ui/ui_widgets.hpp"

#include <lvgl.h>
#include <stdint.h>

/* The screens behind the tiles.
 *
 * Operating an item used to open a "window": a full-screen object parented to
 * the page, with a coloured header bar and a close button the size of a glyph.
 * It was a window in every respect except being one -- LV_USE_WIN is off, so it
 * was hand-rolled -- and it was laid out for a mouse. The close button was about
 * 30 px on a 143-167 dpi panel, which is four and a half millimetres of glass.
 *
 * These are real screens now, pushed onto ui_screen's stack, one source file per
 * item type, and laid out for a finger: nothing you are meant to hit is under
 * 44 px on its short axis, and the way back is a bar across the whole top edge
 * rather than a glyph in the corner.
 *
 * One at a time, which is what the single `open_window` slot in openhab_ui.cpp
 * always enforced in practice. */

/* Everything a live item screen keeps. The `state_window_*` members that used
 * to hang off widget_context_s live here instead, which is what lets the page
 * stop knowing anything about controls it does not draw. */
struct item_view_s
{
    Item     *item;  /* borrowed -- the Sitemap owns it */
    uint8_t   slot;  /* the tile it came from */

    lv_obj_t *screen;
    lv_obj_t *body;  /* the area below the back bar; a builder fills this */
    lv_obj_t *value; /* the dominant readout every screen has */

    /* Per-type widgets a refresh has to reach. Named by the builder that set
     * them; a union would save a few bytes and cost the clarity. */
    lv_obj_t *control;
    lv_obj_t *extra[3];

    const struct item_screen_dsc_s *dsc;
};

/* One per item type. `refresh` is what makes an open screen follow the server;
 * the old windows never did, so a dimmer changed from a phone left the panel
 * showing a stale number until it was closed and reopened. */
struct item_screen_dsc_s
{
    enum ItemType type;
    void (*build)(struct item_view_s *v);
    void (*refresh)(struct item_view_s *v);
    void (*destroy)(struct item_view_s *v); /* may be NULL */
};

/* NULL for a type that has no screen -- which is the dispatcher's "nothing to
 * open here" answer, replacing the switch's default arm. */
const struct item_screen_dsc_s *item_screen_find(enum ItemType type);

void item_screen_open(Item *item, uint8_t slot);
void item_screen_close(void);   /* animated, as a back gesture */
void item_screen_dismiss(void); /* instant -- for a theme change */

/* No-op unless that slot's screen is the one that is up. */
void item_screen_refresh(uint8_t slot);

bool          item_screen_is_open(void);
enum ItemType item_screen_open_type(void);
uint8_t       item_screen_open_slot(void);

/* The page tells us how to say "this item moved, re-poll it". Keeps
 * widget_context_s private to the page while still letting a control here mark
 * its tile for refresh. */
typedef void (*item_screen_changed_cb_t)(uint8_t slot);
void item_screen_set_changed_cb(item_screen_changed_cb_t cb);

/* ---------------------------------------------------------- for the builders */

/* Send the item's current local state to openHAB and mark the tile. Every
 * builder ends a gesture with this. */
void item_screen_publish(struct item_view_s *v);

/* The same, without the chime.
 *
 * For a gesture that repeats and speaks for itself while it does -- holding
 * the setpoint's plus, which publishes about ten times a second. That used to
 * go through item_screen_publish() and beep on every one of them. The release
 * still ends on a proper item_screen_publish(), so a hold reads as a run of
 * ticks and then a commit. */
void item_screen_publish_quiet(struct item_view_s *v);

/* An unstyled, unpadded, non-scrolling container. */
lv_obj_t *item_screen_container(lv_obj_t *parent);

/* A themed, finger-sized button carrying `text`. */
lv_obj_t *item_screen_button(lv_obj_t *parent, const char *text);

/* Apply the item's openHAB display pattern to a numeric value. */
void item_screen_set_pattern(lv_obj_t *label, Item *item, float value);

/* Set a button's label in the large face.
 *
 * For a control whose entire content is one glyph, which is most of them here.
 * This used to have to settle for the 22 px face: the 36 px fonts were built
 * with FA_LARGE at two codepoints, so every other LV_SYMBOL_* was a
 * placeholder box at that size. FA_LARGE now carries the twelve a control can
 * be, so a plus sign on a 106 px pad is finally the size of the pad. */
void item_screen_glyph(lv_obj_t *btn);

/* The minimum anything may be on its short axis: 44 px is about 6.7 mm on the
 * 2.4" panel and 7.8 mm on the 2.8". A fingertip is nearer 9. */
#define ITEM_TAP_MIN 44

/* The back bar across the top of every item screen. The settings screen wears
 * the same one; ui_widgets.hpp is where it and its height live. */
#define ITEM_BAR_H UI_BAR_H

/* What is left underneath it, which is all any builder gets: the screen's
 * height minus the bar. Worth stating as an expression, because in landscape
 * it is small enough that every layout below has to be checked against it
 * rather than assumed to fit. */
#define ITEM_BODY_H (LV_VER_RES - ITEM_BAR_H)

extern const struct item_screen_dsc_s item_screen_slider;
extern const struct item_screen_dsc_s item_screen_setpoint;
extern const struct item_screen_dsc_s item_screen_selection;
extern const struct item_screen_dsc_s item_screen_rollershutter;
extern const struct item_screen_dsc_s item_screen_player;
extern const struct item_screen_dsc_s item_screen_color;

#endif /* ITEM_SCREEN_HPP */
