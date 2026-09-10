/**
 * @file ui_screen.cpp
 *
 * The root screen, the one that can cover it, and the way between them.
 */
#include "ui_screen.hpp"

#include "debug.h"
#include "ui_style.hpp"

static lv_obj_t           *root = NULL;
static lv_obj_t           *pushed = NULL;
static enum ui_screen_id_e pushed_id = UI_SCREEN_NONE;

/* Hide the transient banners while something is pushed over the page.
 *
 * Moved here from ui_settings.cpp, because the reason applies to every pushed
 * screen and not only that one: a "WLAN NOT CONNECTED" warning is created with
 * no timeout and never expires, so without this it would sit on top of an item
 * control screen as readily as on top of the settings tabs.
 *
 * The children get the flag rather than the layer itself. lv_obj_remove_flag()
 * reacts to LV_OBJ_FLAG_HIDDEN by marking the object's *parent* layout dirty,
 * with no NULL check, and a layer has no parent -- hiding the layer works only
 * by luck and unhiding it segfaults. A banner is a plain child of the layer and
 * has a parent, so the flag behaves on it. */
static void banners_hide(bool hidden)
{
    lv_obj_t *top = lv_layer_top();
    uint32_t  count = lv_obj_get_child_count(top);

    for (uint32_t i = 0; i < count; i++)
        lv_obj_set_flag(lv_obj_get_child(top, i), LV_OBJ_FLAG_HIDDEN, hidden);
}

/* The pushed screen is gone for good once it leaves the display, and this is
 * the *only* place it is deleted.
 *
 * Deleting it here rather than at the pop call site is what makes an animated
 * pop safe: the pop is reached from an event on one of the screen's own
 * descendants, and this fires long after that handler has returned. Having it
 * be the only place is what stops the instant pop deleting the same object
 * twice -- lv_screen_load() unloads the screen synchronously, so an explicit
 * delete next to it queued a second lv_obj_delete_async() on an object this
 * had already queued one for. */
static void unloaded_event(lv_event_t *e)
{
    lv_obj_t *screen = (lv_obj_t *)lv_event_get_target(e);

    if (screen == pushed)
        return; /* still the current one: this was a settle, not a pop */

    lv_obj_delete_async(screen);
}

void ui_screen_setup(void)
{
    /* lv_screen_active() is already valid here -- port_display_init() gives the
     * display a screen of its own -- so the root is that one rather than a new
     * object. Creating one would leave the display's original screen orphaned
     * and still holding the styles applied to it. */
    root = lv_screen_active();

    lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(root, 0, 0);

    /* ui_style_init() used to add ui_style_screen to lv_screen_active() behind a
     * one-shot flag, so that re-applying a theme did not stack it. Every screen
     * in the UI is now made in one of exactly two places -- here and
     * ui_screen_create() -- and each adds the style once to an object it has
     * just made, so the flag is gone and with it the assumption that
     * lv_screen_active() is the right object to style. */
    lv_obj_add_style(root, &ui_style_screen, LV_PART_MAIN);
}

lv_obj_t *ui_screen_root(void)
{
    return root;
}

lv_obj_t *ui_screen_create(void)
{
    lv_obj_t *screen = lv_obj_create(NULL);

    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_style(screen, &ui_style_screen, LV_PART_MAIN);
    lv_obj_set_style_pad_all(screen, 0, 0);
    lv_obj_set_style_pad_gap(screen, 0, 0);

    return screen;
}

void ui_screen_push(lv_obj_t *screen, enum ui_screen_id_e id, uint32_t anim_ms)
{
    if (screen == NULL || root == NULL)
        return;

    /* Only one thing may cover the page. A caller that wants to replace what is
     * up has to pop it first; silently stacking would strand the screen
     * underneath with no way back to it. */
    if (pushed != NULL)
        ui_screen_pop(0);

    pushed = screen;
    pushed_id = id;

    lv_obj_add_event_cb(screen, unloaded_event, LV_EVENT_SCREEN_UNLOADED, NULL);

    banners_hide(true);

#if CONFIG_OHEZ_DEBUG_UI_SCREEN
    printf("ui_screen: push id=%u anim=%ums\r\n", (unsigned)id, (unsigned)anim_ms);
#endif

    /* auto_del is false and must stay false: it deletes the screen being loaded
     * *away from*, which here is the root. */
    if (anim_ms == 0)
        lv_screen_load(screen);
    else
        lv_screen_load_anim(screen, LV_SCREEN_LOAD_ANIM_OVER_LEFT, anim_ms, 0, false);
}

void ui_screen_pop(uint32_t anim_ms)
{
    if (pushed == NULL || root == NULL)
        return;

    lv_obj_t *going = pushed;

    pushed = NULL;
    pushed_id = UI_SCREEN_NONE;

    banners_hide(false);

#if CONFIG_OHEZ_DEBUG_UI_SCREEN
    printf("ui_screen: pop anim=%ums\r\n", (unsigned)anim_ms);
#endif

    /* Either way unloaded_event() does the deleting -- see its comment. Not
     * lv_screen_load_anim(..., 0, 0, true) for the instant case: with a zero
     * duration that deletes synchronously, and every caller here is inside an
     * event on one of `going`'s own descendants. */
    if (anim_ms == 0)
        lv_screen_load(root);
    else
        lv_screen_load_anim(root, LV_SCREEN_LOAD_ANIM_OVER_RIGHT, anim_ms, 0, false);

    LV_UNUSED(going);
}

void ui_screen_settle(void)
{
    lv_obj_t *target = (pushed != NULL) ? pushed : root;

    if (target == NULL)
        return;

    /* Loading the screen that is already being loaded to, with no animation,
     * makes LVGL finish the pending transition immediately. Cheap and idempotent
     * when nothing is in flight. */
    lv_screen_load(target);
}

enum ui_screen_id_e ui_screen_top(void)
{
    return pushed_id;
}

void ui_screen_loop(void)
{
    if (pushed != NULL)
        banners_hide(true);
}
