#ifndef UI_WIDGETS_HPP
#define UI_WIDGETS_HPP

#include <lvgl.h>

/* The three widgets every screen in this UI is built out of.
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

#endif /* UI_WIDGETS_HPP */
