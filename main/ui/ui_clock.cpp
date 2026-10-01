/**
 * @file ui_clock.cpp
 *
 * The screensaver: the time, and under it either the home page's three clock
 * items or the weekday and the date, in the colours the settings give the
 * theme's current variant -- and the dim state machine that decides when it
 * is up.
 */
#include "ui_clock.hpp"

#include "control/backlight_control.hpp"
#include "debug.h"
#include "items/item_screen.hpp"
#include "openhab_ui.hpp"
#include "port/port_sys.h"
#include "ui_screen.hpp"
#include "ui_settings.hpp"
#include "ui_style.hpp"
#include "ui_widgets.hpp"

#include <lvgl.h>
#include <time.h>

/* Owned and armed in main.cpp; the state this module hangs off is exactly the
 * one it answers for. */
extern BacklightControl tft_backlight;

static Config   *clock_config = NULL;
static lv_obj_t *clock_screen = NULL;
static lv_obj_t *clock_time   = NULL;
static lv_obj_t *clock_weekday = NULL;
static lv_obj_t *clock_date   = NULL;

/* The lower half in its other form: one column per clock item, each an icon
 * over the reading over the name. All NULL while the date is showing. */
static lv_obj_t *clock_row = NULL;
static lv_obj_t *clock_item_icon[CLOCK_ITEM_COUNT];
static lv_obj_t *clock_item_label[CLOCK_ITEM_COUNT];
static lv_obj_t *clock_item_reading[CLOCK_ITEM_COUNT];

/* What the lower half was built for, so the loop can tell when a new page has
 * brought other items -- or none. */
static size_t   clock_built_count = 0;
static uint32_t clock_built_generation = 0;

/* The colours on screen, 0xRRGGBB. Compared every loop against what the
 * settings and the theme variant ask for, so a save or the night schedule
 * recolours the screen while it is up. */
static uint32_t clock_fg = 0;
static uint32_t clock_bg = 0;

/* The second the labels were last written, so the loop below can run every
 * iteration and still only touch LVGL once a second. -1 means "expired, write
 * now"; the no-time case needs a sentinel of its own, because it cannot record
 * a second it does not have. */
static int clock_second = -1;
static bool clock_no_time = false;

/* German, and deliberately not strftime("%a"): the device's C library has no
 * locale worth the name, so the spellings are named here where the screensaver
 * can be read against them. The long forms only: a screensaver is legibility
 * at three metres, not information density. The one umlaut the month names
 * contain -- "März" -- is why the 36 px faces carry 0xE4; see
 * tools/build_fonts.sh. */
static const char *const day_names[7] = {
    "Sonntag", "Montag", "Dienstag", "Mittwoch",
    "Donnerstag", "Freitag", "Samstag"
};
static const char *const month_names[12] = {
    "Januar", "Februar", "März",     "April",   "Mai",      "Juni",
    "Juli",    "August",  "September", "Oktober", "November", "Dezember"
};

/* Write the labels. The shared styles carry the fonts, so a theme change
 * swaps the faces with no help from here; the colours are clock_colors_apply()'s. */
static void clock_update(void)
{
    struct tm timeinfo;
    char      text[24];

    /* The same convention as the header clock: on the device this fails until
     * NTP has answered, and the screensaver shows dashes rather than a time
     * that has not been set. */
    if (port_localtime(&timeinfo) == false)
    {
        if (clock_no_time == false)
        {
            clock_no_time = true;
            lv_label_set_text(clock_time, "--:--");

            if (clock_weekday != NULL)
            {
                lv_label_set_text(clock_weekday, "");
                lv_label_set_text(clock_date, "");
            }
        }

        return;
    }

    clock_no_time = false;

    if (timeinfo.tm_sec == clock_second)
        return;

    clock_second = timeinfo.tm_sec;

    /* A steady colon, unlike the header clock: the blink there is a matter of
     * taste the frame families disagree on, and a screensaver that is already
     * rewriting itself once a second gains nothing by flipping between two
     * spellings as well. */
    lv_snprintf(text, sizeof(text), "%02d:%02d", timeinfo.tm_hour,
                timeinfo.tm_min);
    lv_label_set_text(clock_time, text);

    if (clock_weekday == NULL)
        return;

    lv_label_set_text(clock_weekday, day_names[timeinfo.tm_wday]);

    /* The German order and punctuation: "24. September", the day number
     * carrying its full stop. */
    lv_snprintf(text, sizeof(text), "%d. %s", timeinfo.tm_mday,
                month_names[timeinfo.tm_mon]);
    lv_label_set_text(clock_date, text);
}

/* Text in `rgb`, on every label of the screen. Local styles over the shared
 * ones, which are what carry the faces; set on the labels themselves rather
 * than inherited, because the shared label styles set a colour of their own
 * and an inherited one would lose to it. */
static void clock_text_color(lv_obj_t *label, uint32_t rgb)
{
    if (label != NULL)
        lv_obj_set_style_text_color(label, lv_color_hex(rgb), LV_PART_MAIN);
}

static void clock_colors_apply(bool force)
{
    const config_item_t &item = clock_config->item;
    bool                 night = ui_style_night();
    uint32_t             fg = night ? item.backlight.clock_night_fg : item.backlight.clock_day_fg;
    uint32_t             bg = night ? item.backlight.clock_night_bg : item.backlight.clock_day_bg;

    if (force == false && fg == clock_fg && bg == clock_bg)
        return;

    clock_fg = fg;
    clock_bg = bg;

    lv_obj_set_style_bg_color(clock_screen, lv_color_hex(bg), LV_PART_MAIN);
    lv_obj_set_style_bg_grad_color(clock_screen, lv_color_hex(bg), LV_PART_MAIN);

    clock_text_color(clock_time, fg);
    clock_text_color(clock_weekday, fg);
    clock_text_color(clock_date, fg);

    for (size_t i = 0; i < CLOCK_ITEM_COUNT; i++)
    {
        clock_text_color(clock_item_label[i], fg);

        /* The reading is a container of two labels, the value and the unit. */
        if (clock_item_reading[i] != NULL)
        {
            clock_text_color(lv_obj_get_child(clock_item_reading[i], 0), fg);
            clock_text_color(lv_obj_get_child(clock_item_reading[i], 1), fg);
        }

        if (clock_item_icon[i] != NULL)
            lv_obj_set_style_image_recolor(clock_item_icon[i], lv_color_hex(fg), LV_PART_MAIN);
    }
}

/* Whether each icon wears the text colour, decided by what it is -- and
 * looked at every loop, because the pixels arrive whenever the server
 * answers. The built-in set is single-colour line art drawn for a light tile,
 * which on this screen's background would vanish, so it takes the text colour
 * whole. A server PNG keeps its own colours: a gauge or a weather icon
 * recoloured flat is a disc. */
static void clock_icons_recolor(void)
{
    for (size_t i = 0; i < CLOCK_ITEM_COUNT; i++)
    {
        if (clock_item_icon[i] == NULL)
            continue;

        const void *src = lv_image_get_src(clock_item_icon[i]);
        lv_opa_t    want = LV_OPA_TRANSP;

        if (   src != NULL
            && lv_image_src_get_type(src) == LV_IMAGE_SRC_VARIABLE
            && ((const lv_image_dsc_t *)src)->header.cf == LV_COLOR_FORMAT_I4)
            want = LV_OPA_COVER;

        if (lv_obj_get_style_image_recolor_opa(clock_item_icon[i], LV_PART_MAIN) != want)
            lv_obj_set_style_image_recolor_opa(clock_item_icon[i], want, LV_PART_MAIN);
    }
}

/* Take the lower half down, whichever form it is in. The clock items' objects
 * are handed back to openhab_ui first: it must not draw into them once they
 * are gone. */
static void clock_lower_destroy(void)
{
    openhab_ui_clock_detach();

    if (clock_weekday != NULL)
        lv_obj_delete(clock_weekday);

    if (clock_date != NULL)
        lv_obj_delete(clock_date);

    if (clock_row != NULL)
        lv_obj_delete(clock_row);

    clock_weekday = NULL;
    clock_date = NULL;
    clock_row = NULL;

    for (size_t i = 0; i < CLOCK_ITEM_COUNT; i++)
    {
        clock_item_icon[i] = NULL;
        clock_item_label[i] = NULL;
        clock_item_reading[i] = NULL;
    }
}

/* The lower half, for what the home page offers right now: its clock items
 * when it has a clock frame, the weekday and the date when it has not -- or
 * has not been loaded yet. */
static void clock_lower_build(void)
{
    clock_lower_destroy();

    clock_built_count = openhab_ui_clock_item_count();
    clock_built_generation = openhab_ui_clock_generation();

    if (clock_built_count == 0)
    {
        clock_weekday = lv_label_create(clock_screen);
        lv_obj_add_style(clock_weekday, &ui_style_label_large, LV_PART_MAIN);
        lv_obj_align(clock_weekday, LV_ALIGN_CENTER, 0, 32);

        clock_date = lv_label_create(clock_screen);
        lv_obj_add_style(clock_date, &ui_style_label_large, LV_PART_MAIN);
        lv_obj_align(clock_date, LV_ALIGN_CENTER, 0, 70);
    }
    else
    {
        /* Under the time, across the width, a third each: a column per item
         * however many there are, so one item sits in the middle and three
         * share the row evenly. */
        const ui_theme_s *theme = ui_style_theme();

        clock_row = ui_plain_container(clock_screen);
        lv_obj_set_size(clock_row, lv_pct(100), LV_SIZE_CONTENT);
        lv_obj_align(clock_row, LV_ALIGN_CENTER, 0, 62);
        lv_obj_set_flex_flow(clock_row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(clock_row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_START);
        lv_obj_set_style_pad_hor(clock_row, 4, 0);

        for (size_t i = 0; i < clock_built_count; i++)
        {
            lv_obj_t *column = ui_plain_container(clock_row);

            lv_obj_set_size(column, lv_pct(32), LV_SIZE_CONTENT);
            lv_obj_set_flex_flow(column, LV_FLEX_FLOW_COLUMN);
            lv_obj_set_flex_align(column, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                                  LV_FLEX_ALIGN_CENTER);
            lv_obj_set_style_pad_row(column, 2, 0);

            clock_item_icon[i] = lv_image_create(column);
            lv_obj_set_size(clock_item_icon[i], 32, 32);

            clock_item_reading[i] =
                ui_reading_create(column, &ui_style_label_large, &ui_style_label,
                                  lv_font_get_line_height(theme->font_large));

            clock_item_label[i] = lv_label_create(column);
            lv_obj_add_style(clock_item_label[i], &ui_style_label, LV_PART_MAIN);
            lv_label_set_long_mode(clock_item_label[i], LV_LABEL_LONG_DOT);
            lv_obj_set_width(clock_item_label[i], lv_pct(100));
            lv_obj_set_style_text_align(clock_item_label[i], LV_TEXT_ALIGN_CENTER, 0);

            openhab_ui_clock_attach(i, clock_item_icon[i], clock_item_label[i],
                                    clock_item_reading[i]);
        }
    }

    clock_colors_apply(true);

    /* The weekday and the date are written by clock_update(), on the next
     * second it notices. */
    clock_second = -1;
}

static void clock_show(void)
{
    /* Everything hides, uniformly: this was chosen over keeping the settings
     * visible, so whatever is up comes down first -- through its owner, not
     * by pushing over it, which would strand the owner holding a screen
     * ui_screen had already deleted. */
    if (ui_settings_is_open() == true)
        ui_settings_close();

    if (item_screen_is_open() == true)
        item_screen_dismiss();

    /* ui_settings_close() pops with an animation; a push while it is in
     * flight would race it for the display. Finish it first -- idempotent
     * when nothing is moving. */
    ui_screen_settle();

    clock_screen = ui_screen_create();

    /* Opaque, and in the settings' colours rather than the theme's: a pair
     * per variant, so the night schedule can take this screen down to
     * something that does not light a bedroom, and the day one can match a
     * wall. clock_colors_apply() fills them in once everything is built. */
    lv_obj_set_style_bg_opa(clock_screen, LV_OPA_COVER, LV_PART_MAIN);

    clock_time = lv_label_create(clock_screen);
    /* Well over three times the theme's largest role, and not from the theme
     * table: the time is the one thing on this screen anybody reads from
     * across the room, and custom_font_clock_117 carries exactly the glyphs
     * the line can spell -- see tools/build_fonts.sh. */
    lv_obj_set_style_text_font(clock_time, &custom_font_clock_117, LV_PART_MAIN);
    lv_obj_align(clock_time, LV_ALIGN_CENTER, 0, -54);

    clock_no_time = false;

    clock_lower_build();

    /* No animation, for the same SPI-per-frame reason as the item screen --
     * and none wanted: with the blank transition this happens with the
     * backlight at nothing, and without it, while the backlight is already
     * fading, which hides a hard cut better than any slide would. */
    ui_screen_push(clock_screen, UI_SCREEN_CLOCK, 0);

    clock_update();

    /* Drawn now, whole, rather than at the next timer tick: the backlight is
     * about to come up on whatever the panel holds, and that has to be this
     * screen rather than the page it replaced. */
    lv_refr_now(NULL);
}

static void clock_forget(void)
{
    clock_screen = NULL;
    clock_time   = NULL;
    clock_weekday = NULL;
    clock_date   = NULL;
    clock_row = NULL;

    for (size_t i = 0; i < CLOCK_ITEM_COUNT; i++)
    {
        clock_item_icon[i] = NULL;
        clock_item_label[i] = NULL;
        clock_item_reading[i] = NULL;
    }
}

static void clock_hide(void)
{
    /* ui_screen owns the delete -- it is reached from the main loop rather
     * than from an event on one of this screen's descendants, but the
     * unloaded-event path is the only one that deletes a pushed screen, and
     * keeping it that way is what makes the animated pops elsewhere safe. */
    openhab_ui_clock_detach();
    ui_screen_pop(0);
    clock_forget();

    /* The same as on the way in: the page is what the light comes up on. */
    lv_refr_now(NULL);
}

void ui_clock_setup(Config *config)
{
    clock_config = config;
}

void ui_clock_loop(void)
{
    if (clock_config == NULL)
        return;

    /* Somebody else may have pushed a screen over ours: the portal path in
     * main.cpp opens the settings once, and on a panel nobody has touched
     * since boot that can land while this is up. ui_screen has already
     * deleted ours, so the only thing left to do is forget it -- popping here
     * would take down whatever replaced it. */
    if (clock_screen != NULL && ui_screen_top() != UI_SCREEN_CLOCK)
    {
        openhab_ui_clock_detach();
        clock_forget();
    }

    /* The whole state machine: the setting and the dim state, read live. A
     * save that turns the checkbox off while the screen is up is honoured on
     * the next iteration, and so is the waking touch -- ohez_touch_wake()
     * starts the wake inside the pointer read, the backlight clears the dim
     * state once it is dark, and this pops the screen then -- before the
     * pointer suppression that follows the waking tap has lifted. */
    bool want = (clock_config->item.backlight.clock_dimmed == true)
                && (tft_backlight.isDimmed() == true);

    if (want == true && clock_screen == NULL)
        clock_show();
    else if (want == false && clock_screen != NULL)
        clock_hide();

    if (clock_screen == NULL)
        return;

    /* A new home page, or the first one: the lower half follows it. */
    if (   openhab_ui_clock_generation() != clock_built_generation
        || openhab_ui_clock_item_count() != clock_built_count)
        clock_lower_build();

    /* Every iteration, which costs a few comparisons when nothing changed. */
    clock_colors_apply(false);
    clock_icons_recolor();

    clock_update();
}