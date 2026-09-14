#ifndef UI_STYLE_HPP
#define UI_STYLE_HPP

#include <lvgl.h>
#include "ui_geometry.hpp"
#include "ui_motion.hpp"
#include "ui_theme.hpp"

struct ui_frame_ops_s;
struct ui_sound_s;

/* The shared styles of the OhEzTouch UI.
 *
 * These replace the hand-forked LVGL v7 theme that used to live in
 * src/themes/. A theme is machinery for styling widgets you do not control;
 * this application creates every widget it draws, and only about eight kinds of
 * them, so the styles are simply applied at the creation sites.
 *
 * LVGL's built-in "simple" theme still supplies the defaults underneath (see
 * main.cpp), but it supplies more than it looks: the screen background and text
 * colour, the item window background, the systeminfo table cells, the slider
 * indicator and the setpoint button matrix are all its light greys, none of
 * which this project used to set. A dark theme cannot leave any of them alone,
 * so those surfaces now have styles of their own here and are added at their
 * creation sites like everything else.
 *
 * A custom lv_theme was considered for that job and rejected:
 * lv_display_set_theme() only applies while the display's screens are still
 * childless, and it applies to screens[0] -- which is the bottom layer, not the
 * active screen. It could therefore neither style lv_screen_active() nor be
 * swapped once the UI exists.
 *
 * Call ui_style_select() and then ui_style_init() once, before creating any
 * widget. ui_style_apply() re-does it on a live UI. */

/* One entry per (family, night) combination, indexed by UI_STYLE_INDEX(). */
#define UI_THEME_COUNT (UI_THEME_FAMILY_COUNT * 2)
#define UI_STYLE_INDEX(family, night) ((unsigned)(family) * 2u + ((night) ? 1u : 0u))

/* The alpha byte of a 0xRRGGBB colour is unused, so it carries the sentinel:
 * UI_COLOR_KEEP means "do not set this property at all". That is how
 * ui_style_tile_pressed keeps the tile's own border, and how the Default
 * variant sets exactly the properties the v7-derived code set -- no more, no
 * less. UI_METRIC_KEEP is the same idea for the numeric properties, and
 * UI_ENUM_KEEP for the two that are enums with a meaningful zero. */
#define UI_COLOR_KEEP  0xFF000000u
#define UI_METRIC_KEEP ((int16_t)-1)
#define UI_ENUM_KEEP   ((uint8_t)0xFF)

/* Every raised or filled surface in this UI is these ten properties. Sharing
 * one descriptor keeps the theme table readable and lets a single helper apply
 * it, rather than ten lv_style_set_* calls per surface at every entry.
 *
 * Colours are 0xRRGGBB rather than lv_color_t because lv_color_make() and
 * lv_palette_main() are real functions, not inline: a table initialised with
 * them would need dynamic initialisation and would land in RAM instead of
 * flash. */
struct ui_surface_s
{
    uint32_t bg;           /* lv_style_set_bg_color        */
    uint32_t bg_grad;      /* lv_style_set_bg_grad_color   */
    uint8_t  grad_dir;     /* lv_style_set_bg_grad_dir     (lv_grad_dir_t)    */
    int16_t  bg_opa;       /* lv_style_set_bg_opa                             */
    uint32_t border;       /* lv_style_set_border_color    */
    int16_t  border_width; /* lv_style_set_border_width    */
    int16_t  border_opa;   /* lv_style_set_border_opa      */
    uint8_t  border_side;  /* lv_style_set_border_side     (lv_border_side_t) */
    int16_t  radius;       /* lv_style_set_radius; LV_RADIUS_CIRCLE fits      */
    uint32_t text;         /* lv_style_set_text_color      */
};

/* An additive border, for the two markers that are laid over ui_style_tile to
 * distinguish navigation from controllable items. */
struct ui_marker_s
{
    uint32_t color;
    int16_t  width;
    int16_t  opa;
    uint8_t  side;
};

struct ui_theme_s
{
    const char             *name; /* "LCARS Night" -- diagnostics only */
    enum ui_theme_family_e  family;
    bool                    night;

    /* One per shared surface style. */
    struct ui_surface_s screen;       /* lv_screen_active() MAIN         */
    struct ui_surface_s tile;         /* ui_style_tile                   */
    struct ui_surface_s tile_pressed; /* ui_style_tile_pressed           */
    struct ui_surface_s window;       /* ui_style_window                 */
    struct ui_surface_s header;       /* ui_style_win_header             */
    struct ui_surface_s btn;          /* ui_style_btn                    */
    struct ui_surface_s btn_checked;  /* ui_style_btn_checked            */
    struct ui_surface_s slider;       /* ui_style_slider (MAIN)          */
    struct ui_surface_s slider_indic; /* ui_style_slider_indicator       */
    struct ui_surface_s knob;         /* ui_style_slider_knob            */
    struct ui_surface_s cell;         /* ui_style_table_cell (ITEMS)     */
    struct ui_surface_s swatch;       /* ui_style_swatch                 */
    struct ui_surface_s info;         /* ui_style_info                   */

    /* The accent feeds nothing directly. ui_style_tile_pressed and
     * ui_style_btn_checked have always been this colour darkened twice over
     * rather than two colours of their own; a variant that wants that leaves
     * their bg at UI_COLOR_KEEP and gets the derivation, which is what makes
     * the Default variant identical to the old code by construction. LCARS,
     * which presses *lighter*, names its colours outright. */
    uint32_t accent;

    struct ui_marker_s link;   /* ui_style_tile_link   */
    struct ui_marker_s active; /* ui_style_tile_active */

    uint32_t info_warning_bg; /* ui_style_info_warning */
    uint32_t info_error_bg;   /* ui_style_info_error   */

    /* The openHAB icons are dark line-art PNGs fetched at runtime and drawn as
     * a watermark behind the tile's caption and state line. On a dark variant
     * they vanish, so image_recolor is the only lever short of shipping our
     * own -- but a bright recolour destroys the caption's contrast, so these
     * are chosen against the composite, not against the tile alone. */
    uint32_t icon_recolor;
    uint8_t  icon_recolor_opa; /* 0 leaves the PNG's own colours alone */
    uint8_t  icon_opa;
    uint8_t  symbol_opa;     /* the parent-link arrow  */
    uint8_t  symbol_dim_opa; /* the no-icon eye        */

    /* A shadow standing in for a glow, on the tile and the buttons. Width 0
     * disables it, so the variants that do not want one pay nothing -- which
     * matters because LV_DRAW_SW_SHADOW_CACHE_SIZE is 0 and every shadow is
     * re-blurred on every frame. */
    uint32_t glow;
    uint8_t  glow_width;
    uint8_t  glow_opa;

    /* Three roles, not one font per style, so that adding a variant cannot
     * quietly pull a fourth font into the build. The roles are also the three
     * *weights* of the family's face, not three sizes of one weight, which is
     * where the hierarchy comes from -- see tools/build_fonts.sh. */
    const lv_font_t *font_small;  /* captions, buttons, table cells */
    const lv_font_t *font_normal; /* state lines, window headers    */
    const lv_font_t *font_large;  /* the big value labels           */
    int16_t          letter_space;

    /* How this family moves. Scalars only, for the same reason the colours are
     * uint32_t rather than lv_color_t: the table has to stay in flash. */
    struct ui_motion_cfg_s motion;

    /* The chrome it wears, and where it lets the tile grid sit. A pointer to a
     * const vtable of static functions -- link-time address constants, so the
     * table is still .rodata. */
    const struct ui_frame_ops_s *frame;

    /* How the tiles pack into whatever rectangle the frame leaves. */
    struct ui_grid_s grid;

    /* What it sounds like. Per family, shared by day and night: a theme does
     * not sound different after dark. */
    const struct ui_sound_s *sound;
};

void ui_style_select(enum ui_theme_family_e family, bool night);

/* Build every shared style from the selected variant. Idempotent: it resets
 * each style first, so ui_style_apply() can call it again on a live UI. */
void ui_style_init(void);

/* Rebuild the live styles in place and repaint. Colours and fonts follow; the
 * per-family decorations do not (they are objects, not properties), so a
 * caller that switches variant has to re-run those -- see openhab_ui.cpp. */
void ui_style_apply(void);

enum ui_theme_family_e   ui_style_family(void);
bool                     ui_style_night(void);
const char              *ui_style_name(void);
const struct ui_theme_s *ui_style_theme(void);

/* Structural decoration used to live here, as a single LCARS-shaped hook, on
 * the grounds that at 320x240 six tiles leave no room for chrome around them.
 * That turned out to be a matter of how the space was spent rather than how
 * much of it there was: see frames/ui_frame.hpp, where every family builds its
 * own chrome and then says which rectangle the tile grid may have.
 *
 * The rule it was right about still holds, and is why the interface has a
 * destroy(): none of that is properties, so lv_obj_report_style_change()
 * cannot reach it and a live variant change has to tear it down and rebuild.
 */

/* Build a style selector out of a part and a state. lv_obj_add_style() takes an
 * lv_style_selector_t, but LV_PART_* and LV_STATE_* are two distinct enums, and
 * C++20 deprecates combining those with `|` directly. */
static inline lv_style_selector_t ui_style_selector(lv_part_t part, lv_state_t state)
{
    return (lv_style_selector_t)part | (lv_style_selector_t)state;
}

/* Widget tiles on the main page. `link` and `active` are additive: they are
 * applied on top of ui_style_tile to mark navigation and controllable items. */
extern lv_style_t ui_style_tile;
extern lv_style_t ui_style_tile_pressed;
extern lv_style_t ui_style_tile_link;
extern lv_style_t ui_style_tile_active;

/* Text. `label` is the tile caption, `state` the state line along the bottom
 * edge, `large` the oversized text in item windows. */
extern lv_style_t ui_style_label;
extern lv_style_t ui_style_label_state;
extern lv_style_t ui_style_label_large;

/* Item windows. */
extern lv_style_t ui_style_win_header;
extern lv_style_t ui_style_btn;
extern lv_style_t ui_style_btn_checked;
extern lv_style_t ui_style_slider;
extern lv_style_t ui_style_slider_knob;

/* The surfaces lv_theme_simple used to supply, plus the two that built private
 * styles of their own. */
extern lv_style_t ui_style_screen;           /* lv_screen_active() MAIN     */
extern lv_style_t ui_style_window;           /* the item window's own obj   */
extern lv_style_t ui_style_table_cell;       /* systeminfo table, ITEMS     */
extern lv_style_t ui_style_slider_indicator; /* slider INDICATOR            */
extern lv_style_t ui_style_icon;             /* the tile's watermark image  */
extern lv_style_t ui_style_swatch;           /* both colour swatches        */
extern lv_style_t ui_style_info;             /* the Messagebox panel        */
extern lv_style_t ui_style_info_warning;     /* additive, over ui_style_info */
extern lv_style_t ui_style_info_error;       /* additive, over ui_style_info */

#endif
