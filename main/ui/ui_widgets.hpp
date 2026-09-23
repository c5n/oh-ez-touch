#ifndef UI_WIDGETS_HPP
#define UI_WIDGETS_HPP

#include <lvgl.h>

/* The widgets every screen in this UI is built out of.
 *
 * Each of them existed once per screen family before this header: the plain
 * container in four copies (the openHAB page, the settings screen, the item
 * screens and the frames), the themed button and the back bar in two each.
 * They were copies rather than variants -- the settings screen's back bar and
 * an item screen's differed only in which glyph sits at the left end -- so a
 * change to how a button acknowledges a press had to be made in two places
 * and was, twice, made in one.
 *
 * Nothing here knows what it is being used for. These take a parent and
 * return an object; every caller still owns its own layout, sizing and
 * events, which is the part that genuinely differs between screens.
 */

/* The bar across the top of a pushed screen, and its height.
 *
 * 56 px is about 8.5 mm on the 2.4" panel. It is edge-anchored and full
 * width, which is what makes it the easiest thing on the screen to hit --
 * the opposite of the glyph-sized close button the item windows used to
 * have. */
#define UI_BAR_H 56

/* An unstyled, unpadded, non-scrolling container.
 *
 * v9's lv_obj_create() arrives with the theme's background, border, radius,
 * padding and scrolling, none of which a layout container wants. */
lv_obj_t *ui_plain_container(lv_obj_t *parent);

/* A themed button carrying `text`, content-sized until the caller says
 * otherwise.
 *
 * The pressed state is styled as well as the checked one, and deliberately
 * with the same surface: without it a button's only feedback is the plate
 * deformation from ui_style_press_active, and only Material asks for one --
 * LCARS and JARVIS both set press_grow to 0 because they acknowledge a press
 * by changing colour. On those two an unstyled button acknowledged nothing.
 *
 * The label is child 0, centred, so a caller that wants to re-align or
 * re-face it can reach it with lv_obj_get_child(btn, 0). */
lv_obj_t *ui_themed_button(lv_obj_t *parent, const char *text);

/* A back bar: `symbol` at the left, `title` beside it, and the whole bar is
 * the target.
 *
 * `symbol` is the caller's because it is the one thing the two users disagree
 * on -- an item screen always goes back, the settings screen closes when it
 * is already at its root. Both labels have their click flag removed, so a tap
 * anywhere on the bar reaches `cb` rather than being eaten by whichever child
 * it landed on. */
lv_obj_t *ui_back_bar(lv_obj_t *parent, const char *symbol, const char *title,
                      lv_event_cb_t cb);

/* A reading: a value and its unit, the unit a face smaller.
 *
 * "3.5 °C" used to be one label in one face, which made the unit as loud as
 * the number it annotates. The number is what somebody standing in front of
 * the panel is after; the unit only has to be there. So the two are separate
 * labels now -- `value_style` on the value, `unit_style` on whatever follows
 * the pattern's conversion -- centred as a pair, level at the bottom of the
 * value's line.
 *
 * The pair is re-centred whenever the text or the box's width changes. A
 * value too wide for the box is dotted, as it has always been; the unit keeps
 * its place.
 *
 * Callers talk to the widget through the two setters below; the return value
 * is the opaque object they size, align and move. */
lv_obj_t *ui_reading_create(lv_obj_t *parent, lv_style_t *value_style,
                            lv_style_t *unit_style, int32_t height);

/* A reading without a unit: mappings, strings and player states. */
void ui_reading_set_text(lv_obj_t *reading, const char *text);

/* Format `value` with the item's openHAB pattern. Everything up to and
 * including the conversion is the value; the rest is the unit. */
void ui_reading_set_pattern(lv_obj_t *reading, const char *pattern, float value);

#endif /* UI_WIDGETS_HPP */
