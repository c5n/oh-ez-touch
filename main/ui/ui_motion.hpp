#ifndef UI_MOTION_HPP
#define UI_MOTION_HPP

#include <lvgl.h>
#include <stdint.h>

/* How this UI moves.
 *
 * The vocabulary is small on purpose, because the hardware is the design
 * constraint here and not a detail. The panel is a 320x240 RGB565 display on a
 * 40 MHz SPI bus: 5 MB/s, so a full-screen repaint is 30.7 ms of pure transfer
 * and rendering it costs about a quarter of that. Frame time is the SPI time.
 * The budget is therefore a *pixel* budget, and it is roughly
 *
 *   sustained motion   <= 24,000 px/frame   (9.6 ms, leaving the bus 60% free)
 *   a one-shot < 700ms <= 40,000 px/frame   (16 ms, about 30 fps)
 *
 * Everything below is chosen to stay inside that, and the two things that
 * cannot are named so nobody has to rediscover them:
 *
 *   - Never lv_obj_set_style_opa_layered(). It promotes the object to
 *     LV_LAYER_TYPE_SIMPLE, which at LV_DRAW_LAYER_SIMPLE_BUF_SIZE 4096 means a
 *     96x93 card is rendered ten times into ten heap-allocated sub-layers, every
 *     frame: about 2.8 ms against 0.37 ms for a plain opa. Plain
 *     lv_obj_set_style_opa() does *not* force a layer -- calculate_layer_type()
 *     never tests LV_STYLE_OPA -- and it is folded into every draw task and
 *     recursed into children on the same layer, so one animation fades a whole
 *     subtree for free. That is what ui_motion_fade() uses.
 *
 *   - Never transform_scale_x/y or transform_rotation. Those *do* force
 *     LV_LAYER_TYPE_TRANSFORM, which is not subdivided at all: a card becomes a
 *     ~43 KB ARGB8888 allocation out of the same heap as WiFi, every frame, plus
 *     a 4 ms resample. transform_width/height inflate the drawn box with no
 *     layer at all and read the same to a finger, which is why the press
 *     feedback is built on them -- as LVGL's own default theme does.
 *
 * The other rule, and the one that prevents crashes: every animation started
 * here sets lv_anim_set_var() to the lv_obj_t it animates, never to a context
 * struct. lv_obj_delete() calls lv_anim_delete(obj, NULL), so an animation
 * keyed on its object cannot outlive it. An animation whose var is application
 * state is a defect, not a style choice. */

/* Named curves. The theme table stores an id rather than a callback, so adding
 * a variant cannot quietly pull a new easing function into the build. */
enum ui_ease_e
{
    UI_EASE_LINEAR = 0,
    UI_EASE_STEP,          /* no interpolation at all -- the LCARS curve */
    UI_EASE_OUT_QUAD,
    UI_EASE_OUT_CUBIC,     /* the everyday settle */
    UI_EASE_IN_OUT_CUBIC,  /* symmetrical moves */
    UI_EASE_OUT_BACK,      /* overshoots and returns */
    UI_EASE_OUT_EXPO,      /* fast attack, long tail -- the JARVIS bloom */
    UI_EASE_COUNT
};

/* Never NULL: an id out of range resolves to linear. */
lv_anim_path_cb_t ui_motion_path(enum ui_ease_e ease);

/* How a thing arrives. The distance is in pixels and its sign is the direction,
 * so one enum covers "from the left" and "from the right". */
enum ui_entry_e
{
    UI_ENTRY_NONE = 0,
    UI_ENTRY_FADE,
    UI_ENTRY_RISE,   /* translate_y, with the fade */
    UI_ENTRY_SLIDE,  /* translate_x, with the fade */
    UI_ENTRY_COUNT
};

/* One family's motion, as plain scalars so the theme table stays in flash.
 *
 * The press is three numbers rather than one because the families genuinely
 * disagree about it: LCARS changes colour the instant it is touched and takes
 * its time coming back, which is what a machine in the show does, while Default
 * eases both ways. A hold before the return is what makes a tap shorter than
 * the transition still visible. */
struct ui_motion_cfg_s
{
    uint8_t  entry;         /* enum ui_entry_e */
    uint8_t  ease;          /* enum ui_ease_e, for entrances */
    uint16_t duration_ms;   /* per element */
    uint16_t screen_ms;     /* a whole-screen transition, where one is allowed */
    uint8_t  stagger_ms;    /* between elements; 0 means all at once */
    int8_t   entry_dist;    /* px; the sign is the direction */

    uint8_t  press_ease;    /* enum ui_ease_e */
    uint16_t press_ms;      /* into the pressed state */
    uint16_t press_out_ms;  /* back out of it */
    uint8_t  press_hold_ms; /* held pressed before the return starts */
    int8_t   press_grow;    /* transform_width/height; negative sinks the plate */
};

/* Fade a whole subtree. LV_STYLE_OPA, so no layer -- see the header comment.
 * The local property is removed again when the fade-in completes, so a later
 * lv_obj_report_style_change() is not fighting a leftover. */
void ui_motion_fade(lv_obj_t *obj, lv_opa_t from, lv_opa_t to,
                    uint32_t ms, uint32_t delay, enum ui_ease_e ease);

/* Bring one element in with the current theme's entry. `index` is its position
 * in the stagger, so callers walking a container pass 0, 1, 2, ... */
void ui_motion_enter_obj(lv_obj_t *obj, uint32_t index);

/* The same for every child of a container, in child order. */
void ui_motion_enter(lv_obj_t *container);

/* Attach the theme's press feedback -- the styles, and the contact tick
 * through ui_beep_attach_press(). Call once, at creation.
 *
 * Every tile, themed button, back bar and settings row goes through here,
 * which is what gives the whole interface one press sound from one place. */
void ui_motion_pressable(lv_obj_t *obj);

/* Cancel whatever this module started on `obj`. Deleting the object already
 * does this; this is for reusing one. */
void ui_motion_cancel(lv_obj_t *obj);

/* Rebuild the shared transition styles from the current theme. Called by
 * ui_style_init(), so a theme change re-times the press feedback. */
void ui_motion_styles_init(void);

/* Global off switch. Everything collapses to its end state immediately. */
void ui_motion_set_enabled(bool en);
bool ui_motion_enabled(void);

/* Added to a widget that should respond to being pressed. Built by
 * ui_motion_styles_init() from the theme's ui_motion_cfg_s. */
extern lv_style_t ui_style_press;         /* MAIN               */
extern lv_style_t ui_style_press_active;  /* MAIN | PRESSED     */

#endif /* UI_MOTION_HPP */
