#ifndef UI_FRAME_HPP
#define UI_FRAME_HPP

#include "openhab/openhab_connector.hpp"

#include <lvgl.h>
#include <stddef.h>
#include <stdint.h>

/* The chrome around the tiles, and where the tiles are allowed to go.
 *
 * Until now the three theme families were one geometry in three palettes:
 * ui_style.hpp said so outright -- "at 320x240 six tiles leave no room for
 * chrome around them" -- and the only structural thing a family could
 * contribute was ui_style_decorate_window(), an LCARS-shaped early return
 * bolted next to the style table.
 *
 * That is what this replaces. A family builds its own chrome and then says
 * which rectangle is left for the grid, so LCARS can put a spine down the left
 * edge and an elbow across the top, Default can have no chrome objects at all,
 * and JARVIS can draw two hairlines and a telemetry strip -- without the page
 * knowing anything about any of it.
 *
 * "No frame" is a real implementation of this interface, not an early return
 * from it. content_area() is the reason the interface exists: it is what lets
 * three different chromes share one page_rebuild().
 *
 * Objects, not properties. lv_obj_report_style_change() cannot reach any of
 * this, so a live theme change has to tear the frame down and build the new
 * family's -- see openhab_ui.cpp's theme_apply_pending(). */

struct ui_frame_ops_s
{
    /* Create the chrome on `parent`. Everything made here must hang off the
     * single object returned, so destroy() is one delete. */
    void (*build)(lv_obj_t *parent);

    /* Tear it down. Called before the next family's build(). */
    void (*destroy)(void);

    /* The rectangle the tile grid may use, in `parent` coordinates. */
    lv_area_t (*content_area)(void);

    /* The page's name. May be dropped on the floor by a family that shows none. */
    void (*set_title)(const char *title);

    /* The clock, as "HH:MM" or "HH MM" -- the caller decides whether the colon
     * is blinking, because that is a matter of taste and the families differ. */
    void (*set_clock)(const char *text);

    /* Link state. `rssi` is a percentage, or -1 when there is no radio to ask
     * (a wired host); `online` is false while the station is down. */
    void (*set_link)(bool online, int rssi);

    /* Whatever this family adds to a tile that a style cannot: brackets, a
     * gauge, a spine. NULL for a family that adds nothing, so the others pay
     * no call. */
    void (*decorate_tile)(lv_obj_t *tile, enum ItemType type, uint8_t slot);
};

/* One per family. Selected through the theme table, so a family's chrome and
 * its palette cannot get out of step. */
/* The chrome every family wore until the redesign: a 33 px status row of
 * clock, title, signal and WLAN glyph across the top. Kept as a real frame so
 * that a family which has not been redesigned yet still has one, rather than
 * the page carrying a fallback of its own. */
extern const struct ui_frame_ops_s ui_frame_classic;

extern const struct ui_frame_ops_s ui_frame_default;
extern const struct ui_frame_ops_s ui_frame_lcars;
extern const struct ui_frame_ops_s ui_frame_jarvis;

/* JARVIS draws a ring on any tile whose value has a range behind it, and needs
 * the value's place in that range rather than the value. Only one family has
 * gauges, so this is a direct call rather than a seventh entry in the vtable
 * that five implementations would have to leave NULL. */
void ui_frame_jarvis_set_gauge(uint8_t slot, uint8_t percent);

/* ------------------------------------------------------ shared by the frames */

/* An unstyled, unpadded, non-scrolling container. */
lv_obj_t *ui_frame_container(lv_obj_t *parent);

/* Make `obj` open the settings screen when touched. Every family puts this on
 * whatever carries its status readout. */
void ui_frame_settings_target(lv_obj_t *obj);

/* Undo the caller's blinking colon.
 *
 * header_update() alternates "HH:MM" and "HH MM" every second and lets each
 * family decide what to do with it. A family that does not want the blink
 * cannot simply ignore it -- the two strings are not the same width, because
 * a proportional face has no reason to make its colon and its space agree, so
 * the minutes would step sideways once a second. Writes at most `size` bytes,
 * the separator restored. */
void ui_frame_clock_steady(char *dst, size_t size, const char *text);

/* A plain rect of one colour, positioned absolutely. The building block the
 * LCARS elbow and the JARVIS hairlines are made of. */
lv_obj_t *ui_frame_block(lv_obj_t *parent, int16_t x, int16_t y, int16_t w, int16_t h,
                         uint32_t color, int16_t radius);

#endif /* UI_FRAME_HPP */
