/**
 * @file ui_motion.cpp
 *
 * The easing curves, the entrance choreography and the press feedback.
 */
#include "ui_motion.hpp"

#include "ui_beep.hpp"

#include "ui_style.hpp"

lv_style_t ui_style_press;
lv_style_t ui_style_press_active;

static bool motion_enabled = true;

/* ------------------------------------------------------------------ easing */

/* LVGL's own cubic-bezier path is static and its parameter block is only filled
 * in for animations that opt into lv_anim_path_custom_bezier3(). Style
 * transitions do not: lv_obj_style_create_transition() builds its lv_anim_t
 * from lv_anim_init() and sets nothing but path_cb, so parameter.bezier3 stays
 * zeroed and a shared bezier path would evaluate cubic-bezier(0,0,0,0) there.
 *
 * Each curve is therefore self-contained -- one line over a shared evaluator,
 * using the public lv_cubic_bezier() and lv_map(). That also means the same
 * callback works for an animation we start and for a transition LVGL starts. */
static int32_t ease_eval(const lv_anim_t *a, int32_t x1, int32_t y1, int32_t x2, int32_t y2)
{
    if (a->duration == 0)
        return a->end_value;

    int32_t t = lv_map(a->act_time, 0, a->duration, 0, LV_BEZIER_VAL_MAX);
    int32_t step = lv_cubic_bezier(t, x1, y1, x2, y2);

    /* Not a shift: an overshooting curve returns more than LV_BEZIER_VAL_MAX and
     * a downward animation has end < start, so this has to stay signed. */
    return a->start_value +
           (int32_t)(((int64_t)step * (a->end_value - a->start_value)) >> LV_BEZIER_VAL_SHIFT);
}

#define EASE(name, x1, y1, x2, y2)                                        \
    static int32_t name(const lv_anim_t *a)                               \
    {                                                                     \
        return ease_eval(a, LV_BEZIER_VAL_FLOAT(x1), LV_BEZIER_VAL_FLOAT(y1), \
                            LV_BEZIER_VAL_FLOAT(x2), LV_BEZIER_VAL_FLOAT(y2)); \
    }

EASE(path_out_quad,     0.25, 0.46, 0.45, 0.94)
EASE(path_out_cubic,    0.22, 0.61, 0.36, 1.00)
EASE(path_in_out_cubic, 0.65, 0.00, 0.35, 1.00)
EASE(path_out_back,     0.34, 1.56, 0.64, 1.00)
EASE(path_out_expo,     0.16, 1.00, 0.30, 1.00)

lv_anim_path_cb_t ui_motion_path(enum ui_ease_e ease)
{
    switch (ease)
    {
    case UI_EASE_STEP:          return lv_anim_path_step;
    case UI_EASE_OUT_QUAD:      return path_out_quad;
    case UI_EASE_OUT_CUBIC:     return path_out_cubic;
    case UI_EASE_IN_OUT_CUBIC:  return path_in_out_cubic;
    case UI_EASE_OUT_BACK:      return path_out_back;
    case UI_EASE_OUT_EXPO:      return path_out_expo;
    case UI_EASE_LINEAR:
    default:                    return lv_anim_path_linear;
    }
}

/* ------------------------------------------------------------------- fades */

static void set_opa(void *obj, int32_t v)
{
    lv_obj_set_style_opa((lv_obj_t *)obj, (lv_opa_t)v, 0);
}

/* Hand the object back to its styles once it is fully opaque again. Without
 * this every faded-in object keeps a local LV_STYLE_OPA of 255 forever, and a
 * later theme change cannot override what a local property has pinned. LVGL's
 * own lv_obj_fade_in() does exactly this, for exactly this reason. */
static void fade_in_done(lv_anim_t *a)
{
    lv_obj_remove_local_style_prop((lv_obj_t *)a->var, LV_STYLE_OPA, 0);
}

void ui_motion_fade(lv_obj_t *obj, lv_opa_t from, lv_opa_t to,
                    uint32_t ms, uint32_t delay, enum ui_ease_e ease)
{
    if (obj == NULL)
        return;

    if (motion_enabled == false || ms == 0)
    {
        if (to >= LV_OPA_COVER)
            lv_obj_remove_local_style_prop(obj, LV_STYLE_OPA, 0);
        else
            lv_obj_set_style_opa(obj, to, 0);
        return;
    }

    lv_anim_t a;

    lv_anim_init(&a);
    lv_anim_set_var(&a, obj);
    lv_anim_set_exec_cb(&a, set_opa);
    lv_anim_set_values(&a, from, to);
    lv_anim_set_duration(&a, ms);
    lv_anim_set_delay(&a, delay);
    lv_anim_set_path_cb(&a, ui_motion_path(ease));

    /* Without this the start value is applied only once the delay expires, so a
     * staggered element renders at full opacity for its whole delay and then
     * snaps back to invisible. It is the one bug every naive stagger has. */
    lv_anim_set_early_apply(&a, true);

    if (to >= LV_OPA_COVER)
        lv_anim_set_completed_cb(&a, fade_in_done);

    lv_anim_start(&a);
}

/* ------------------------------------------------------------------ entries */

static void set_translate_y(void *obj, int32_t v)
{
    lv_obj_set_style_translate_y((lv_obj_t *)obj, v, 0);
}

static void set_translate_x(void *obj, int32_t v)
{
    lv_obj_set_style_translate_x((lv_obj_t *)obj, v, 0);
}

static void translate_done(lv_anim_t *a)
{
    lv_obj_remove_local_style_prop((lv_obj_t *)a->var, LV_STYLE_TRANSLATE_X, 0);
    lv_obj_remove_local_style_prop((lv_obj_t *)a->var, LV_STYLE_TRANSLATE_Y, 0);
}

void ui_motion_enter_obj(lv_obj_t *obj, uint32_t index)
{
    if (obj == NULL)
        return;

    const struct ui_motion_cfg_s *m = &ui_style_theme()->motion;

    if (motion_enabled == false || m->entry == UI_ENTRY_NONE || m->duration_ms == 0)
        return;

    uint32_t delay = (uint32_t)m->stagger_ms * index;

    ui_motion_fade(obj, LV_OPA_TRANSP, LV_OPA_COVER, m->duration_ms, delay,
                   (enum ui_ease_e)m->ease);

    if (m->entry == UI_ENTRY_FADE || m->entry_dist == 0)
        return;

    lv_anim_t a;

    lv_anim_init(&a);
    lv_anim_set_var(&a, obj);
    lv_anim_set_exec_cb(&a, (m->entry == UI_ENTRY_SLIDE) ? set_translate_x : set_translate_y);
    lv_anim_set_values(&a, m->entry_dist, 0);
    lv_anim_set_duration(&a, m->duration_ms);
    lv_anim_set_delay(&a, delay);
    lv_anim_set_path_cb(&a, ui_motion_path((enum ui_ease_e)m->ease));
    lv_anim_set_early_apply(&a, true);
    lv_anim_set_completed_cb(&a, translate_done);
    lv_anim_start(&a);
}

void ui_motion_enter(lv_obj_t *container)
{
    if (container == NULL)
        return;

    uint32_t count = lv_obj_get_child_count(container);

    for (uint32_t i = 0; i < count; i++)
        ui_motion_enter_obj(lv_obj_get_child(container, i), i);
}

/* ------------------------------------------------------------------ presses */

/* The properties a press is allowed to move. transform_width/height are in
 * here and transform_scale is not, for the reason in the header: the first pair
 * inflates the drawn box with no layer, the second allocates one per frame. */
static const lv_style_prop_t press_props[] = {
    LV_STYLE_BG_COLOR, LV_STYLE_BG_OPA, LV_STYLE_BG_GRAD_COLOR,
    LV_STYLE_BORDER_COLOR, LV_STYLE_BORDER_OPA, LV_STYLE_BORDER_WIDTH,
    LV_STYLE_TRANSFORM_WIDTH, LV_STYLE_TRANSFORM_HEIGHT,
    LV_STYLE_TRANSLATE_Y,
    LV_STYLE_SHADOW_WIDTH, LV_STYLE_SHADOW_OPA,
    LV_STYLE_TEXT_COLOR,
    0 /* the list is zero-terminated */
};

/* File statics, and they have to be: lv_style_set_transition() stores the
 * pointer, so a descriptor on the stack would leave the styles pointing at
 * freed memory the moment ui_style_init() returned. */
static lv_style_transition_dsc_t trans_press;
static lv_style_transition_dsc_t trans_release;

void ui_motion_styles_init(void)
{
    static bool inited;

    if (inited == false)
    {
        inited = true;
        lv_style_init(&ui_style_press);
        lv_style_init(&ui_style_press_active);
    }
    else
    {
        lv_style_reset(&ui_style_press);
        lv_style_reset(&ui_style_press_active);
    }

    const struct ui_motion_cfg_s *m = &ui_style_theme()->motion;

    uint32_t          in   = motion_enabled ? m->press_ms : 0;
    uint32_t          out  = motion_enabled ? m->press_out_ms : 0;
    uint32_t          hold = motion_enabled ? m->press_hold_ms : 0;
    lv_anim_path_cb_t path = ui_motion_path((enum ui_ease_e)m->press_ease);

    /* Two descriptors rather than one, following LVGL's own default theme. The
     * return is delayed by press_hold_ms so that a tap shorter than the
     * transition is still seen: on an instant press with no hold, a 30 ms touch
     * would be acknowledged and undone inside a single frame. */
    lv_style_transition_dsc_init(&trans_press, press_props, path, in, 0, NULL);
    lv_style_transition_dsc_init(&trans_release, press_props, path, out, hold, NULL);

    /* On MAIN, so it governs the way *back* to rest. */
    lv_style_set_transition(&ui_style_press, &trans_release);

    /* On PRESSED, so it governs the way in -- and carries the deformation. */
    lv_style_set_transition(&ui_style_press_active, &trans_press);

    if (m->press_grow != 0)
    {
        lv_style_set_transform_width(&ui_style_press_active, m->press_grow);
        lv_style_set_transform_height(&ui_style_press_active, m->press_grow);
    }
}

void ui_motion_pressable(lv_obj_t *obj)
{
    if (obj == NULL)
        return;

    lv_obj_add_style(obj, &ui_style_press, LV_PART_MAIN);
    lv_obj_add_style(obj, &ui_style_press_active,
                     ui_style_selector(LV_PART_MAIN, LV_STATE_PRESSED));

    /* And the audible half of the same acknowledgement. It is one call rather
     * than a handler here because a slider wants the sound without the plate
     * deformation -- see ui_beep_attach_press(), and the layering policy in
     * ui_beep.hpp before adding a second sound to anything this reaches. */
    ui_beep_attach_press(obj);
}

/* -------------------------------------------------------------------- misc */

void ui_motion_cancel(lv_obj_t *obj)
{
    if (obj == NULL)
        return;

    /* NULL means "any exec_cb", which is what LVGL itself uses to clear an
     * object's animations. */
    lv_anim_delete(obj, NULL);
}

void ui_motion_set_enabled(bool en)
{
    motion_enabled = en;
}

bool ui_motion_enabled(void)
{
    return motion_enabled;
}
