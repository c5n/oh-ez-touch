/**
 * @file ui_clock.cpp
 *
 * The screensaver: the time, the weekday and the date in white on black, and
 * the dim state machine that decides when it is up.
 */
#include "ui_clock.hpp"

#include "control/backlight_control.hpp"
#include "debug.h"
#include "items/item_screen.hpp"
#include "port/port_sys.h"
#include "ui_screen.hpp"
#include "ui_settings.hpp"
#include "ui_style.hpp"

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

/* Write the labels. The shared styles carry the font and the colour, so a
 * theme or night-mode change restyles this screen with no help from here. */
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
            lv_label_set_text(clock_weekday, "");
            lv_label_set_text(clock_date, "");
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

    lv_label_set_text(clock_weekday, day_names[timeinfo.tm_wday]);

    /* The German order and punctuation: "24. September", the day number
     * carrying its full stop. */
    lv_snprintf(text, sizeof(text), "%d. %s", timeinfo.tm_mday,
                month_names[timeinfo.tm_mon]);
    lv_label_set_text(clock_date, text);
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

    /* Deliberately not the theme's colours: white on black reads as "the
     * panel is asleep" in a way no themed variant does, and it is the most
     * the dimmed backlight can show of either. The shared label styles are
     * still what carry the fonts -- only the colours are overridden here, so
     * a theme or night-mode change still swaps the faces and these two
     * properties stay put, which is exactly what a screensaver owes its
     * half-lit pixels. */
    lv_obj_set_style_bg_color(clock_screen, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_grad_color(clock_screen, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(clock_screen, LV_OPA_COVER, LV_PART_MAIN);

    clock_time = lv_label_create(clock_screen);
    /* Nearly three times the theme's largest role, and not from the theme
     * table: the time is the one thing on this screen anybody reads from
     * across the room, and custom_font_clock_130 carries exactly the glyphs
     * the line can spell -- see tools/build_fonts.sh. */
    lv_obj_set_style_text_font(clock_time, &custom_font_clock_130, LV_PART_MAIN);
    lv_obj_set_style_text_color(clock_time, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_align(clock_time, LV_ALIGN_CENTER, 0, -54);

    clock_weekday = lv_label_create(clock_screen);
    lv_obj_add_style(clock_weekday, &ui_style_label_large, LV_PART_MAIN);
    lv_obj_set_style_text_color(clock_weekday, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_align(clock_weekday, LV_ALIGN_CENTER, 0, 32);

    clock_date = lv_label_create(clock_screen);
    lv_obj_add_style(clock_date, &ui_style_label_large, LV_PART_MAIN);
    lv_obj_set_style_text_color(clock_date, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_align(clock_date, LV_ALIGN_CENTER, 0, 70);

    /* Not "-1 means expired" but a state nothing can collide with: the second
     * this holds is not one port_localtime() will report. */
    clock_second = -1;
    clock_no_time = false;

    /* No animation, for the same SPI-per-frame reason as the item screen; and
     * unlike the settings screen this one appears while the backlight is
     * already fading, which hides a hard cut better than any slide would. */
    ui_screen_push(clock_screen, UI_SCREEN_CLOCK, 0);

    clock_update();
}

static void clock_hide(void)
{
    /* ui_screen owns the delete -- it is reached from the main loop rather
     * than from an event on one of this screen's descendants, but the
     * unloaded-event path is the only one that deletes a pushed screen, and
     * keeping it that way is what makes the animated pops elsewhere safe. */
    ui_screen_pop(0);

    clock_screen = NULL;
    clock_time   = NULL;
    clock_weekday = NULL;
    clock_date   = NULL;
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
        clock_screen = NULL;
        clock_time   = NULL;
        clock_weekday = NULL;
        clock_date   = NULL;
    }

    /* The whole state machine: the setting and the dim state, read live. A
     * save that turns the checkbox off while the screen is up is honoured on
     * the next iteration, and so is the waking touch -- ohez_touch_wake()
     * clears the dim state inside the pointer read, and this pops the screen
     * before the 200 ms pointer suppression that follows it has lifted. */
    bool want = (clock_config->item.backlight.clock_dimmed == true)
                && (tft_backlight.isDimmed() == true);

    if (want == true && clock_screen == NULL)
        clock_show();
    else if (want == false && clock_screen != NULL)
        clock_hide();

    if (clock_screen != NULL)
        clock_update();
}