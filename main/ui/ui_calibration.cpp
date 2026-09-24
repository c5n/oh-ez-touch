/**
 * @file ui_calibration.cpp
 *
 * Four crosses, and then the arithmetic made visible.
 *
 * The result view is the part worth explaining. A calibration screen that ends
 * with "done" asks to be trusted; this one ends with the four taps it just
 * took, drawn twice -- once where the cross was, and once where the calibration
 * being replaced would have reported that same finger. The line between each
 * pair is the error, measured rather than asserted, and its length in pixels is
 * the headline. A panel that was already right draws four dots on four crosses
 * and says so, which is the answer the user wanted in that case anyway.
 *
 * Everything on the screen is therefore derived from the recorded raw pairs.
 * Nothing is illustrative.
 */
#include "ui_calibration.hpp"

#include <stdio.h>
#include <string.h>

#include <lvgl.h>

#include "port/ohez_port.h"
#include "ui_beep.hpp"
#include "ui_motion.hpp"
#include "ui_settings.hpp"
#include "ui_style.hpp"
#include "ui_widgets.hpp"

/* Where the crosses go, as a fraction of each axis: a tenth in from every edge.
 *
 * Not the corners themselves. A resistive panel is least linear at its extreme
 * edge, and the bezel makes the last few pixels hard to hit squarely; a tenth
 * in is still far enough apart that the extrapolation to the edges multiplies
 * any tap error by only a quarter. */
#define TARGET_INSET_DIV 10

/* When a press is the last one arriving twice rather than the next cross.
 *
 * By place, in raw counts, and with no time limit at all. The crosses are eight
 * tenths of the screen apart, and the reading being compared is the panel's own
 * -- so a press that reads where the last accepted one read is physically not
 * somebody pressing the next cross, however long they took over it. A full span
 * is around 3600 counts, which puts 400 comfortably inside "the same cross" and
 * nowhere near the next.
 *
 * It is worth having twice over. A resistive panel bounces, and one bounce
 * otherwise records the same corner against two different crosses -- the solve
 * then refuses the whole set as "two taps disagree", which is the safety net
 * working and a procedure to start over. And it is what makes pressing again
 * safe when a press seems not to have landed: the repeat is either the press
 * that was lost, or a duplicate that is dropped here. A script can simply keep
 * tapping until the count moves. */
#define TARGET_SAME_RAW 400

/* How long the result screen ignores presses after it appears.
 *
 * The fourth cross is pressed and the result takes its place, buttons and all,
 * in the same frame. A finger that bounces -- or a script that presses again
 * because it has not been told the fourth one landed yet -- then lands on
 * whichever button happens to be under it, and Keep is one of them. Storing a
 * calibration nobody has looked at is the one outcome this screen exists to
 * prevent.
 *
 * A quarter of a second is past any bounce and far short of reading four
 * numbers and a diagram. */
#define RESULT_GRACE_MS 400

/* The cross: arm length and the ring around it, in pixels. Large enough to aim
 * at with a finger, small enough that its centre is unambiguous. */
#define CROSS_ARM  11
#define CROSS_RING 9

/* The result view's furniture. The diagram takes whatever is left once the
 * sentence and the table have had theirs, between these two bounds: below the
 * minimum it stops being a panel and becomes a smudge, and above the maximum it
 * is spending room the numbers want. */
#define RESULT_FOOTER_H  44
#define RESULT_ROW_GAP   2
#define DIAGRAM_MIN_H    56
#define DIAGRAM_MAX_H    160
#define MARK_SIZE        5

static lv_obj_t *root;

/* Which cross is showing, or TOUCH_CAL_SAMPLES once all four are in. */
static unsigned step;

static struct touch_cal_sample_s samples[TOUCH_CAL_SAMPLES];

static struct touch_cal_s before_cal;
static struct touch_cal_s after_cal;
static bool               result_ready;
static int32_t            worst_px;
static int32_t            residual_px;

/* When the result screen appeared, for RESULT_GRACE_MS. */
static uint32_t           result_shown_at;
static const char        *refusal;

/* The raw pair taken at touchdown, held until the press turns out to be a tap.
 * Touchdown rather than release because that is where the finger meant to be,
 * and because ui_input.c withholds the click from a press that then travels --
 * so a smeared tap is dropped here for free and the user simply taps again. */
static int32_t press_raw_x;
static int32_t press_raw_y;
static bool    press_raw_valid;


/* lv_line keeps the pointer it is given rather than a copy, so the points have
 * to outlive the call that sets them. */
static lv_point_precise_t error_line[TOUCH_CAL_SAMPLES][2];

static void build_target(void);
static void build_result(void);

/* The overlay slot is not ours: the settings screen closes it when it closes
 * itself, and a theme change rebuilds the screen underneath. Following the
 * object's own destruction is therefore the only way to know this flow has
 * ended without ui_settings having to tell it.
 *
 * Guarded on the target, because the slot is replaced by *creating* the next
 * overlay and deleting the previous one asynchronously: that delete arrives
 * after `root` already names the new one. */
static void overlay_deleted_event(lv_event_t *e)
{
    if (lv_event_get_target(e) != root)
        return;

    root = NULL;
    step = 0;
    result_ready = false;
}

static int32_t display_w(void)
{
    return lv_display_get_horizontal_resolution(NULL);
}

static int32_t display_h(void)
{
    return lv_display_get_vertical_resolution(NULL);
}

/* Clockwise from the top left, which is only a convention -- touch_cal_solve()
 * discovers the two ends of each axis rather than being told which sample is
 * which. */
static void target_point(unsigned index, int32_t *x, int32_t *y)
{
    static const uint8_t corner[TOUCH_CAL_SAMPLES][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};

    int32_t w = display_w();
    int32_t h = display_h();
    int32_t inset_x = w / TARGET_INSET_DIV;
    int32_t inset_y = h / TARGET_INSET_DIV;

    *x = corner[index][0] ? (w - 1 - inset_x) : inset_x;
    *y = corner[index][1] ? (h - 1 - inset_y) : inset_y;
}

/* Asked of the overlay rather than read out of the theme table.
 *
 * The table's colours carry UI_COLOR_KEEP, the sentinel for "this variant does
 * not set that property" -- and its value is 0xFF000000, which lv_color_hex()
 * turns into black. LCARS leaves the window's text at KEEP and inherits a light
 * one from the screen, so reading the field gave black text and black hairlines
 * on a black overlay: a result screen that was almost entirely invisible, and
 * only in one of the four themes. The object knows what it resolved to. */
static lv_color_t color_text(void)
{
    if (root == NULL)
        return lv_color_black();

    return lv_obj_get_style_text_color(root, LV_PART_MAIN);
}

/* What the panel used to do with these taps, in the theme's accent: the one
 * colour every family reserves for "look here". */
static lv_color_t color_before(void)
{
    return lv_color_hex(ui_style_theme()->accent);
}

/* A bar of `w` by `h` at `x`, `y`, outside the parent's layout. Every mark on
 * this screen is one of these -- the crosses, their rings, the panel outline
 * and the dots -- because a rectangle with a radius is cheaper than any drawing
 * primitive and needs no LVGL module that is not already compiled in. */
static lv_obj_t *mark(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h,
                      lv_color_t color)
{
    lv_obj_t *obj = lv_obj_create(parent);

    lv_obj_set_ignore_layout(obj, true);
    lv_obj_set_scrollable(obj, false);
    lv_obj_set_clickable(obj, false);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_set_style_radius(obj, 0, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(obj, color, 0);

    return obj;
}

static void cross_draw(lv_obj_t *parent, int32_t cx, int32_t cy, lv_color_t color)
{
    mark(parent, cx - CROSS_ARM, cy, (CROSS_ARM * 2) + 1, 1, color);
    mark(parent, cx, cy - CROSS_ARM, 1, (CROSS_ARM * 2) + 1, color);

    lv_obj_t *ring = mark(parent, cx - (CROSS_RING / 2), cy - (CROSS_RING / 2),
                          CROSS_RING, CROSS_RING, color);

    lv_obj_set_style_bg_opa(ring, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ring, 1, 0);
    lv_obj_set_style_border_color(ring, color, 0);
    lv_obj_set_style_border_opa(ring, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, 0);
}

/* ------------------------------------------------------------- the result */

static int32_t distance(int32_t ax, int32_t ay, int32_t bx, int32_t by)
{
    int32_t dx = ax - bx;
    int32_t dy = ay - by;

    if (dx < 0)
        dx = -dx;

    if (dy < 0)
        dy = -dy;

    /* The larger axis plus a bit under half the smaller: within about three per
     * cent of a hypotenuse, with no square root and no float. This is a figure
     * the user reads off a screen, not one anything computes with. */
    return (dx > dy) ? (dx + (dy / 2)) : (dy + (dx / 2));
}

/* The worst a calibration is out at the four points that were actually
 * measured. Not a bound over the whole screen -- it is the error where it was
 * sampled, which is what the diagram beside it draws. */
static int32_t worst_error(const struct touch_cal_s *cal)
{
    int32_t worst = 0;

    for (unsigned i = 0; i < TOUCH_CAL_SAMPLES; i++)
    {
        int32_t sx = 0;
        int32_t sy = 0;

        port_indev_cal_map(cal, samples[i].raw_x, samples[i].raw_y, &sx, &sy);

        int32_t d = distance(sx, sy, samples[i].target_x, samples[i].target_y);

        if (d > worst)
            worst = d;
    }

    return worst;
}

static void solve(void)
{
    result_ready = false;
    refusal = port_indev_cal_solve(samples, TOUCH_CAL_SAMPLES, &after_cal);

    if (refusal != NULL)
        return;

    worst_px = worst_error(&before_cal);
    residual_px = worst_error(&after_cal);
    result_ready = true;
}

/* ------------------------------------------------------------- the events */

/* Whether the result screen has been up long enough to be answered. Every
 * button on it asks first -- see RESULT_GRACE_MS. */
static bool result_settled(void)
{
    return lv_tick_elaps(result_shown_at) >= RESULT_GRACE_MS;
}

static void close_event(lv_event_t *e)
{
    LV_UNUSED(e);

    if (result_settled() == false)
        return;

    BEEPER_EVENT_CANCEL();
    root = NULL;
    step = 0;
    result_ready = false;
    ui_settings_overlay_dismiss();
}

static void retry_event(lv_event_t *e)
{
    LV_UNUSED(e);

    if (result_settled() == false)
        return;

    BEEPER_EVENT_LINK();
    step = 0;
    result_ready = false;
    refusal = NULL;
    build_target();
}

static void keep_event(lv_event_t *e)
{
    LV_UNUSED(e);

    if (result_ready == false || result_settled() == false)
        return;

    root = NULL;
    step = 0;

    /* Stores, saves, applies and takes the overlay down. The next press is
     * already converted with what is on this screen, which is the point of
     * doing it here rather than leaving it in the draft for a Save the user
     * would have to find. */
    ui_settings_touch_cal_keep(&after_cal);
}

static bool raw_near(int32_t ax, int32_t ay, int32_t bx, int32_t by)
{
    int32_t dx = ax - bx;
    int32_t dy = ay - by;

    if (dx < 0)
        dx = -dx;

    if (dy < 0)
        dy = -dy;

    return dx < TARGET_SAME_RAW && dy < TARGET_SAME_RAW;
}

static void press_event(lv_event_t *e)
{

    press_raw_valid = port_indev_raw_press(&press_raw_x, &press_raw_y);
}

static void tap_event(lv_event_t *e)
{
    LV_UNUSED(e);

    if (step >= TOUCH_CAL_SAMPLES)
        return;

    /* A panel that cannot say what it read cannot be calibrated. Nothing is
     * recorded and the cross stays where it is, so the procedure neither
     * advances on a guess nor ends without saying why. */
    if (press_raw_valid == false)
        return;

    /* Where the last cross was pressed, pressed again. Dropped rather than
     * recorded against this one -- see TARGET_SAME_RAW. */
    if (step > 0 && raw_near(press_raw_x, press_raw_y, samples[step - 1].raw_x,
                             samples[step - 1].raw_y) == true)
    {
        press_raw_valid = false;
        return;
    }

    target_point(step, &samples[step].target_x, &samples[step].target_y);
    samples[step].raw_x = press_raw_x;
    samples[step].raw_y = press_raw_y;

    press_raw_valid = false;
    step++;

    BEEPER_EVENT_TICK();

    if (step < TOUCH_CAL_SAMPLES)
    {
        build_target();
        return;
    }

    solve();
    build_result();
}

/* -------------------------------------------------------- the target step */

static void build_target(void)
{
    root = ui_settings_overlay();

    if (root == NULL)
        return;

    lv_obj_set_style_pad_all(root, 0, 0);
    lv_obj_set_clickable(root, true);
    lv_obj_add_event_cb(root, overlay_deleted_event, LV_EVENT_DELETE, NULL);
    lv_obj_add_event_cb(root, press_event, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(root, tap_event, LV_EVENT_CLICKED, NULL);

    int32_t cx = 0;
    int32_t cy = 0;

    target_point(step, &cx, &cy);
    cross_draw(root, cx, cy, color_text());

    lv_obj_t *label = lv_label_create(root);

    lv_obj_set_ignore_layout(label, true);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(label, lv_pct(80));
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(label, color_text(), 0);
    lv_obj_set_style_text_font(label, ui_style_theme()->font_normal, 0);

    static char text[64];

    snprintf(text, sizeof(text), "Tap the centre of the cross\n%u of %u",
             (unsigned)(step + 1), (unsigned)TOUCH_CAL_SAMPLES);
    lv_label_set_text(label, text);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);
}

/* -------------------------------------------------------- the result step */

/* The panel, drawn small, with the four measurements on it.
 *
 * Positions are scaled to the box; the error *offsets* are not. A tap that was
 * nine pixels out is drawn nine pixels long, on a panel drawn at half size.
 *
 * That is a deliberate magnification and the headline says so. Scaling the
 * offsets too would be the literal picture and a useless one: the errors this
 * screen exists to show are single-digit pixels on a 320 px panel, so a
 * faithful drawing at any size that fits is four dots sitting on four crosses
 * -- the same picture a panel with no error at all produces. Drawing the error
 * at its own size keeps every length on the diagram a real measurement, with
 * nothing stretched to look worse than it is, while making the one thing being
 * compared visible.
 */
static void diagram_build(lv_obj_t *parent, int32_t avail_w, int32_t max_h)
{
    int32_t panel_w = display_w();
    int32_t panel_h = display_h();

    /* The panel's own shape, as large as fits: a diagram in a different aspect
     * ratio would put the marks in places the panel does not have. */
    int32_t box_h = max_h;
    int32_t box_w = (box_h * panel_w) / panel_h;

    if (box_w > avail_w)
    {
        box_w = avail_w;
        box_h = (box_w * panel_h) / panel_w;
    }

    lv_obj_t *box = lv_obj_create(parent);

    lv_obj_set_scrollable(box, false);
    lv_obj_set_clickable(box, false);
    lv_obj_set_size(box, box_w, box_h);
    lv_obj_set_style_pad_all(box, 0, 0);
    lv_obj_set_style_radius(box, 0, 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(box, 1, 0);
    lv_obj_set_style_border_color(box, color_text(), 0);
    lv_obj_set_style_border_opa(box, LV_OPA_40, 0);

    /* In the middle, which is the one part of this diagram nothing is ever
     * drawn in: every mark belongs to a corner. */
    lv_obj_t *legend = lv_label_create(box);

    lv_obj_set_ignore_layout(legend, true);
    lv_label_set_long_mode(legend, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(legend, box_w - (CROSS_ARM * 2));
    lv_obj_set_style_text_align(legend, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(legend, ui_style_theme()->font_small, 0);
    lv_obj_set_style_text_color(legend, color_text(), 0);
    lv_obj_set_style_text_opa(legend, LV_OPA_50, 0);
    /* Two words, because the box is about ninety pixels across and the corner
     * marks are twelve in from its edges. The sentence above the diagram
     * carries the meaning; this only has to stop the offsets being read as if
     * they were to the diagram's scale. */
    lv_label_set_text(legend, "dots at true size");
    lv_obj_align(legend, LV_ALIGN_CENTER, 0, 0);

    for (unsigned i = 0; i < TOUCH_CAL_SAMPLES; i++)
    {
        int32_t was_x = 0;
        int32_t was_y = 0;

        port_indev_cal_map(&before_cal, samples[i].raw_x, samples[i].raw_y, &was_x, &was_y);

        int32_t tx = (samples[i].target_x * box_w) / panel_w;
        int32_t ty = (samples[i].target_y * box_h) / panel_h;
        int32_t wx = tx + (was_x - samples[i].target_x);
        int32_t wy = ty + (was_y - samples[i].target_y);

        /* A calibration can put a tap off the panel entirely. The mark is held
         * at the edge it went past rather than dropped, because "it went that
         * way, further than this box goes" is the reading that matters and an
         * absent mark says nothing at all. The headline is where the true
         * distance is stated. */
        if (wx < 0)
            wx = 0;

        if (wy < 0)
            wy = 0;

        if (wx > box_w - 1)
            wx = box_w - 1;

        if (wy > box_h - 1)
            wy = box_h - 1;

        error_line[i][0].x = wx;
        error_line[i][0].y = wy;
        error_line[i][1].x = tx;
        error_line[i][1].y = ty;

        lv_obj_t *line = lv_line_create(box);

        lv_obj_set_ignore_layout(line, true);
        lv_obj_set_clickable(line, false);
        lv_obj_set_pos(line, 0, 0);
        lv_obj_set_size(line, box_w, box_h);
        lv_obj_set_style_line_width(line, 1, 0);
        lv_obj_set_style_line_color(line, color_before(), 0);
        lv_obj_set_style_line_opa(line, LV_OPA_COVER, 0);
        lv_line_set_points(line, error_line[i], 2);

        /* Where it landed, and where it should have. */
        lv_obj_t *dot = mark(box, wx - (MARK_SIZE / 2), wy - (MARK_SIZE / 2),
                             MARK_SIZE, MARK_SIZE, color_before());

        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);

        mark(box, tx - 3, ty, 7, 1, color_text());
        mark(box, tx, ty - 3, 1, 7, color_text());
    }
}

/* The four constants, old against new.
 *
 * Transposed -- the axes across the top, was/now/diff down the side -- because
 * that way round it is four rows instead of five and, more to the point, the
 * three numbers being compared sit in a column under a heading wide enough to
 * name them. The other way round the name column has to hold "X span" and the
 * value columns end up narrower than the four digits they carry.
 *
 * No arrow between was and now: the widest the fonts can spell one is "->".
 * The LV_SYMBOL_* set has none, and a real arrow is outside the 0x20-0xFF
 * ranges tools/build_fonts.sh generates, so it would render as a box.
 */
static void numbers_build(lv_obj_t *parent, int32_t avail_w)
{
    static const char *const axis_name[4] = {"X org", "X span", "Y org", "Y span"};
    static const char *const row_name[3] = {"was", "now", "diff"};

    int32_t old_value[4] = {before_cal.x_origin, before_cal.x_span,
                            before_cal.y_origin, before_cal.y_span};
    int32_t new_value[4] = {after_cal.x_origin, after_cal.x_span,
                            after_cal.y_origin, after_cal.y_span};

    lv_obj_t *table = lv_table_create(parent);

    lv_obj_set_scrollable(table, false);
    lv_obj_set_clickable(table, false);
    lv_obj_set_style_pad_all(table, 0, 0);
    lv_obj_set_style_border_width(table, 0, 0);
    lv_obj_set_style_bg_opa(table, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_hor(table, 2, LV_PART_ITEMS);
    /* No vertical cell padding: four rows of numbers have to fit under a
     * diagram on a screen 196 px tall, and the row spacing the font already
     * carries in its line height is enough to read them apart. */
    lv_obj_set_style_pad_ver(table, 0, LV_PART_ITEMS);
    lv_obj_set_style_text_font(table, ui_style_theme()->font_small, LV_PART_ITEMS);

    lv_table_set_column_count(table, 5);
    lv_table_set_row_count(table, 4);

    /* The row labels are the shortest text in the table, so they get the
     * narrowest column and the four axes divide the rest evenly. */
    int32_t label_w = (avail_w * 18) / 100;
    int32_t axis_w = (avail_w - label_w) / 4;

    lv_table_set_column_width(table, 0, label_w);

    for (unsigned i = 0; i < 4; i++)
    {
        lv_table_set_column_width(table, i + 1, axis_w);
        lv_table_set_cell_value(table, 0, i + 1, axis_name[i]);
    }

    lv_table_set_cell_value(table, 0, 0, "");

    for (unsigned r = 0; r < 3; r++)
    {
        lv_table_set_cell_value(table, r + 1, 0, row_name[r]);

        for (unsigned i = 0; i < 4; i++)
        {
            char text[16];

            if (r == 0)
                snprintf(text, sizeof(text), "%d", (int)old_value[i]);
            else if (r == 1)
                snprintf(text, sizeof(text), "%d", (int)new_value[i]);
            else
                /* Signed and always with its sign: this row is about the
                 * direction of the change as much as its size. */
                snprintf(text, sizeof(text), "%+d",
                         (int)(new_value[i] - old_value[i]));

            lv_table_set_cell_value(table, r + 1, i + 1, text);
        }
    }
}

static void build_result(void)
{
    root = ui_settings_overlay();

    if (root == NULL)
        return;

    lv_obj_set_style_pad_all(root, 0, 0);
    lv_obj_add_event_cb(root, overlay_deleted_event, LV_EVENT_DELETE, NULL);

    result_shown_at = lv_tick_get();

    int32_t hres = display_w();
    int32_t vres = display_h();

    /* The buttons are a footer of their own rather than the last item of the
     * scroll: the three ways out of this screen must not be below the fold on
     * the panel whose touch is the thing in question. */
    lv_obj_t *body = lv_obj_create(root);

    lv_obj_set_pos(body, 0, 0);
    lv_obj_set_size(body, lv_pct(100), vres - RESULT_FOOTER_H);
    lv_obj_set_style_pad_all(body, 4, 0);
    lv_obj_set_style_pad_row(body, RESULT_ROW_GAP, 0);
    lv_obj_set_style_radius(body, 0, 0);
    lv_obj_set_style_border_width(body, 0, 0);
    lv_obj_set_style_bg_opa(body, LV_OPA_TRANSP, 0);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(body, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    int32_t inner_w = hres - 8;

    static char summary[128];

    if (result_ready == false)
    {
        /* The message box's own two styles, in the order they are meant to be
         * used: ui_style_info_error sets a background colour and nothing else,
         * so on its own -- on a bare label, say -- it is invisible. */
        lv_obj_t *panel = lv_obj_create(body);

        lv_obj_set_scrollable(panel, false);
        lv_obj_set_width(panel, lv_pct(96));
        lv_obj_set_height(panel, LV_SIZE_CONTENT);
        lv_obj_add_style(panel, &ui_style_info, LV_PART_MAIN);
        lv_obj_add_style(panel, &ui_style_info_error, LV_PART_MAIN);
        lv_obj_set_style_pad_all(panel, 8, 0);

        lv_obj_t *reason = lv_label_create(panel);

        lv_label_set_long_mode(reason, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(reason, lv_pct(100));
        lv_obj_set_style_text_align(reason, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_text(reason, (refusal != NULL) ? refusal : "Calibration failed");

        lv_obj_t *hint = lv_label_create(body);

        lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(hint, lv_pct(96));
        lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_font(hint, ui_style_theme()->font_small, 0);
        lv_obj_set_style_text_color(hint, color_text(), 0);
        lv_label_set_text(hint, "Nothing was changed. Tap each cross at its "
                                "centre, once.");

        BEEPER_EVENT_ERROR();
    }
    else
    {
        lv_obj_t *headline = lv_label_create(body);

        lv_label_set_long_mode(headline, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(headline, lv_pct(100));
        lv_obj_set_style_text_align(headline, LV_TEXT_ALIGN_CENTER, 0);

        /* font_small, not the font_normal a heading would take: this has to fit
         * two lines on a panel 240 px across, and everything below it is worth
         * more room than the sentence describing it. */
        lv_obj_set_style_text_font(headline, ui_style_theme()->font_small, 0);
        lv_obj_set_style_text_color(headline, color_text(), 0);

        /* One line on a 320 px panel, two on a 240 px one -- which is where
         * there is room for two. The legend it used to carry is inside the
         * diagram now, in the space the diagram was not using. */
        if (worst_px <= 1)
            snprintf(summary, sizeof(summary), "Already accurate. Nothing to correct.");
        else
            snprintf(summary, sizeof(summary), "Off by up to %d px, now within %d px.",
                     (int)worst_px, (int)residual_px);

        lv_label_set_text(headline, summary);

        /* The table before the diagram, and then moved behind it.
         *
         * The diagram is the one thing on this screen with no size of its own:
         * it should have whatever is left. Measuring what is left rather than
         * predicting it is what makes that true across four theme families --
         * three typefaces, three sets of metrics, and a table whose rows LVGL
         * sizes to its own padding. A predicted row height is right for one
         * family and leaves the last row of numbers under the footer on the
         * others. */
        numbers_build(body, inner_w);
        lv_obj_update_layout(body);

        int32_t left = lv_obj_get_content_height(body)
                       - lv_obj_get_height(headline)
                       - lv_obj_get_height(lv_obj_get_child(body, 1))
                       - (2 * RESULT_ROW_GAP);

        if (left > DIAGRAM_MAX_H)
            left = DIAGRAM_MAX_H;

        if (left < DIAGRAM_MIN_H)
            left = DIAGRAM_MIN_H;


        diagram_build(body, inner_w, left);
        lv_obj_move_to_index(lv_obj_get_child(body, 2), 1);

        BEEPER_EVENT_ACCEPT();
    }

    lv_obj_t *footer = lv_obj_create(root);

    lv_obj_set_scrollable(footer, false);
    lv_obj_set_pos(footer, 0, vres - RESULT_FOOTER_H);
    lv_obj_set_size(footer, lv_pct(100), RESULT_FOOTER_H);
    lv_obj_add_style(footer, &ui_style_win_header, LV_PART_MAIN);
    lv_obj_set_style_radius(footer, 0, 0);
    lv_obj_set_style_pad_ver(footer, 0, 0);
    lv_obj_set_style_pad_hor(footer, 6, 0);
    lv_obj_set_style_pad_column(footer, 4, 0);
    lv_obj_set_flex_flow(footer, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(footer, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    /* Keep only where there is something to keep. A refused solve offers the
     * two things that are still true: measure again, or leave it alone. */
    if (result_ready == true)
    {
        lv_obj_add_event_cb(ui_themed_button(footer, "Keep"), keep_event, LV_EVENT_CLICKED,
                            NULL);
    }

    lv_obj_add_event_cb(ui_themed_button(footer, "Retry"), retry_event, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(ui_themed_button(footer, "Discard"), close_event, LV_EVENT_CLICKED,
                        NULL);

    ui_motion_enter(body);
}

/* ------------------------------------------------------------- the public */

void ui_calibration_open(void)
{
    if (port_indev_calibratable() == false)
        return;

    step = 0;
    result_ready = false;
    refusal = NULL;
    press_raw_valid = false;
    memset(samples, 0, sizeof(samples));

    /* What the panel is converting with right now, which is what the result
     * will be compared against -- not what config.json holds, because the two
     * differ on a board whose stored calibration is the "use the built-in
     * constants" zero. */
    port_indev_cal_get(&before_cal);

    BEEPER_EVENT_SCREEN();
    build_target();
}

bool ui_calibration_is_open(void)
{
    return root != NULL;
}

unsigned ui_calibration_targets(int32_t *pairs, unsigned max_pairs)
{
    if (pairs == NULL || root == NULL)
        return 0;

    unsigned count = (max_pairs < TOUCH_CAL_SAMPLES) ? max_pairs : TOUCH_CAL_SAMPLES;

    for (unsigned i = 0; i < count; i++)
        target_point(i, &pairs[i * 2], &pairs[(i * 2) + 1]);

    return count;
}

void ui_calibration_progress(unsigned *taken, unsigned *total)
{
    if (taken != NULL)
        *taken = (root != NULL) ? step : 0;

    if (total != NULL)
        *total = TOUCH_CAL_SAMPLES;
}

bool ui_calibration_result(struct touch_cal_s *before, struct touch_cal_s *after,
                           int32_t *worst, int32_t *residual)
{
    if (result_ready == false)
        return false;

    if (before != NULL)
        *before = before_cal;

    if (after != NULL)
        *after = after_cal;

    if (worst != NULL)
        *worst = worst_px;

    if (residual != NULL)
        *residual = residual_px;

    return true;
}
