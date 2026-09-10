#include "ui_style.hpp"

#include "frames/ui_frame.hpp"

#include "debug.h"

lv_style_t ui_style_tile;
lv_style_t ui_style_tile_pressed;
lv_style_t ui_style_tile_link;
lv_style_t ui_style_tile_active;
lv_style_t ui_style_label;
lv_style_t ui_style_label_state;
lv_style_t ui_style_label_large;
lv_style_t ui_style_win_header;
lv_style_t ui_style_btn;
lv_style_t ui_style_btn_checked;
lv_style_t ui_style_slider;
lv_style_t ui_style_slider_knob;
lv_style_t ui_style_screen;
lv_style_t ui_style_window;
lv_style_t ui_style_table_cell;
lv_style_t ui_style_slider_indicator;
lv_style_t ui_style_icon;
lv_style_t ui_style_swatch;
lv_style_t ui_style_info;
lv_style_t ui_style_info_warning;
lv_style_t ui_style_info_error;

/* The theme derived most of its metrics from LV_DPI. LV_DPI_DEF is pinned to
 * the v7 value of 100 in lv_conf.h so these keep producing the same geometry.
 * Metrics that no variant wants to change stay here rather than in the table:
 * they are geometry, not theme. */
#define BORDER_THIN  (LV_DPI_DEF / 50 >= 1 ? LV_DPI_DEF / 50 : 1)
#define RADIUS_TILE  (LV_DPI_DEF / 15)
/* Slate's cards. Generous on purpose: at 96x93 an 18 px radius is what makes
 * the shape read as a sheet resting on the ground rather than as a button. */
#define RADIUS_CARD  18
#define RADIUS_PANEL (LV_DPI_DEF / 20)
#define RADIUS_LCARS (LV_DPI_DEF / 6)
#define PAD_TILE     (LV_DPI_DEF / 20)
#define PAD_BTN      (LV_DPI_DEF / 12)
#define PAD_INFO     (LV_DPI_DEF / 10)

/* The LCARS variants are the only reason a second font family is compiled in.
 * Antonio is a tall condensed face; see tools/build_fonts.sh. */
#define FONT_UI_SMALL     (&custom_font_ui_16)     /* Barlow Regular   */
#define FONT_UI_NORMAL    (&custom_font_ui_22)     /* Barlow Medium    */
#define FONT_UI_LARGE     (&custom_font_ui_36)     /* Barlow SemiBold  */
#define FONT_HUD_SMALL    (&custom_font_hud_16)    /* Rajdhani Regular */
#define FONT_HUD_NORMAL   (&custom_font_hud_22)    /* Rajdhani Medium  */
#define FONT_HUD_LARGE    (&custom_font_hud_36)    /* Rajdhani SemiBold*/
#define FONT_LCARS_SMALL  (&custom_font_lcars_16)  /* Antonio          */
#define FONT_LCARS_NORMAL (&custom_font_lcars_22)
#define FONT_LCARS_LARGE  (&custom_font_lcars_36)

/* Shorthand, so that a table row fits on a line and the six variants can be
 * read against each other. Undefined again below the table. */
#define CK    UI_COLOR_KEEP
#define MK    UI_METRIC_KEEP
#define EK    UI_ENUM_KEEP
#define VER   LV_GRAD_DIR_VER
#define NON   LV_GRAD_DIR_NONE
#define LEFT  LV_BORDER_SIDE_LEFT
#define BOT   LV_BORDER_SIDE_BOTTOM
#define CIRC  LV_RADIUS_CIRCLE
#define OP30  LV_OPA_30
#define OP70  LV_OPA_70
#define FULLO LV_OPA_COVER

/*        bg        grad      dir  bg_opa  border    bw  bopa   side  radius        text     */
/* entry, ease, duration, screen, stagger, dist,
 * press_ease, press_in, press_out, press_hold, press_grow */
#define MOTION(e, ea, d, sc, st, di, pe, pi, po, ph, pg)                       \
    {                                                                          \
        (uint8_t)(e), (uint8_t)(ea), (uint16_t)(d), (uint16_t)(sc),            \
            (uint8_t)(st), (int8_t)(di), (uint8_t)(pe), (uint16_t)(pi),        \
            (uint16_t)(po), (uint8_t)(ph), (int8_t)(pg)                        \
    }

/* frame ops, then cols, rows, gutter, margin */
#define FRAME(f, c, r, g, m)                                                   \
    &(f), { (uint8_t)(c), (uint8_t)(r), (uint8_t)(g), (uint8_t)(m) }

#define SURF(bg, grad, dir, bgopa, bd, bw, bo, sd, rad, txt)                       \
    {                                                                              \
        (bg), (grad), (uint8_t)(dir), (int16_t)(bgopa), (bd), (int16_t)(bw),        \
            (int16_t)(bo), (uint8_t)(sd), (int16_t)(rad), (txt)                    \
    }

#define MARK(color, width, opa, side)                                              \
    {                                                                              \
        (color), (int16_t)(width), (int16_t)(opa), (uint8_t)(side)                  \
    }

/* The six variants.
 *
 * Indexed by UI_STYLE_INDEX(family, night), i.e. day and night of a family are
 * adjacent. They are separate entries rather than a base plus a mechanical
 * darkening, because night is not a uniform darkening: text and background
 * swap roles, the tile gradient reverses its sense of depth, the icon
 * watermark goes from untouched to fully recoloured, and JARVIS changes hue
 * outright. Every field would need an exception, and the exceptions would be
 * the table again.
 *
 * UI_THEME_DEFAULT's day entry must reproduce the look this project had before
 * it was themeable, which is why it also carries the five greys lv_theme_simple
 * used to supply underneath (0xF5F5F5 screen, 0x616161 screen text, 0xFFFFFF
 * window, 0xE0E0E0 table cell, 0x9E9E9E slider indicator). Those are
 * lv_palette_lighten(GREY, 4) / darken(GREY, 2) / white / lighten(GREY, 2) /
 * main(GREY) -- read out of the theme, not guessed. */
static const struct ui_theme_s ui_themes[UI_THEME_COUNT] = {

    /* ---------------------------------------------------- Default -- "Slate"
     * Warm paper, flat white cards, and exactly one saturated colour on the
     * screen -- a deep teal that means "this is on". The blue-on-silver the UI
     * was born with had a gradient on every surface and a marker in four
     * different places; what carries state here is a 4 px shelf along the
     * bottom edge of the card, which is a border side rather than an object.
     *
     * The link and active markers keep their names but stop being all-round
     * borders: LV_BORDER_SIDE_BOTTOM is the shelf, and it follows the 18 px
     * radius round as a stroked arc, which is what makes the card look like it
     * is resting on it. */
    {
        UI_THEME_NAME_DEFAULT " Day", UI_THEME_DEFAULT, false,
        /* screen       */ SURF(0xF2F0EC, CK, NON, FULLO, CK, MK, MK, EK, MK, 0x6A6E76),
        /* tile         */ SURF(0xFFFFFF, CK, NON, FULLO, 0xE2DED7, 1, FULLO, EK, RADIUS_CARD, 0x1B1D21),
        /* tile_pressed */ SURF(0xE8E4DC, CK, NON, MK, CK, MK, MK, EK, MK, 0x1B1D21),
        /* window       */ SURF(0xFFFFFF, CK, EK, FULLO, CK, 0, MK, EK, 0, CK),
        /* header       */ SURF(0x0B7A75, CK, NON, FULLO, CK, 0, MK, EK, 0, 0xFFFFFF),
        /* btn          */ SURF(0xFFFFFF, CK, NON, FULLO, 0xE2DED7, 1, FULLO, EK, RADIUS_PANEL, 0x1B1D21),
        /* btn_checked  */ SURF(0x0B7A75, CK, NON, MK, CK, MK, MK, EK, MK, 0xFFFFFF),
        /* slider       */ SURF(0xE8E4DC, CK, NON, FULLO, 0xD5D0C6, 1, FULLO, EK, RADIUS_PANEL, CK),
        /* slider_indic */ SURF(0x0B7A75, CK, EK, FULLO, CK, MK, MK, EK, MK, CK),
        /* knob         */ SURF(0x1B1D21, CK, NON, FULLO, CK, 0, MK, EK, 2, CK),
        /* cell         */ SURF(0xFFFFFF, CK, EK, FULLO, 0xE2DED7, 1, MK, EK, MK, CK),
        /* swatch       */ SURF(CK, CK, EK, FULLO, 0xC9C4BB, 1, MK, EK, RADIUS_PANEL, CK),
        /* info         */ SURF(0xFFFFFF, CK, EK, FULLO, 0x0B7A75, 3, MK, EK, RADIUS_PANEL, 0x1B1D21),
        /* accent       */ 0x0B7A75,
        /* link         */ MARK(0x4A4E57, 4, FULLO, BOT),
        /* active       */ MARK(0x0B7A75, 4, FULLO, BOT),
        /* info warn/err*/ 0xB4531F, 0xB3261E,
        /* icon         */ 0x000000, 0, 22, 140, 50,
        /* glow         */ 0x000000, 0, 0,
        /* fonts        */ FONT_UI_SMALL, FONT_UI_NORMAL, FONT_UI_LARGE, 0,
    /* motion       */ MOTION(UI_ENTRY_RISE, UI_EASE_OUT_CUBIC, 192, 240, 64, 10,
                              UI_EASE_OUT_CUBIC, 96, 160, 32, -3),
    /* frame        */ FRAME(ui_frame_default, 3, 2, 8, 8),
    },
    {
        UI_THEME_NAME_DEFAULT " Night", UI_THEME_DEFAULT, true,
        /* screen       */ SURF(0x14161A, CK, NON, FULLO, CK, MK, MK, EK, MK, 0x8A9099),
        /* tile         */ SURF(0x1E2126, CK, NON, FULLO, 0x2C3037, 1, FULLO, EK, RADIUS_CARD, 0xE8E4DC),
        /* tile_pressed */ SURF(0x262A31, CK, NON, MK, CK, MK, MK, EK, MK, 0xFFFFFF),
        /* window       */ SURF(0x14161A, CK, EK, FULLO, CK, 0, MK, EK, 0, CK),
        /* header       */ SURF(0x2A6B64, CK, NON, FULLO, CK, 0, MK, EK, 0, 0xE8E4DC),
        /* btn          */ SURF(0x1E2126, CK, NON, FULLO, 0x2C3037, 1, FULLO, EK, RADIUS_PANEL, 0xE8E4DC),
        /* btn_checked  */ SURF(0x2A6B64, CK, NON, MK, CK, MK, MK, EK, MK, 0xFFFFFF),
        /* slider       */ SURF(0x11141A, CK, NON, FULLO, 0x2C3037, 1, FULLO, EK, RADIUS_PANEL, CK),
        /* slider_indic */ SURF(0x48C0B0, CK, EK, FULLO, CK, MK, MK, EK, MK, CK),
        /* knob         */ SURF(0xE8E4DC, CK, NON, FULLO, CK, 0, MK, EK, 2, CK),
        /* cell         */ SURF(0x1E2126, CK, EK, FULLO, 0x2C3037, 1, MK, EK, MK, CK),
        /* swatch       */ SURF(CK, CK, EK, FULLO, 0x383D45, 1, MK, EK, RADIUS_PANEL, CK),
        /* info         */ SURF(0x1E2126, CK, EK, FULLO, 0x48C0B0, 3, MK, EK, RADIUS_PANEL, 0xE8E4DC),
        /* accent       */ 0x48C0B0,
        /* link         */ MARK(0x5A616B, 4, FULLO, BOT),
        /* active       */ MARK(0x48C0B0, 4, FULLO, BOT),
        /* info warn/err*/ 0xC4832E, 0xD9584C,
        /* icon         */ 0xE8E4DC, 255, 30, 150, 55,
        /* glow         */ 0x000000, 0, 0,
        /* fonts        */ FONT_UI_SMALL, FONT_UI_NORMAL, FONT_UI_LARGE, 0,
        /* motion       */ MOTION(UI_ENTRY_RISE, UI_EASE_OUT_CUBIC, 192, 240, 64, 10,
                              UI_EASE_OUT_CUBIC, 96, 160, 32, -3),
    /* frame        */ FRAME(ui_frame_default, 3, 2, 8, 8),
    },

    /* ------------------------------------------------------------------ LCARS
     * Flat blocks of colour on black, generously rounded, in a tall condensed
     * face. Real LCARS elbows are not drawable: LVGL v9 has one uniform radius
     * property, not four, and this build has neither arcs nor a canvas. What
     * carries the look instead is the palette, the radius, and the left spine
     * that lv_style_set_border_side() puts on navigation and control tiles.
     *
     * The tiles are nearly square (104 x 101), so LV_RADIUS_CIRCLE would round
     * their corners away entirely and the caption would spill outside the
     * shape; they get RADIUS_LCARS. The window header and the buttons are wide
     * and short, so those do become true stadiums. */
    {
        UI_THEME_NAME_LCARS " Day", UI_THEME_LCARS, false,
        /* screen       */ SURF(0x000000, CK, NON, FULLO, CK, MK, MK, EK, MK, 0xFF9900),
        /* tile         */ SURF(0xCC99CC, CK, NON, FULLO, CK, 0, MK, EK, RADIUS_LCARS, 0x000000),
        /* tile_pressed */ SURF(0xFFCC66, CK, NON, MK, CK, MK, MK, EK, MK, 0x000000),
        /* window       */ SURF(0x000000, CK, EK, FULLO, CK, 0, MK, EK, 0, CK),
        /* header       */ SURF(0xFF9900, CK, NON, FULLO, CK, 0, MK, EK, CIRC, 0x000000),
        /* btn          */ SURF(0xCC6666, CK, NON, FULLO, CK, 0, MK, EK, CIRC, 0x000000),
        /* btn_checked  */ SURF(0xFFCC66, CK, NON, MK, CK, MK, MK, EK, MK, 0x000000),
        /* slider       */ SURF(0x2A2A2A, CK, NON, FULLO, CK, 0, MK, EK, CIRC, CK),
        /* slider_indic */ SURF(0xFF9900, CK, EK, FULLO, CK, MK, MK, EK, CIRC, CK),
        /* knob         */ SURF(0xFFCC66, CK, NON, FULLO, CK, 0, MK, EK, CIRC, CK),
        /* cell         */ SURF(0x000000, CK, EK, FULLO, 0xCC99CC, 1, MK, EK, MK, CK),
        /* swatch       */ SURF(CK, CK, EK, FULLO, 0xFF9900, 2, MK, EK, CIRC, CK),
        /* info         */ SURF(0xCC6666, CK, NON, FULLO, 0xFF9900, 4, MK, EK, MK, 0x000000),
        /* accent       */ 0xFF9900,
        /* link         */ MARK(0x99CCFF, 6, FULLO, LEFT),
        /* active       */ MARK(0xFF9900, 6, FULLO, LEFT),
        /* info warn/err*/ 0xFFCC66, 0xCC6699,
        /* icon         */ 0xFF9900, 255, 90, 200, 70,
        /* glow         */ 0x000000, 0, 0,
        /* fonts        */ FONT_LCARS_SMALL, FONT_LCARS_NORMAL, FONT_LCARS_LARGE, 1,
    /* motion       */ MOTION(UI_ENTRY_FADE, UI_EASE_STEP, 16, 240, 48, 0,
                              UI_EASE_LINEAR, 0, 96, 48, 0),
    /* frame        */ FRAME(ui_frame_lcars, 3, 2, 6, 0),
    },
    {
        UI_THEME_NAME_LCARS " Night", UI_THEME_LCARS, true,
        /* screen       */ SURF(0x000000, CK, NON, FULLO, CK, MK, MK, EK, MK, 0xCC7700),
        /* tile         */ SURF(0x5A3A52, CK, NON, FULLO, CK, 0, MK, EK, RADIUS_LCARS, 0xE8C89A),
        /* tile_pressed */ SURF(0x8A5A2A, CK, NON, MK, CK, MK, MK, EK, MK, 0xF0D8B0),
        /* window       */ SURF(0x000000, CK, EK, FULLO, CK, 0, MK, EK, 0, CK),
        /* header       */ SURF(0xB37300, CK, NON, FULLO, CK, 0, MK, EK, CIRC, 0x000000),
        /* btn          */ SURF(0x6A3838, CK, NON, FULLO, CK, 0, MK, EK, CIRC, 0xE8C89A),
        /* btn_checked  */ SURF(0x8A5A2A, CK, NON, MK, CK, MK, MK, EK, MK, 0xF0D8B0),
        /* slider       */ SURF(0x1A1A1A, CK, NON, FULLO, CK, 0, MK, EK, CIRC, CK),
        /* slider_indic */ SURF(0xB37300, CK, EK, FULLO, CK, MK, MK, EK, CIRC, CK),
        /* knob         */ SURF(0x8A5A2A, CK, NON, FULLO, CK, 0, MK, EK, CIRC, CK),
        /* cell         */ SURF(0x000000, CK, EK, FULLO, 0x5A3A52, 1, MK, EK, MK, CK),
        /* swatch       */ SURF(CK, CK, EK, FULLO, 0xB37300, 2, MK, EK, CIRC, CK),
        /* info         */ SURF(0x6A3838, CK, NON, FULLO, 0xB37300, 4, MK, EK, MK, 0xE8C89A),
        /* accent       */ 0xB37300,
        /* link         */ MARK(0x4A6A8A, 6, FULLO, LEFT),
        /* active       */ MARK(0xB37300, 6, FULLO, LEFT),
        /* info warn/err*/ 0x8A6A1E, 0x8A2A44,
        /* icon         */ 0xCC7700, 255, 45, 160, 60,
        /* glow         */ 0x000000, 0, 0,
        /* fonts        */ FONT_LCARS_SMALL, FONT_LCARS_NORMAL, FONT_LCARS_LARGE, 1,
    /* motion       */ MOTION(UI_ENTRY_FADE, UI_EASE_STEP, 16, 240, 48, 0,
                              UI_EASE_LINEAR, 0, 96, 48, 0),
    /* frame        */ FRAME(ui_frame_lcars, 3, 2, 6, 0),
    },

    /* ----------------------------------------------------------------- JARVIS
     * Hairlines on a deep ground, with a shadow standing in for the bloom.
     * The tile's own outline is a dimmed cyan so that the brighter link and
     * amber control markers still read as a change of state rather than just a
     * thicker line. Concentric arc reticles are not achievable here for the
     * same reason as the LCARS elbows.
     *
     * The glow is kept narrow on purpose: LV_DRAW_SW_SHADOW_CACHE_SIZE is 0, so
     * every shadow is re-blurred on every frame, and a wide soft one would also
     * band visibly at 320x240 in RGB565. */
    {
        UI_THEME_NAME_JARVIS " Day", UI_THEME_JARVIS, false,
        /* screen       */ SURF(0x050B12, CK, NON, FULLO, CK, MK, MK, EK, MK, 0x9FE8FF),
        /* tile         */ SURF(0x0F2434, CK, NON, FULLO, 0x1E6E8C, 1, FULLO, EK, 2, 0xD8F6FF),
        /* tile_pressed */ SURF(0x13415C, 0x0C2A3D, VER, MK, CK, MK, MK, EK, MK, 0xFFFFFF),
        /* window       */ SURF(0x061018, CK, EK, FULLO, 0x35D6FF, 1, FULLO, EK, 0, CK),
        /* header       */ SURF(0x0E3247, CK, NON, FULLO, 0x35D6FF, 2, FULLO, BOT, 2, 0x9FE8FF),
        /* btn          */ SURF(0x0C2233, 0x071620, VER, FULLO, 0x1E6E8C, 1, FULLO, EK, 4, 0xD8F6FF),
        /* btn_checked  */ SURF(0x1A5A7D, 0x13415C, VER, MK, CK, MK, MK, EK, MK, 0xFFFFFF),
        /* slider       */ SURF(0x08202E, CK, NON, FULLO, 0x1E6E8C, 1, FULLO, EK, RADIUS_PANEL, CK),
        /* slider_indic */ SURF(0x35D6FF, CK, EK, FULLO, CK, MK, MK, EK, RADIUS_PANEL, CK),
        /* knob         */ SURF(0xD8F6FF, CK, NON, FULLO, 0x35D6FF, 1, FULLO, EK, CIRC, CK),
        /* cell         */ SURF(0x08202E, CK, EK, FULLO, 0x1E6E8C, 1, MK, EK, MK, CK),
        /* swatch       */ SURF(CK, CK, EK, FULLO, 0x35D6FF, 1, MK, EK, MK, CK),
        /* info         */ SURF(0x0E3247, CK, NON, FULLO, 0x35D6FF, 2, FULLO, EK, MK, 0xD8F6FF),
        /* accent       */ 0x35D6FF,
        /* link         */ MARK(0x35D6FF, 2, FULLO, EK),
        /* active       */ MARK(0xFFA23A, 2, FULLO, EK),
        /* info warn/err*/ 0xFFA23A, 0xFF5A4A,
        /* icon         */ 0x2E7F99, 255, 110, 180, 60,
        /* glow         */ 0x35D6FF, 8, 70,
        /* fonts        */ FONT_HUD_SMALL, FONT_HUD_NORMAL, FONT_HUD_LARGE, 1,
    /* motion       */ MOTION(UI_ENTRY_RISE, UI_EASE_OUT_EXPO, 240, 240, 80, -6,
                              UI_EASE_OUT_EXPO, 160, 256, 24, 0),
    /* frame        */ FRAME(ui_frame_jarvis, 3, 2, 6, 6),
    },
    {
        UI_THEME_NAME_JARVIS " Night", UI_THEME_JARVIS, true,
        /* screen       */ SURF(0x0A0603, CK, NON, FULLO, CK, MK, MK, EK, MK, 0xE0A860),
        /* tile         */ SURF(0x1E1409, CK, NON, FULLO, 0x6A4A1A, 1, FULLO, EK, 2, 0xE8C48A),
        /* tile_pressed */ SURF(0x3A2410, 0x2A1A0B, VER, MK, CK, MK, MK, EK, MK, 0xF0D8B0),
        /* window       */ SURF(0x0A0704, CK, EK, FULLO, 0xB3762A, 1, FULLO, EK, 0, CK),
        /* header       */ SURF(0x241706, CK, NON, FULLO, 0xB3762A, 2, FULLO, BOT, 2, 0xE0A860),
        /* btn          */ SURF(0x1C1207, 0x120B04, VER, FULLO, 0x6A4A1A, 1, FULLO, EK, 4, 0xE8C48A),
        /* btn_checked  */ SURF(0x3A2410, 0x2A1A0B, VER, MK, CK, MK, MK, EK, MK, 0xF0D8B0),
        /* slider       */ SURF(0x140D06, CK, NON, FULLO, 0x6A4A1A, 1, FULLO, EK, RADIUS_PANEL, CK),
        /* slider_indic */ SURF(0xB3762A, CK, EK, FULLO, CK, MK, MK, EK, RADIUS_PANEL, CK),
        /* knob         */ SURF(0xE0A860, CK, NON, FULLO, 0xB3762A, 1, FULLO, EK, CIRC, CK),
        /* cell         */ SURF(0x140D06, CK, EK, FULLO, 0x6A4A1A, 1, MK, EK, MK, CK),
        /* swatch       */ SURF(CK, CK, EK, FULLO, 0xB3762A, 1, MK, EK, MK, CK),
        /* info         */ SURF(0x241706, CK, NON, FULLO, 0xB3762A, 2, FULLO, EK, MK, 0xE8C48A),
        /* accent       */ 0xB3762A,
        /* link         */ MARK(0xB3762A, 2, FULLO, EK),
        /* active       */ MARK(0xE0A860, 2, FULLO, EK),
        /* info warn/err*/ 0x8A6A1E, 0x8A3A22,
        /* icon         */ 0x6A4A1A, 255, 90, 160, 55,
        /* glow         */ 0xB3762A, 6, 50,
        /* fonts        */ FONT_HUD_SMALL, FONT_HUD_NORMAL, FONT_HUD_LARGE, 1,
    /* motion       */ MOTION(UI_ENTRY_RISE, UI_EASE_OUT_EXPO, 240, 240, 80, -6,
                              UI_EASE_OUT_EXPO, 160, 256, 24, 0),
    /* frame        */ FRAME(ui_frame_jarvis, 3, 2, 6, 6),
    },
};

#undef CK
#undef MK
#undef EK
#undef VER
#undef NON
#undef LEFT
#undef BOT
#undef CIRC
#undef OP30
#undef OP70
#undef FULLO
#undef SURF
#undef MARK

static_assert(sizeof(ui_themes) / sizeof(ui_themes[0]) == UI_THEME_COUNT,
              "the theme table has to hold a day and a night entry per family");

static const struct ui_theme_s *theme = &ui_themes[0];

/* ------------------------------------------------------------------ helpers */

/* Reset before re-initialising, so that ui_style_init() can run a second time
 * on a live UI: without the reset, a property one variant sets and the next one
 * leaves alone would linger, and the property array would leak. */
static void style_reinit(lv_style_t *style)
{
    lv_style_reset(style);
    lv_style_init(style);
}

static void apply_surface(lv_style_t *style, const struct ui_surface_s *s)
{
    if (s->bg != UI_COLOR_KEEP)
        lv_style_set_bg_color(style, lv_color_hex(s->bg));

    if (s->bg_grad != UI_COLOR_KEEP)
        lv_style_set_bg_grad_color(style, lv_color_hex(s->bg_grad));

    if (s->grad_dir != UI_ENUM_KEEP)
        lv_style_set_bg_grad_dir(style, (lv_grad_dir_t)s->grad_dir);

    if (s->bg_opa != UI_METRIC_KEEP)
        lv_style_set_bg_opa(style, (lv_opa_t)s->bg_opa);

    if (s->border != UI_COLOR_KEEP)
        lv_style_set_border_color(style, lv_color_hex(s->border));

    if (s->border_width != UI_METRIC_KEEP)
        lv_style_set_border_width(style, s->border_width);

    if (s->border_opa != UI_METRIC_KEEP)
        lv_style_set_border_opa(style, (lv_opa_t)s->border_opa);

    if (s->border_side != UI_ENUM_KEEP)
        lv_style_set_border_side(style, (lv_border_side_t)s->border_side);

    if (s->radius != UI_METRIC_KEEP)
        lv_style_set_radius(style, s->radius);

    if (s->text != UI_COLOR_KEEP)
        lv_style_set_text_color(style, lv_color_hex(s->text));
}

/* ui_style_tile_pressed and ui_style_btn_checked have never been colours of
 * their own. v7 marked the active choice by forcing the widget into
 * LV_BTN_STATE_PRESSED; v9 has no such call, so the port reproduced that look
 * as the primary colour darkened twice over. A variant that wants the same
 * leaves the two backgrounds at UI_COLOR_KEEP and gets the derivation -- which
 * is what makes the Default variant identical to the old code by construction,
 * with no hand-computed hex to get wrong. LCARS, which presses *lighter*, names
 * its colours outright. */
static void apply_pressed(lv_style_t *style, const struct ui_surface_s *s, uint32_t accent)
{
    apply_surface(style, s);

    if (s->bg == UI_COLOR_KEEP)
    {
        lv_color_t c = lv_color_hex(accent);

        lv_style_set_bg_color(style, lv_color_darken(c, LV_OPA_60));
        lv_style_set_bg_grad_color(style, lv_color_darken(c, LV_OPA_30));
        lv_style_set_bg_grad_dir(style, LV_GRAD_DIR_VER);
    }
}

static void apply_marker(lv_style_t *style, const struct ui_marker_s *m)
{
    if (m->color != UI_COLOR_KEEP)
        lv_style_set_border_color(style, lv_color_hex(m->color));

    if (m->width != UI_METRIC_KEEP)
        lv_style_set_border_width(style, m->width);

    if (m->opa != UI_METRIC_KEEP)
        lv_style_set_border_opa(style, (lv_opa_t)m->opa);

    if (m->side != UI_ENUM_KEEP)
        lv_style_set_border_side(style, (lv_border_side_t)m->side);
}

static void apply_glow(lv_style_t *style)
{
    if (theme->glow_width == 0)
        return;

    lv_style_set_shadow_color(style, lv_color_hex(theme->glow));
    lv_style_set_shadow_width(style, theme->glow_width);
    lv_style_set_shadow_opa(style, theme->glow_opa);
}

/* ------------------------------------------------------------------ exported */

void ui_style_select(enum ui_theme_family_e family, bool night)
{
    if ((unsigned)family >= UI_THEME_FAMILY_COUNT)
        family = UI_THEME_DEFAULT;

    theme = &ui_themes[UI_STYLE_INDEX(family, night)];
}

enum ui_theme_family_e ui_style_family(void)
{
    return theme->family;
}

bool ui_style_night(void)
{
    return theme->night;
}

const char *ui_style_name(void)
{
    return theme->name;
}

const struct ui_theme_s *ui_style_theme(void)
{
    return theme;
}

void ui_style_init(void)
{
    static bool inited;

#if CONFIG_OHEZ_DEBUG_UI_STYLE
    /* The flat table is indexed by UI_STYLE_INDEX(), so entry i has to be the
     * day or night half of family i / 2. A row pasted into the wrong place
     * would otherwise show up only as the wrong colours. */
    for (unsigned i = 0; i < UI_THEME_COUNT; i++)
    {
        if (ui_themes[i].family != (enum ui_theme_family_e)(i / 2) ||
            ui_themes[i].night != ((i % 2) == 1))
            LV_LOG_WARN("theme table entry %u is out of order", i);
    }
#endif

    if (inited == false)
    {
        /* lv_style_reset() on never-initialised storage would walk a garbage
         * property array, so the first pass initialises instead of resetting. */
        inited = true;

        lv_style_init(&ui_style_tile);
        lv_style_init(&ui_style_tile_pressed);
        lv_style_init(&ui_style_tile_link);
        lv_style_init(&ui_style_tile_active);
        lv_style_init(&ui_style_label);
        lv_style_init(&ui_style_label_state);
        lv_style_init(&ui_style_label_large);
        lv_style_init(&ui_style_win_header);
        lv_style_init(&ui_style_btn);
        lv_style_init(&ui_style_btn_checked);
        lv_style_init(&ui_style_slider);
        lv_style_init(&ui_style_slider_knob);
        lv_style_init(&ui_style_screen);
        lv_style_init(&ui_style_window);
        lv_style_init(&ui_style_table_cell);
        lv_style_init(&ui_style_slider_indicator);
        lv_style_init(&ui_style_icon);
        lv_style_init(&ui_style_swatch);
        lv_style_init(&ui_style_info);
        lv_style_init(&ui_style_info_warning);
        lv_style_init(&ui_style_info_error);
    }
    else
    {
        style_reinit(&ui_style_tile);
        style_reinit(&ui_style_tile_pressed);
        style_reinit(&ui_style_tile_link);
        style_reinit(&ui_style_tile_active);
        style_reinit(&ui_style_label);
        style_reinit(&ui_style_label_state);
        style_reinit(&ui_style_label_large);
        style_reinit(&ui_style_win_header);
        style_reinit(&ui_style_btn);
        style_reinit(&ui_style_btn_checked);
        style_reinit(&ui_style_slider);
        style_reinit(&ui_style_slider_knob);
        style_reinit(&ui_style_screen);
        style_reinit(&ui_style_window);
        style_reinit(&ui_style_table_cell);
        style_reinit(&ui_style_slider_indicator);
        style_reinit(&ui_style_icon);
        style_reinit(&ui_style_swatch);
        style_reinit(&ui_style_info);
        style_reinit(&ui_style_info_warning);
        style_reinit(&ui_style_info_error);
    }

    /* ---- the screen ----
     * Everything textual that carries no style of its own -- the four header
     * labels, the window titles, the colour picker's H/S/V captions, the
     * slider's min and max labels -- reads its colour, font and letter spacing
     * from here, because those properties are inheritable. */
    apply_surface(&ui_style_screen, &theme->screen);
    lv_style_set_text_font(&ui_style_screen, theme->font_small);
    lv_style_set_text_letter_space(&ui_style_screen, theme->letter_space);

    /* ---- tiles ---- */
    apply_surface(&ui_style_tile, &theme->tile);
    lv_style_set_text_font(&ui_style_tile, theme->font_small);
    lv_style_set_pad_all(&ui_style_tile, PAD_TILE);
    apply_glow(&ui_style_tile);

    apply_pressed(&ui_style_tile_pressed, &theme->tile_pressed, theme->accent);

    /* Navigation tiles (links and groups) and controllable ones (switch,
     * slider, ...) are marked by an additive border over the tile above. */
    apply_marker(&ui_style_tile_link, &theme->link);
    apply_marker(&ui_style_tile_active, &theme->active);

    /* ---- text ---- */
    lv_style_set_text_font(&ui_style_label, theme->font_small);

    lv_style_set_text_font(&ui_style_label_state, theme->font_normal);
    lv_style_set_text_line_space(&ui_style_label_state, 0);

    lv_style_set_text_font(&ui_style_label_large, theme->font_large);

    /* ---- item windows ---- */
    apply_surface(&ui_style_window, &theme->window);

    apply_surface(&ui_style_win_header, &theme->header);
    lv_style_set_text_font(&ui_style_win_header, theme->font_normal);
    lv_style_set_pad_hor(&ui_style_win_header, 10);
    lv_style_set_pad_ver(&ui_style_win_header, 5);

    apply_surface(&ui_style_btn, &theme->btn);
    lv_style_set_text_font(&ui_style_btn, theme->font_small);
    lv_style_set_pad_all(&ui_style_btn, PAD_BTN);
    apply_glow(&ui_style_btn);

    apply_pressed(&ui_style_btn_checked, &theme->btn_checked, theme->accent);

    apply_surface(&ui_style_slider, &theme->slider);
    lv_style_set_pad_all(&ui_style_slider, PAD_TILE);

    apply_surface(&ui_style_slider_indicator, &theme->slider_indic);
    apply_surface(&ui_style_slider_knob, &theme->knob);

    /* ---- the systeminfo table ---- */
    apply_surface(&ui_style_table_cell, &theme->cell);
    lv_style_set_text_font(&ui_style_table_cell, theme->font_small);
    lv_style_set_pad_ver(&ui_style_table_cell, 0);

    /* ---- the colour swatches ----
     * Deliberately no bg_color: the swatch's fill is the item's own colour,
     * painted as a local style by colorpicker_preview() and
     * update_state_widget(), and a shared style must not fight it. */
    apply_surface(&ui_style_swatch, &theme->swatch);

    /* ---- the watermark icon ----
     * The openHAB icons are dark line art on transparency. Recolouring them is
     * the only way to keep them visible on a dark variant, but they sit behind
     * the tile's caption and state line, so the recolour has to stay dark
     * enough for that text to survive on top of it. */
    lv_style_set_image_opa(&ui_style_icon, theme->icon_opa);

    if (theme->icon_recolor_opa != 0)
    {
        lv_style_set_image_recolor(&ui_style_icon, lv_color_hex(theme->icon_recolor));
        lv_style_set_image_recolor_opa(&ui_style_icon, theme->icon_recolor_opa);
    }

    /* ---- the info label ---- */
    apply_surface(&ui_style_info, &theme->info);
    lv_style_set_text_font(&ui_style_info, theme->font_normal);
    /* Set here as well as on the screen: the info label lives on the top layer,
     * which is a screen root of its own and inherits nothing from ours. */
    lv_style_set_text_letter_space(&ui_style_info, theme->letter_space);
    lv_style_set_pad_all(&ui_style_info, PAD_INFO);

    lv_style_set_bg_color(&ui_style_info_warning, lv_color_hex(theme->info_warning_bg));
    lv_style_set_bg_color(&ui_style_info_error, lv_color_hex(theme->info_error_bg));

    /* Last, and from here rather than from its own call site, so that a theme
     * change re-times the press feedback along with everything else it
     * repaints. The transition descriptors it owns are what the shared press
     * styles point at, so they have to be rebuilt whenever these are. */
    ui_motion_styles_init();
}

void ui_style_apply(void)
{
    ui_style_init();

    /* Reaches every object of every screen, the top layer included: LVGL keeps
     * the bottom, top and sys layers in the display's screen array. It is also
     * why the live switch needs LV_OBJ_STYLE_CACHE to stay 0 (see lv_conf.h):
     * with the cache on, this refreshes only the first matching part of a
     * multi-part widget, and sliders and tables would keep their old colours. */
    lv_obj_report_style_change(NULL);
}

/* LCARS window headers end in a detached block, separated from the bar by a
 * sliver of background. That gap is what makes the shape read as LCARS rather
 * than as a rounded title bar, and it cannot come from a style: LVGL v9 has one
 * radius property for all four corners, so the bar and the block have to be two
 * objects.
 *
 * The block goes at the end of the flex row the header already is, after the
 * close button, and takes the accent's lighter partner. Every other family
 * returns without adding anything. */
void ui_style_decorate_window(lv_obj_t *header)
{
    if (theme->family != UI_THEME_LCARS)
        return;

    lv_obj_t *cap = lv_obj_create(header);

    lv_obj_remove_flag(cap, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(cap, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(cap, LV_DPI_DEF / 4, lv_pct(60));
    lv_obj_set_style_bg_opa(cap, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(cap, lv_color_hex(theme->btn_checked.bg), 0);
    lv_obj_set_style_border_width(cap, 0, 0);
    lv_obj_set_style_radius(cap, LV_RADIUS_CIRCLE, 0);
    /* The sliver of window background that detaches the block from the bar. */
    lv_obj_set_style_margin_left(cap, LV_DPI_DEF / 25, 0);
}
