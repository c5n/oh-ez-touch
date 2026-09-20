/**
 * @file linux/port_indev.c
 *
 * The simulator's pointer: the mouse, from LVGL's own SDL driver. Clicks act as
 * touches, which is as close to a finger as a desktop gets.
 */
#include "port_indev.h"

#include "drivers/sdl/lv_sdl_mouse.h"
#include "ui/ui_input.h"

void port_indev_init(lv_display_t *disp, bool portrait)
{
    /* lv_sdl_mouse_create() binds itself to the default display, which is the
     * one port_display_init() just created; the arguments are here for the
     * device, where the touch panel has to be told which display it is on and
     * which way up it is. SDL's mouse already reports window coordinates. */
    (void)disp;
    (void)portrait;

    ui_input_disable_swipes(lv_sdl_mouse_create());
}
