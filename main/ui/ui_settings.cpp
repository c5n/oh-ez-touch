/**
 * The on-device settings screen.
 *
 * Everything the web interface can change, changeable on the panel. The rows of
 * the openHAB, Sensors and Other tabs are not written out here: they are one
 * pass over config_fields[], the same table webui.cpp renders its form from,
 * so a setting cannot exist in one of the two and not the other. Only the WLAN
 * tab is hand-built, because credentials live in NVS rather than in
 * config.json, and only the Info tab is read-only.
 *
 * Unlike the item windows in openhab_ui.cpp this is a real LVGL screen. The
 * openHAB page keeps living underneath and comes back untouched -- no sitemap
 * refetch, no tile rebuild -- which also means openhab_ui_loop() can carry on
 * updating it while the user is in here.
 *
 * Edits go into a draft copy of Config::item, not into the live one. Closing
 * without saving therefore changes nothing, a half-typed hostname never reaches
 * wlan_setup(), and Save can tell exactly which fields moved -- which is what
 * decides whether a restart is worth offering.
 */

#include "sdkconfig.h"

#include "ui_settings.hpp"

#include "icons/icon_set.hpp"
#include "openhab_ui.hpp"
#include "config/config_fields.hpp"
#include "openhab/openhab_discover.hpp"
#include "openhab/openhab_sitemaps.hpp"
#include "control/beeper_control.hpp"
#include "ui_beep.hpp"
#include "ui_calibration.hpp"
#include "ui_motion.hpp"
#include "ui_geometry.hpp"
#include "ui_screen.hpp"
#include "ui_style.hpp"
#include "ui_theme.hpp"
#include "ui_widgets.hpp"
#include "version.h"
#include "net/wlan.hpp"
#include "debug.h"
#include "port/port_indev.h"
#include "port/port_net.h"
#include "port/port_sys.h"


#include <lvgl.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h> /* strcasecmp() */

/* 320x240 leaves very little room, so the chrome is measured rather than
 * proportional -- window_create()'s vertical/5 header would eat a fifth of the
 * screen. There is no title bar here at all: the tab bar and the footer are as
 * much chrome as 240 px can carry, and a third bar holding only a title and a
 * close button spent a row of settings on what the footer says and does just
 * as well. */
/* The one place in the UI that animates a whole screen. It costs a full-screen
 * repaint per frame -- about 30 ms at 40 MHz -- so seven frames is all this is,
 * and the visible stepping is the point: it reads as "somewhere else" rather
 * than as part of the page. Everything else staggers its contents instead. */
#define SETTINGS_ANIM_MS 240

/* A bar across the top that is entirely the way back, the same one the item
 * screens wear, and rows sized for the finger this panel is operated with.
 * ROW_HEIGHT was 30, which is four and a half millimetres on a 167 dpi panel;
 * 48 is a little over seven. */
#define BAR_HEIGHT    UI_BAR_H
#define FOOTER_HEIGHT 40
#define ROW_HEIGHT    48

/* A menu is two columns and as many rows as its entries need, rather than a
 * scrolling list, so every entry is on screen at once. Six 48 px rows would
 * not fit the 184 px below the bar, and the two you could not see would be
 * the two nobody ever found. */
#define INDEX_COLS   2
#define INDEX_GAP    6
#define INDEX_ROWS(n) (uint8_t)(((n) + INDEX_COLS - 1) / INDEX_COLS)

/* The Icons page is paged rather than scrolled: building a cell for every icon
 * the firmware carries at once -- three lv_objects each for the ~200 base
 * icons, on a heap that still holds the openHAB page living underneath this
 * screen -- is what used to run the panel out of memory and reset it. Five
 * cells across, as the flex grid this replaced was sized, and as many rows as
 * the panel is tall between the bar and the footer; the footer turns the
 * pages. */
#define ICON_PAGE_COLS 5
#define ICON_PAGE_GAP  6

/* The four ways a footer button turns one of those pages, in the order the
 * buttons sit in it. */
enum icons_page_e
{
    ICONS_PAGE_FIRST, /* << */
    ICONS_PAGE_PREV,  /* <  */
    ICONS_PAGE_NEXT,  /* >  */
    ICONS_PAGE_LAST   /* >> */
};

/* Enough for the widest text field in Config, which is the 63 character MQTT
 * password, plus room for the numbers. This buffer is not only what a row
 * *shows* -- field_edit_open() prefills the keyboard from it, so a value too
 * long for it would be truncated on the way in and then saved truncated. */
#define VALUE_BUFFER_LEN 80

/* More than a home has, and a hard bound on what a scan may return. */
#define SCAN_RESULT_MAX 16

/* How often the WLAN tab's state line is recomputed. It reports a state
 * machine that moves on 20 and 60 second timers, so this only has to be fine
 * enough to look prompt. */
#define WLAN_STATE_REFRESH_INTERVAL 1000

/* ------------------------------------------------------------------- state */

static Config *settings_config = NULL;

/* The edited copy, and the values as they were when the screen opened.
 * settings_restart_needed() compares the two. Static rather than allocated:
 * about 250 bytes against 320 KB of RAM, and no allocation to fail. */
static config_item_t draft;
static config_item_t baseline;

static lv_obj_t *screen = NULL;

/* What is on screen: a section, or a menu (MENU_ROOT and above -- see the menu
 * tables below). The tabview used to answer this. */
static uint8_t current_tab = SETTINGS_TAB_COUNT;

/* One per tab: the scrollable list of rows, and the footer's message label. */
static lv_obj_t *tab_rows[SETTINGS_TAB_COUNT];
static lv_obj_t *tab_status[SETTINGS_TAB_COUNT];

/* The Audio page's Demo button, kept so that its label can be flipped to Stop
 * and back. Cleared by widget_refs_clear() with the rest of them: the settings
 * screen is rebuilt whole on a theme change, and a pointer that outlived one
 * would be into a deleted object. */
static lv_obj_t *audio_demo_button = NULL;

/* The Icons page's image descriptors, one per cell on the page. lv_image keeps
 * the pointer it is given rather than the contents, so each icon needs a
 * descriptor of its own even though every field but ->data is the same. One
 * per *cell* and not one per icon: the page is paged rather than scrolled (see
 * ICON_PAGE_COLS), so only a page's worth ever exists at once. Allocated when
 * the page is built, freed by widget_refs_clear() -- which every path that
 * deletes the page's widgets (both screen_show_*() and ui_settings_close())
 * runs after the delete, never before it. */
static lv_image_dsc_t *icon_dscs = NULL;

/* Which page of the Icons catalogue is showing. Reset by every way of
 * arriving at the section, kept by ui_settings_rebuild() -- a theme change is
 * not a navigation, and must not throw away where the user was. */
static uint16_t icons_page = 0;

/* The keyboard or the confirmation prompt -- only ever one at a time, and a
 * child of the screen rather than of lv_layer_top(), because open() hides that
 * layer to keep the Messagebox banner off this screen. */
static lv_obj_t *overlay = NULL;
static lv_obj_t *overlay_textarea = NULL;

/* What the keyboard is editing: either a row of config_fields[] in the draft,
 * or a raw buffer (the WLAN credentials, which are not part of Config). */
static const struct config_field_s *edit_field = NULL;
static char                          *edit_buffer = NULL;
static size_t                         edit_buffer_size = 0;
static lv_obj_t                      *edit_row = NULL;

/* The WLAN tab. The credentials are edited here and only reach NVS on Save. */
static char wlan_ssid_buf[WLAN_SSID_SIZE];
static char wlan_psk_buf[WLAN_PSK_SIZE];

static lv_obj_t *wlan_state_label = NULL;
static lv_obj_t *wlan_ssid_row = NULL;
static lv_obj_t *wlan_psk_row = NULL;
static lv_obj_t *wlan_scan_list = NULL;

struct scan_result_s
{
    char   ssid[WLAN_SSID_SIZE];
    int8_t rssi;
    bool   encrypted;
};

static struct scan_result_s scan_results[SCAN_RESULT_MAX];
static uint8_t              scan_result_count = 0;
static bool                 scan_running = false;

static uint64_t wlan_state_refresh_deadline = 0;

/* The openHAB section. The Sitemap field is a text row like any other; what is
 * new is the list of what the server actually serves, built under the fields
 * from the cache openhab_sitemaps.cpp fills.
 *
 * `sitemaps_drawn` is the revision that list was built from, so the poll below
 * rebuilds it when the answer moves and not every few milliseconds. The host
 * and port are the ones the last fetch was asked for: the draft's, and when
 * the draft's move away from them -- somebody edited the host -- the list is
 * fetched again from where it would now come from. */
/* Which of the openHAB section's two pages is showing.
 *
 * The section is one entry in the menu and two screens under it: the lists of
 * what is out there, and the three fields behind the Manual button. They are
 * one section because they are one setting seen twice -- the lists write the
 * fields, and Save on either saves the same draft -- and two pages because a
 * 240 px screen that carried both was a page nobody could read. The flag is
 * cleared by every way of *arriving* at the section, so the lists are what it
 * opens on, and by Back on the manual page; a rebuild for a theme change goes
 * through screen_show_target() and keeps it, or a night switch would throw
 * away a half-typed hostname's page. */
static bool openhab_manual = false;

static lv_obj_t *host_field_row = NULL;
static lv_obj_t *port_field_row = NULL;
static lv_obj_t *sitemap_field_row = NULL;
static lv_obj_t *sitemap_list_obj = NULL;
static lv_obj_t *sitemap_status_label = NULL;
static uint32_t  sitemaps_drawn = 0;
static char      sitemaps_host[sizeof(draft.openhab.hostname)];
static int       sitemaps_port = 0;

/* And the same for the scan that finds the servers those sitemaps come from --
 * openHAB announces itself over mDNS, so the Host and Port rows can be filled
 * in from a list as well. Above the sitemap list on the page, because it comes
 * first in every sense: pick a server, then pick what it serves. */
static lv_obj_t *server_list_obj = NULL;
static lv_obj_t *server_status_label = NULL;
static uint32_t  servers_drawn = 0;

/* Whether the answers now in flight are worth a sound: set by the Scan button
 * and by nothing else. What happens because the page was opened is not
 * something anyone asked for by itself, and a panel that chimes at a page it
 * was merely shown is a panel that chimes at nothing. */
static bool sitemaps_announce = false;
static bool servers_announce = false;

/* A theme change asked for while an overlay is up. See ui_settings_rebuild(). */
static bool rebuild_pending = false;

/* A symbol and a name for every section.
 *
 * The index cell carries both, so unlike the tab bar this replaced -- six
 * pictograms across 320 px, each a sixth of the width, with the footer naming
 * whichever one you had hit -- the symbol only has to distinguish, not
 * explain. LV_SYMBOL_UPLOAD for MQTT, because a panel's side of a broker is
 * almost all publishing; LV_SYMBOL_GPS for Sensors, which is both a
 * thermometer and a beacon scanner and so is really about what is around the
 * panel; LV_SYMBOL_REFRESH for Time, because what that page configures is not
 * a clock but where the clock is fetched from. LV_SYMBOL_KEYBOARD for Touch,
 * because it is the only input device in the set and that page is about the
 * panel's -- it is also the on-screen keyboard's own cancel key, which is a
 * different surface and never on screen at the same time as this index.
* LV_SYMBOL_LIST for the Info menu, which is a pair of index-like pages rather
 * than something with a shape of its own, LV_SYMBOL_FILE for Fonts, the
 * one glyph in the set that names what a typeface lives in, and
 * LV_SYMBOL_IMAGE for Icons. Every one of these is a codepoint
 * tools/build_fonts.sh puts in the 16 and 22 px faces; a symbol outside that
 * list renders as a box. */
static const char *const tab_symbol[SETTINGS_TAB_COUNT] = {
    LV_SYMBOL_WIFI,      LV_SYMBOL_HOME,       LV_SYMBOL_UPLOAD,
    LV_SYMBOL_GPS,       LV_SYMBOL_EDIT,       LV_SYMBOL_KEYBOARD,
    LV_SYMBOL_REFRESH,   LV_SYMBOL_EYE_OPEN,   LV_SYMBOL_VOLUME_MAX,
    LV_SYMBOL_LIST,      LV_SYMBOL_FILE,       LV_SYMBOL_IMAGE};

/* The titles come from config_fields.hpp -- settings_tab_names[] -- so the
 * REST API's section tabs read the same as this screen's. */
static const char *const *const tab_title = settings_tab_names;

/* ------------------------------------------------------------- the menus */

/* Eight sections is more than an index of one screenful, so the index became
 * two of them. A menu entry names either a section or another menu, in one
 * uint8_t: below SETTINGS_TAB_COUNT it is an enum settings_tab_e, at or above
 * it a menu. MENU_ROOT is deliberately equal to SETTINGS_TAB_COUNT, which is
 * what ui_settings_open() has always taken to mean "the index".
 *
 * The pages that configure the installation -- the network the panel is on,
 * the servers it talks to, the hardware it reads, its name and where it gets
 * the time from -- are behind System, because they are set once when it goes
 * on the wall and then never again. What is left on the root menu is what
 * someone might actually walk over to the panel to change. */
#define MENU_ROOT       ((uint8_t)(SETTINGS_TAB_COUNT + 0))
#define MENU_SYSTEM     ((uint8_t)(SETTINGS_TAB_COUNT + 1))
#define MENU_INFO       ((uint8_t)(SETTINGS_TAB_COUNT + 2))
#define MENU_COUNT      3
#define MENU_IS(target) ((target) >= SETTINGS_TAB_COUNT)
#define MENU_AT(target) (&menus[(target) - SETTINGS_TAB_COUNT])

static constexpr uint8_t menu_root_entries[] = {SETTINGS_TAB_THEME, SETTINGS_TAB_AUDIO,
                                                MENU_SYSTEM, MENU_INFO};

static constexpr uint8_t menu_system_entries[] = {SETTINGS_TAB_WLAN,   SETTINGS_TAB_OPENHAB,
                                                   SETTINGS_TAB_MQTT,   SETTINGS_TAB_SENSORS,
                                                   SETTINGS_TAB_DEVICE, SETTINGS_TAB_TOUCH,
                                                   SETTINGS_TAB_TIME};

/* Info, split like System: the read-only table of what the panel is, and the
 * typefaces it draws with and the icons it carries. All three are things to
 * look at rather than change, which is what keeps them together on one
 * menu. */
static constexpr uint8_t menu_info_entries[] = {SETTINGS_TAB_INFO, SETTINGS_TAB_FONTS,
                                                 SETTINGS_TAB_ICONS};

struct menu_s
{
    const char    *title;
    const char    *symbol; /* how the menu above lists it; unused by the root */
    const uint8_t *entries;
    uint8_t        count;
    uint8_t        parent; /* where back goes; the root's is itself, and closes */
};

#define ENTRIES(a) (a), (uint8_t)(sizeof(a) / sizeof((a)[0]))

static constexpr struct menu_s menus[MENU_COUNT] = {
    {"Settings", NULL, ENTRIES(menu_root_entries), MENU_ROOT},
    {"System", LV_SYMBOL_SETTINGS, ENTRIES(menu_system_entries), MENU_ROOT},
    {"Info", LV_SYMBOL_LIST, ENTRIES(menu_info_entries), MENU_ROOT},
};

/* Every section on exactly one menu. Without this a tab added to
 * settings_tab_e but to no menu would compile, build its rows, and be
 * reachable from nothing -- and one listed twice would have a back bar that
 * returns to whichever menu menu_of() finds first, not the one it came
 * from. */
static constexpr bool menus_list_every_section_once(void)
{
    for (uint8_t tab = 0; tab < SETTINGS_TAB_COUNT; tab++)
    {
        unsigned seen = 0;

        for (size_t m = 0; m < MENU_COUNT; m++)
            for (size_t e = 0; e < menus[m].count; e++)
                if (menus[m].entries[e] == tab)
                    seen++;

        if (seen != 1)
            return false;
    }

    return true;
}

static_assert(menus_list_every_section_once(),
              "every settings section must be on exactly one menu");

/* Which menu lists a section -- and so where its back bar goes. Derived
 * rather than remembered: a stale "where I came from" is how a back button
 * ends up somewhere the user has never been. */
static uint8_t menu_of(uint8_t tab)
{
    for (uint8_t m = 0; m < MENU_COUNT; m++)
        for (uint8_t e = 0; e < menus[m].count; e++)
            if (menus[m].entries[e] == tab)
                return (uint8_t)(SETTINGS_TAB_COUNT + m);

    return MENU_ROOT;
}

static const char *target_title(uint8_t target)
{
    return MENU_IS(target) ? MENU_AT(target)->title : tab_title[target];
}

static const char *target_symbol(uint8_t target)
{
    const char *symbol = MENU_IS(target) ? MENU_AT(target)->symbol : tab_symbol[target];

    /* The root menu has none, because nothing lists it. An empty string
     * rather than its NULL, because lv_label_set_text(NULL) leaves LVGL's
     * default "Text" on the cell. */
    return (symbol != NULL) ? symbol : "";
}

static void screen_show_menu(uint8_t menu);
static void screen_show_section(uint8_t tab);
static void status_set(uint8_t tab, const char *text);

/* One entry point for both, so a caller does not have to know which it has. */
static void screen_show_target(uint8_t target)
{
    if (MENU_IS(target))
        screen_show_menu(target);
    else
        screen_show_section(target);
}

static void field_rows_build(uint8_t tab);
static void openhab_tab_build(lv_obj_t *rows);
static void wlan_tab_build(lv_obj_t *rows);
static void info_tab_build(lv_obj_t *rows);
static void fonts_tab_build(lv_obj_t *rows);
static void icons_tab_build(lv_obj_t *rows);
static void icons_page_event(lv_event_t *e);
static void wlan_state_update(void);
static void keyboard_cancel_event(lv_event_t *e);
static void keyboard_key_event(lv_event_t *e);

static void row_refresh(lv_obj_t *row, const struct config_field_s *f);

/* Forget every pointer into the widget tree.
 *
 * Called wherever that tree stops existing: both screen_show_*() begin with
 * lv_obj_clean(), which deletes the lot, and ui_settings_close() deletes the
 * screen itself. Every one of these is read by something that runs from the
 * loop -- ui_settings_loop() refreshes the WLAN state line and polls the scan
 * -- so a pointer left behind here is one those go on writing to after the
 * object is gone. It was written out three times, and the copy in
 * ui_settings_close() had drifted into a different order. */
static void widget_refs_clear(void)
{
    for (uint8_t i = 0; i < SETTINGS_TAB_COUNT; i++)
    {
        tab_rows[i] = NULL;
        tab_status[i] = NULL;
    }

    audio_demo_button = NULL;
    host_field_row = NULL;
    port_field_row = NULL;
    server_list_obj = NULL;
    server_status_label = NULL;
    servers_drawn = 0;
    sitemap_field_row = NULL;
    sitemap_list_obj = NULL;
    sitemap_status_label = NULL;
    /* And with them the revision they were drawn from: the next openHAB page
     * builds its list from whatever the cache holds by then, which is not
     * necessarily a newer revision than this one showed. */
    sitemaps_drawn = 0;
    wlan_state_label = NULL;
    wlan_ssid_row = NULL;
    wlan_psk_row = NULL;
wlan_scan_list = NULL;

    /* The Icons page's descriptors, outlived by this point by every image
     * that pointed into them -- the lv_obj_clean() that always runs before
     * here is what deleted those, and ui_screen_pop() did the same for the
     * one in ui_settings_close(). */
    free(icon_dscs);
    icon_dscs = NULL;

    /* The overlay is a child of the screen like everything else here, so the
     * lv_obj_clean() above has already deleted it. Forgetting it is the whole
     * job on this path -- overlay_close(), which is what normally clears these,
     * *deletes* as well, and a second delete of the same object is an LVGL
     * assertion rather than a no-op.
     *
     * It went unnoticed while the only overlays were the keyboard and the
     * restart confirmation: both cover the screen, so there was no way to reach
     * a back bar or a footer button underneath one, and nothing else rebuilt
     * the screen while one was up. The calibration is an overlay that a script
     * can leave standing -- `settings <page>` rebuilds the screen from under
     * it -- and the next overlay_create() then deleted an object that was
     * already gone. */
    overlay = NULL;
    overlay_textarea = NULL;
    edit_field = NULL;
    edit_buffer = NULL;
    edit_buffer_size = 0;
    edit_row = NULL;
}

/* ---------------------------------------------------------------- builders */

/* A settings row: full width, the name on the left and the current value on the
 * right. Both a button and a two-column layout, so that the whole row is the
 * touch target -- at 30 px tall there is no room for a separate control.
 *
 * Built on ui_themed_button() rather than on a bare lv_button, so that the row
 * and the buttons in the footer cannot disagree about what a press looks like.
 * The label that helper centres becomes the left column: a child of a parent
 * with a layout is positioned by the layout, so its own alignment is simply
 * not read. */
static lv_obj_t *row_create(lv_obj_t *parent, const char *name)
{
    lv_obj_t *row = ui_themed_button(parent, name);

    lv_obj_set_size(row, lv_pct(100), ROW_HEIGHT);
    lv_obj_set_style_pad_hor(row, 6, 0);
    lv_obj_set_style_pad_ver(row, 0, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *name_label = lv_obj_get_child(row, 0);

    lv_label_set_long_mode(name_label, LV_LABEL_LONG_DOT);
    lv_obj_set_flex_grow(name_label, 1);

    /* Child 1, which is what row_set_value() writes to. */
    lv_obj_t *value_label = lv_label_create(row);

    lv_label_set_text(value_label, "");
    lv_label_set_long_mode(value_label, LV_LABEL_LONG_DOT);
    /* Capped rather than content-sized: a 31 character item name would
     * otherwise push the field's own name off the row entirely. */
    lv_obj_set_style_max_width(value_label, lv_pct(55), 0);

    return row;
}

static void row_set_value(lv_obj_t *row, const char *value)
{
    lv_label_set_text(lv_obj_get_child(row, 1), value);
}

/* --------------------------------------------------------------- the rows */

/* config_field_value_text() plus the two substitutions that belong to a row and
 * not to the value: the placeholder that keeps an empty field from reading as a
 * broken row, and the asterisks that keep a secret off the screen. */
static void field_value_text(const struct config_field_s *f, const config_item_t *item,
                             char *buffer, size_t size)
{
    config_field_value_text(f, item, buffer, size);

    if (f->kind != SETTINGS_TEXT)
        return;

    if (buffer[0] == '\0')
    {
        /* An empty item name or hostname is a legitimate value, but a blank
         * right-hand column reads as a broken row. */
        snprintf(buffer, size, "%s", "--");
    }
    else if (f->flags & SETTINGS_F_SECRET)
    {
        snprintf(buffer, size, "%s", "*****");
    }
}

static void row_refresh(lv_obj_t *row, const struct config_field_s *f)
{
    char buffer[VALUE_BUFFER_LEN];

    field_value_text(f, &draft, buffer, sizeof(buffer));
    row_set_value(row, buffer);
}

/* --------------------------------------------------------------- overlays */

static void overlay_close(void)
{
    if (overlay == NULL)
        return;

    lv_obj_delete_async(overlay);
    overlay = NULL;
    overlay_textarea = NULL;
    edit_field = NULL;
    edit_buffer = NULL;
    edit_buffer_size = 0;
    edit_row = NULL;
}

/* Full-screen, opaque and clickable: the tabs below must neither show through
 * nor take a touch that was aimed at the overlay.
 *
 * IGNORE_LAYOUT is what makes "full-screen" true. The screen is a column flex
 * whose one item, the section list, grows to fill it; without the flag the
 * overlay becomes a second item, laid out *after* the list at the bottom edge and
 * 240 px tall from there, so all but its top edge falls off the display. */
static lv_obj_t *overlay_create(void)
{
    overlay_close();

    overlay = lv_obj_create(screen);

    lv_obj_add_flag(overlay, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_remove_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(overlay, 0, 0);
    lv_obj_set_size(overlay, lv_pct(100), lv_pct(100));
    lv_obj_set_style_pad_all(overlay, 4, 0);
    lv_obj_add_style(overlay, &ui_style_window, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_COVER, 0);

    /* Sized now rather than at the next refresh.
     *
     * lv_pct(100) is resolved against the parent during layout, and the pointer
     * is read by a timer that can run before that: an overlay created in one
     * frame is whatever lv_obj_create() made it until the next one. A press in
     * that window is hit-tested against the wrong rectangle and lands on
     * whatever is underneath -- which, at the top of this screen, is the back
     * bar. It then rebuilds the screen and deletes the overlay that was being
     * pressed.
     *
     * Rare and confusing rather than harmless: the calibration's first cross is
     * inside the bar's band, and losing that press took the whole procedure
     * with it. The keyboard and the restart confirmation are built the same way
     * and have always had the same window; theirs is just harder to hit. */
    lv_obj_update_layout(overlay);

    return overlay;
}

lv_obj_t *ui_settings_overlay(void)
{
    if (screen == NULL)
        return NULL;

    return overlay_create();
}

void ui_settings_overlay_dismiss(void)
{
    overlay_close();
}

void ui_settings_touch_cal_keep(const struct touch_cal_s *cal)
{
    if (cal == NULL || settings_config == NULL)
        return;

    /* All four places at once. The draft so the rows under the overlay show
     * what was just measured and a later Save does not put the old numbers
     * back; the baseline so that Save is then not told a restart-flagged field
     * moved; the live config because that is what saveConfig() writes. */
    draft.touch.x_origin = (unsigned int)cal->x_origin;
    draft.touch.x_span = (unsigned int)cal->x_span;
    draft.touch.y_origin = (unsigned int)cal->y_origin;
    draft.touch.y_span = (unsigned int)cal->y_span;

    baseline.touch = draft.touch;
    settings_config->item.touch = draft.touch;

    bool stored = settings_config->saveConfig();

    /* Applies the calibration to the pointer, among everything else it
     * re-applies. Unconditional, like save_event()'s: a calibration that could
     * not be written is still a calibration that works until the next boot,
     * and the user finds that out from the footer rather than from the panel. */
    settings_apply_live(settings_config);

    overlay_close();

    /* Rebuilt rather than refreshed row by row: the four rows are showing the
     * numbers that have just changed, and screen_show_section() is what every
     * other return to a page already does. */
    screen_show_section(SETTINGS_TAB_TOUCH);

    if (stored == false)
    {
        status_set(SETTINGS_TAB_TOUCH, "Applied, not saved");
        BEEPER_EVENT_ERROR();
        return;
    }

    status_set(SETTINGS_TAB_TOUCH, "Calibration saved");
    BEEPER_EVENT_ACCEPT();
}

static void calibrate_event(lv_event_t *e)
{
    LV_UNUSED(e);

    ui_calibration_open();
}

static void confirm_restart_event(lv_event_t *e)
{
    LV_UNUSED(e);

    /* Does not return on either target: the simulator exits, which is the
     * same statement made by a process that cannot reboot itself. */
    port_restart();
}

static void confirm_dismiss_event(lv_event_t *e)
{
    LV_UNUSED(e);

    /* "Later" is a refusal, not a window closing. */
    BEEPER_EVENT_CANCEL();
    overlay_close();
}

/* Not an lv_msgbox, even though LV_USE_MSGBOX is now 1 for the message boxes
 * in ui_messagebox.cpp. Those wanted the header and its one button; this wants
 * two buttons of equal weight and no header at all, and a footer that has to
 * be told twice that neither of them is the default is more work than the
 * label and the two buttons it would replace. */
static void confirm_restart_open(const char *text)
{
    lv_obj_t *root = overlay_create();

    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(root, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    lv_obj_t *label = lv_label_create(root);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(label, lv_pct(96));
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(label, text);

    lv_obj_t *buttons = ui_plain_container(root);
    lv_obj_set_size(buttons, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(buttons, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(buttons, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    lv_obj_add_event_cb(ui_themed_button(buttons, "Restart"), confirm_restart_event,
                        LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(ui_themed_button(buttons, "Later"), confirm_dismiss_event,
                        LV_EVENT_CLICKED, NULL);
}

/* --------------------------------------------------------- the keyboard */

static void keyboard_ready_event(lv_event_t *e)
{
    LV_UNUSED(e);

    const char *text = lv_textarea_get_text(overlay_textarea);

    if (edit_field != NULL)
    {
        switch (edit_field->kind)
        {
        case SETTINGS_TEXT:
            /* False means the value contained '/' or ':' on a row that forbids
             * them. Dropping it silently is what the web form does; the row
             * keeps its old value, which is visible straight away. */
            config_field_set_text(edit_field, &draft, text);
            break;

        default:
            config_field_set_number(edit_field, &draft, strtol(text, NULL, 10));
            break;
        }

        if (edit_row != NULL)
            row_refresh(edit_row, edit_field);
    }
    else if (edit_buffer != NULL)
    {
        strlcpy(edit_buffer, text, edit_buffer_size);

        if (edit_row != NULL)
        {
            /* The passphrase is never shown, here or in the web form. */
            if (edit_buffer == wlan_psk_buf)
                row_set_value(edit_row, (text[0] == '\0') ? "--" : "*****");
            else
                row_set_value(edit_row, (text[0] == '\0') ? "--" : text);
        }
    }

    BEEPER_EVENT_ACCEPT();
    overlay_close();
}

static void keyboard_cancel_event(lv_event_t *e)
{
    LV_UNUSED(e);

    BEEPER_EVENT_CANCEL();
    overlay_close();
}

/* One tick per key, and a different one for backspace: a character gained and
 * a character lost are the two things a keyboard does, and telling them apart
 * is most of what makes typing without looking survivable.
 *
 * The keys excluded here are the ones whose outcome speaks for itself on
 * release -- OK and Cancel get ACCEPT and CANCEL, and the mode switch and
 * newline are either those or nothing. Everything else, the cursor arrows and
 * the abc/ABC/1# keys included, is a tick. */
static void keyboard_key_event(lv_event_t *e)
{
    lv_obj_t *kb = (lv_obj_t *)lv_event_get_current_target(e);
    uint32_t  id = lv_keyboard_get_selected_button(kb);

    if (id == LV_BUTTONMATRIX_BUTTON_NONE)
        return;

    const char *text = lv_keyboard_get_button_text(kb, id);

    if (text == NULL)
        return;

    if (strcmp(text, LV_SYMBOL_OK) == 0 || strcmp(text, LV_SYMBOL_CLOSE) == 0 ||
        strcmp(text, LV_SYMBOL_KEYBOARD) == 0 || strcmp(text, LV_SYMBOL_NEW_LINE) == 0)
        return;

    if (strcmp(text, LV_SYMBOL_BACKSPACE) == 0)
        BEEPER_EVENT_TICK_BACK();
    else
        BEEPER_EVENT_TICK();
}

/* Laid out with flex rather than aligned by hand: lv_keyboard's own default is
 * half the parent's height, and any fixed share of 240 px that leaves the four
 * key rows enough finger room also cuts the bottom one off -- which is the row
 * carrying LVGL's OK and cancel keys. Letting flex hand the keyboard whatever
 * is left over cannot clip it.
 *
 * The cancel key LVGL puts in that row is LV_SYMBOL_KEYBOARD, the phone
 * convention for dismissing one; the explicit close button on the title line is
 * for everyone who does not read it that way, and it sits where the finger just
 * came from on the screen's own header. */
static void keyboard_open(const char *title, const char *value, uint32_t max_length,
                          bool numeric, bool password)
{
    lv_obj_t *root = overlay_create();

    lv_obj_set_style_pad_all(root, 3, 0);
    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(root, 3, 0);

    lv_obj_t *title_row = ui_plain_container(root);
    lv_obj_set_size(title_row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(title_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(title_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    lv_obj_t *label = lv_label_create(title_row);
    lv_label_set_text(label, title);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_obj_add_style(label, &ui_style_label, LV_PART_MAIN);
    lv_obj_set_flex_grow(label, 1);

    lv_obj_add_event_cb(ui_themed_button(title_row, LV_SYMBOL_CLOSE), keyboard_cancel_event,
                        LV_EVENT_CLICKED, NULL);

    overlay_textarea = lv_textarea_create(root);
    lv_textarea_set_one_line(overlay_textarea, true);
    lv_textarea_set_max_length(overlay_textarea, max_length);
    lv_textarea_set_password_mode(overlay_textarea, password);
    lv_textarea_set_text(overlay_textarea, value);
    /* Without a style of its own the field would be lv_theme_simple's white on
     * the Material variant's white panel -- an invisible input. The row style
     * is the right one to borrow: this is what a row looks like being
     * edited. */
    lv_obj_add_style(overlay_textarea, &ui_style_btn, LV_PART_MAIN);
    lv_obj_set_width(overlay_textarea, lv_pct(100));

    lv_obj_t *keyboard = lv_keyboard_create(root);
    lv_keyboard_set_mode(keyboard, numeric ? LV_KEYBOARD_MODE_NUMBER : LV_KEYBOARD_MODE_TEXT_LOWER);
    lv_keyboard_set_textarea(keyboard, overlay_textarea);
    /* The popover magnifies the pressed key over its neighbours, which is the
     * difference between usable and not at this key size. */
    lv_keyboard_set_popovers(keyboard, true);
    lv_obj_add_style(keyboard, &ui_style_window, LV_PART_MAIN);
    lv_obj_add_style(keyboard, &ui_style_btn, LV_PART_ITEMS);
    lv_obj_set_width(keyboard, lv_pct(100));
    lv_obj_set_flex_grow(keyboard, 1);

    lv_obj_add_event_cb(keyboard, keyboard_ready_event, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(keyboard, keyboard_cancel_event, LV_EVENT_CANCEL, NULL);

    /* PRESSED, and not VALUE_CHANGED, for three reasons.
     *
     * With popovers on, a character key carries LV_BUTTONMATRIX_CTRL_POPOVER
     * and fires VALUE_CHANGED on *release*, while backspace and every key of
     * the numeric map fire on press -- so a tick there would be timed
     * differently depending on which key it was. LVGL's own keyboard callback
     * also runs before ours and turns OK into a synchronous LV_EVENT_READY,
     * which reaches overlay_close() and schedules this keyboard for deletion
     * mid-dispatch; PRESSED never has to reason about that, because OK and
     * Cancel are click-triggered and do nothing on the way down. And a tick on
     * contact is what a key tick is *for*.
     *
     * This keyboard is deliberately not ui_motion_pressable() -- if it ever
     * becomes so, every key will sound twice. See ui_beep.hpp. */
    lv_obj_add_event_cb(keyboard, keyboard_key_event, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(keyboard, keyboard_key_event, LV_EVENT_LONG_PRESSED_REPEAT, NULL);

    BEEPER_EVENT_SCREEN();
}

static void field_edit_open(lv_obj_t *row, const struct config_field_s *f)
{
    char buffer[VALUE_BUFFER_LEN];

    if (f->kind == SETTINGS_TEXT)
    {
        bool secret = (f->flags & SETTINGS_F_SECRET) != 0;

        /* A secret is prefilled like any other field, and hidden by the
         * textarea's password mode rather than by being withheld: this row is
         * how it is corrected, and a field that empties itself every time it is
         * opened cannot be edited, only retyped. The WLAN passphrase does
         * withhold, but that one is not a Config field and is not stored here
         * -- buffer_edit_open() is its path. */
        snprintf(buffer, sizeof(buffer), "%s", config_field_text(f, &draft));
        keyboard_open(f->label, buffer, f->size - 1, false, secret);
    }
    else
    {
        snprintf(buffer, sizeof(buffer), "%ld", (long)config_field_read(f, &draft));
        /* Room for the widest bound plus a sign; the value is clamped on
         * commit anyway, so this only stops absurd typing. */
        keyboard_open(f->label, buffer, 10, true, false);
    }

    /* After keyboard_open(): it goes through overlay_create(), which closes any
     * previous overlay and clears exactly these. */
    edit_field = f;
    edit_row = row;
}

static void buffer_edit_open(lv_obj_t *row, const char *title, char *buffer, size_t size,
                             bool password)
{
    keyboard_open(title, password ? "" : buffer, (uint32_t)(size - 1), false, password);

    edit_buffer = buffer;
    edit_buffer_size = size;
    edit_row = row;
}

/* ---------------------------------------------------------- row handlers */

static void field_row_event(lv_event_t *e)
{
    const struct config_field_s *f =
        (const struct config_field_s *)lv_event_get_user_data(e);
    lv_obj_t *row = (lv_obj_t *)lv_event_get_current_target(e);

    switch (f->kind)
    {
    case SETTINGS_BOOL:
        config_field_write(f, &draft, config_field_read(f, &draft) ? 0 : 1);
        row_refresh(row, f);

        /* Read back rather than inferred, so the sound follows the value even
         * if config_field_write() ever clamps or refuses one. */
        if (config_field_read(f, &draft) != 0)
            BEEPER_EVENT_TOGGLE_ON();
        else
            BEEPER_EVENT_TOGGLE_OFF();
        break;

    case SETTINGS_ENUM:
    {
        /* Cycled rather than picked from a list: the longest enum in the table
         * is four options, so a dropdown and a roller both stay compiled out.
         * It is f->count that decides, so a fifth costs nothing here -- but
         * the tap becomes a worse way to reach the last one with every option
         * added, and somewhere past a handful this wants a list after all. */
        int32_t next = config_field_read(f, &draft) + 1;

        if (next >= (int32_t)f->count)
            next = 0;

        config_field_write(f, &draft, next);
        row_refresh(row, f);
        BEEPER_EVENT_CHANGE();
        break;
    }

    default:
        field_edit_open(row, f);
        break;
    }
}

static void wlan_ssid_row_event(lv_event_t *e)
{
    buffer_edit_open((lv_obj_t *)lv_event_get_current_target(e), "Network (SSID)", wlan_ssid_buf,
                     sizeof(wlan_ssid_buf), false);
}

static void wlan_psk_row_event(lv_event_t *e)
{
    buffer_edit_open((lv_obj_t *)lv_event_get_current_target(e), "Password", wlan_psk_buf,
                     sizeof(wlan_psk_buf), true);
}

static void scan_result_event(lv_event_t *e)
{
    size_t index = (size_t)(uintptr_t)lv_event_get_user_data(e);

    if (index >= scan_result_count)
        return;

    strlcpy(wlan_ssid_buf, scan_results[index].ssid, sizeof(wlan_ssid_buf));

    if (wlan_ssid_row != NULL)
        row_set_value(wlan_ssid_row, wlan_ssid_buf);

    if (scan_results[index].encrypted == false)
    {
        /* Nothing left to ask for -- and the passphrase of whatever was
         * selected before must not be carried over to an open network. */
        wlan_psk_buf[0] = '\0';

        if (wlan_psk_row != NULL)
            row_set_value(wlan_psk_row, "--");

        BEEPER_EVENT_CHANGE();
        return;
    }

    /* The passphrase is the only thing still missing, so go straight there
     * rather than making the user find the row. */
    if (wlan_psk_row != NULL)
        buffer_edit_open(wlan_psk_row, "Password", wlan_psk_buf, sizeof(wlan_psk_buf), true);
}

/* -------------------------------------------------------------- the scan */

static void scan_list_rebuild(void)
{
    if (wlan_scan_list == NULL)
        return;

    lv_obj_clean(wlan_scan_list);

    for (size_t i = 0; i < scan_result_count; i++)
    {
        char        value[VALUE_BUFFER_LEN];
        const char *lock = scan_results[i].encrypted ? "" : "  (open)";

        snprintf(value, sizeof(value), "%u %%%s",
                 openhab_ui_signal_quality(scan_results[i].rssi), lock);

        lv_obj_t *row = row_create(wlan_scan_list, scan_results[i].ssid);
        row_set_value(row, value);
        lv_obj_add_event_cb(row, scan_result_event, LV_EVENT_CLICKED, (void *)(uintptr_t)i);
    }
}

static void scan_status_set(const char *text)
{
    if (tab_status[SETTINGS_TAB_WLAN] != NULL)
        lv_label_set_text(tab_status[SETTINGS_TAB_WLAN], text);
}

/* Keep the strongest sighting of each name and drop the rest: a mesh reports
 * the same SSID once per radio, which would otherwise fill the whole list. */
static void scan_result_insert(const char *ssid, int8_t rssi, bool encrypted)
{
    if (ssid == NULL || ssid[0] == '\0')
        return;

    for (size_t i = 0; i < scan_result_count; i++)
    {
        if (strcmp(scan_results[i].ssid, ssid) != 0)
            continue;

        if (rssi > scan_results[i].rssi)
        {
            scan_results[i].rssi = rssi;
            scan_results[i].encrypted = encrypted;
        }

        return;
    }

    if (scan_result_count >= SCAN_RESULT_MAX)
        return;

    strlcpy(scan_results[scan_result_count].ssid, ssid, WLAN_SSID_SIZE);
    scan_results[scan_result_count].rssi = rssi;
    scan_results[scan_result_count].encrypted = encrypted;
    scan_result_count++;
}

static void scan_results_sort(void)
{
    /* Insertion sort over at most sixteen entries. */
    for (size_t i = 1; i < scan_result_count; i++)
    {
        struct scan_result_s key = scan_results[i];
        size_t               j = i;

        while (j > 0 && scan_results[j - 1].rssi < key.rssi)
        {
            scan_results[j] = scan_results[j - 1];
            j--;
        }

        scan_results[j] = key;
    }
}

static void scan_start(void)
{
    if (scan_running == true)
        return;

    if (port_net_scan_start() == false)
    {
        scan_status_set("Scan failed");
        BEEPER_EVENT_ERROR();
        return;
    }

    scan_running = true;
    scan_status_set("Scanning...");

    /* A request taken, not a value moved: nothing has changed yet, and what
     * the user wants to know is that the panel heard them. */
    BEEPER_EVENT_ACCEPT();
}

static void scan_poll(void)
{
    if (scan_running == false)
        return;

    int found = port_net_scan_poll();

    if (found == PORT_NET_SCAN_RUNNING)
        return;

    scan_running = false;
    scan_result_count = 0;

    if (found < 0)
    {
        /* Seconds after the gesture that asked for it, so a warning rather
         * than an error: nobody is standing over the panel expecting an answer
         * at this instant. */
        scan_status_set("Scan failed");
        BEEPER_EVENT_WARNING();
        port_net_scan_free();
        return;
    }

    for (int i = 0; i < found; i++)
    {
        port_net_ap_t ap;

        if (port_net_scan_result(i, &ap) == true)
            scan_result_insert(ap.ssid, ap.rssi, ap.encrypted);
    }

    port_net_scan_free();
    scan_results_sort();
    scan_list_rebuild();

    char text[VALUE_BUFFER_LEN];
    snprintf(text, sizeof(text), "%u network%s", (unsigned)scan_result_count,
             (scan_result_count == 1) ? "" : "s");
    scan_status_set(text);

    /* This one the user *is* waiting on. */
    BEEPER_EVENT_NOTIFY();
}

static void scan_event(lv_event_t *e)
{
    LV_UNUSED(e);
    scan_start();
}

/* -------------------------------------------------- the list of servers */

/* The rows the two lists fill in, looked up rather than held: the table is
 * const, and a lookup on a page build is a couple of dozen string compares
 * against carrying pointers that two builders would have to keep in step. */
static const struct config_field_s *host_field(void)
{
    return config_field_by_name(SETTINGS_FIELD_HOST);
}

static const struct config_field_s *port_field(void)
{
    return config_field_by_name(SETTINGS_FIELD_PORT);
}

static const struct config_field_s *sitemap_field(void)
{
    return config_field_by_name(SETTINGS_FIELD_SITEMAP);
}

static void server_row_text(size_t index, char *buffer, size_t size)
{
    bool current = (   openhab_discover_port(index) == (uint16_t)draft.openhab.port
                    && strcmp(openhab_discover_host(index), draft.openhab.hostname) == 0);

    /* The label openHAB advertises for itself -- "openhab" out of
     * "openhab._openhab-server._tcp.local" -- with a tick when the rows above
     * already name this one. */
    snprintf(buffer, size, "%s%s", current ? LV_SYMBOL_OK " " : "",
             openhab_discover_label(index));
}

/* The counterpart of sitemap_marks_update(), and it exists for the same
 * reason: this runs from a row's own click handler, where lv_obj_clean() would
 * delete the object whose event is still being dispatched. */
static void server_marks_update(void)
{
    if (server_list_obj == NULL)
        return;

    uint32_t rows = lv_obj_get_child_count(server_list_obj);

    for (uint32_t i = 0; i < rows && i < openhab_discover_count(); i++)
    {
        char text[VALUE_BUFFER_LEN];

        server_row_text(i, text, sizeof(text));
        lv_label_set_text(lv_obj_get_child(lv_obj_get_child(server_list_obj, i), 0), text);
    }
}

static void server_row_event(lv_event_t *e)
{
    size_t                       index = (size_t)(uintptr_t)lv_event_get_user_data(e);
    const struct config_field_s *host = host_field();
    const struct config_field_s *port = port_field();

    if (host == NULL || port == NULL || index >= openhab_discover_count())
        return;

    /* Through the fields, so that an address off the network goes through the
     * same width and the same character rules as one typed on the keyboard --
     * and so that a port outside the row's range is clamped rather than
     * stored. */
    if (config_field_set_text(host, &draft, openhab_discover_host(index)) == false)
    {
        BEEPER_EVENT_ERROR();
        return;
    }

    config_field_set_number(port, &draft, openhab_discover_port(index));

    if (host_field_row != NULL)
        row_refresh(host_field_row, host);

    if (port_field_row != NULL)
        row_refresh(port_field_row, port);

    server_marks_update();
    BEEPER_EVENT_CHANGE();

    /* Nothing asks for the sitemaps here. sitemaps_poll() sees the endpoint
     * move on the next turn of the loop and fetches them from the server just
     * chosen, which is the whole point of that comparison being there. */
}

static void server_list_rebuild(void)
{
    if (server_list_obj == NULL)
        return;

    lv_obj_clean(server_list_obj);

    for (size_t i = 0; i < openhab_discover_count(); i++)
    {
        char text[VALUE_BUFFER_LEN];
        char value[VALUE_BUFFER_LEN];

        server_row_text(i, text, sizeof(text));
        snprintf(value, sizeof(value), "%s:%u", openhab_discover_host(i),
                 (unsigned)openhab_discover_port(i));

        lv_obj_t *row = row_create(server_list_obj, text);

        row_set_value(row, value);
        lv_obj_add_event_cb(row, server_row_event, LV_EVENT_CLICKED, (void *)(uintptr_t)i);
    }
}

static void servers_status_update(void)
{
    if (server_status_label == NULL)
        return;

    /* A heading for the list, which is what the page needs it for -- there are
     * two lists on it -- with the states that are not "here it is" saying so in
     * its place. Short on purpose: it is read at a glance on the way past, and
     * a sentence explaining what mDNS cannot see belongs in the README. */
    const char *text = "Servers";

    switch (openhab_discover_state())
    {
    case OPENHAB_DISCOVER_SCANNING:
        text = "Searching...";
        break;

    case OPENHAB_DISCOVER_READY:
        /* Not a failure, and not worded as one: a server behind an access
         * point that drops multicast is never announced to this panel, and
         * Manual is the answer to it. */
        if (openhab_discover_count() == 0)
            text = "No servers found";
        break;

    case OPENHAB_DISCOVER_FAILED:
        text = "Scan failed";
        break;

    case OPENHAB_DISCOVER_IDLE:
    default:
        break;
    }

    lv_label_set_text(server_status_label, text);
}

static void servers_poll(void)
{
    /* Only while the page that shows it is up, like sitemaps_poll(): the
     * answer to a scan started here must not chime at somebody who has since
     * walked to another page. */
    if (current_tab != SETTINGS_TAB_OPENHAB)
        return;

    if (openhab_discover_revision() == servers_drawn)
        return;

    servers_drawn = openhab_discover_revision();

    server_list_rebuild();
    servers_status_update();

    if (servers_announce == false)
        return;

    if (openhab_discover_state() == OPENHAB_DISCOVER_READY)
    {
        servers_announce = false;

        /* What the WLAN scan says about its own two outcomes, for the same two
         * outcomes: something to show, or nothing. */
        if (openhab_discover_count() > 0)
            BEEPER_EVENT_NOTIFY();
        else
            BEEPER_EVENT_WARNING();
    }
    else if (openhab_discover_state() == OPENHAB_DISCOVER_FAILED)
    {
        servers_announce = false;
        BEEPER_EVENT_WARNING();
    }
}

/* ------------------------------------------------ the list of sitemaps */

/* Ask the server in the *draft* what it serves.
 *
 * The draft and not the saved configuration, which is the whole point: someone
 * who has just typed a new host wants to see that host's sitemaps, and they
 * want to see them before saving -- picking the sitemap is usually why they
 * came here. */
static void sitemaps_request(void)
{
    strlcpy(sitemaps_host, draft.openhab.hostname, sizeof(sitemaps_host));
    sitemaps_port = draft.openhab.port;

    openhab_sitemaps_request(sitemaps_host, (uint16_t)sitemaps_port);
}

static void sitemap_row_text(size_t index, char *buffer, size_t size)
{
    const char *name = openhab_sitemaps_name(index);

    /* The configured one wears a tick. This list is as much a report of what
     * the panel is set to as it is a way of changing it -- and the Sitemap row
     * above shows the name, which on a server with a demo and a demo2 is not
     * enough to see at a glance which of them is selected. */
    snprintf(buffer, size, "%s%s",
             (strcmp(name, draft.openhab.sitemap) == 0) ? LV_SYMBOL_OK " " : "", name);
}

/* Move the tick without rebuilding the list.
 *
 * Called from a row's own click handler, where lv_obj_clean() would delete the
 * object whose event is still being dispatched. */
static void sitemap_marks_update(void)
{
    if (sitemap_list_obj == NULL)
        return;

    uint32_t rows = lv_obj_get_child_count(sitemap_list_obj);

    for (uint32_t i = 0; i < rows && i < openhab_sitemaps_count(); i++)
    {
        char text[VALUE_BUFFER_LEN];

        sitemap_row_text(i, text, sizeof(text));
        lv_label_set_text(lv_obj_get_child(lv_obj_get_child(sitemap_list_obj, i), 0), text);
    }
}

static void sitemap_row_event(lv_event_t *e)
{
    size_t                       index = (size_t)(uintptr_t)lv_event_get_user_data(e);
    const struct config_field_s *f = sitemap_field();

    /* The list can have been replaced between the press and this call -- a
     * refresh that landed in the same frame -- so the index is checked against
     * what is in the cache now rather than against what was drawn. */
    if (f == NULL || index >= openhab_sitemaps_count())
        return;

    /* Through the field rather than into the struct: a name picked from a list
     * goes through the same width and the same character rules as one typed on
     * the keyboard. A server cannot smuggle a '/' into the configuration by
     * calling a sitemap after one. */
    if (config_field_set_text(f, &draft, openhab_sitemaps_name(index)) == false)
    {
        BEEPER_EVENT_ERROR();
        return;
    }

    if (sitemap_field_row != NULL)
        row_refresh(sitemap_field_row, f);

    sitemap_marks_update();
    BEEPER_EVENT_CHANGE();
}

static void sitemap_list_rebuild(void)
{
    if (sitemap_list_obj == NULL)
        return;

    lv_obj_clean(sitemap_list_obj);

    for (size_t i = 0; i < openhab_sitemaps_count(); i++)
    {
        char text[VALUE_BUFFER_LEN];

        sitemap_row_text(i, text, sizeof(text));

        lv_obj_t *row = row_create(sitemap_list_obj, text);

        /* The label, which is what a sitemap is called rather than what it is
         * named. openHAB does not require one; SitemapList falls back to the
         * name, so this column is never blank. */
        row_set_value(row, openhab_sitemaps_label(i));
        lv_obj_add_event_cb(row, sitemap_row_event, LV_EVENT_CLICKED, (void *)(uintptr_t)i);
    }
}

static void sitemaps_status_update(void)
{
    if (sitemap_status_label == NULL)
        return;

    char   text[VALUE_BUFFER_LEN];
    size_t count = openhab_sitemaps_count();
    size_t total = openhab_sitemaps_total();

    /* The same heading as the servers list above, on the same terms. The server
     * these belong to is the ticked row over it and is not repeated here. */
    snprintf(text, sizeof(text), "%s", "Sitemaps");

    switch (openhab_sitemaps_state())
    {
    case OPENHAB_SITEMAPS_FETCHING:
        snprintf(text, sizeof(text), "%s", "Loading...");
        break;

    case OPENHAB_SITEMAPS_READY:
        if (count == 0)
            snprintf(text, sizeof(text), "%s", "No sitemaps");
        else if (total > count)
            /* More than the panel holds. Saying so is the difference between a
             * list that is short and a list that has been cut off; Manual
             * still takes a name that is not on it. */
            snprintf(text, sizeof(text), "Sitemaps (%u of %u)", (unsigned)count,
                     (unsigned)total);
        break;

    case OPENHAB_SITEMAPS_FAILED:
        snprintf(text, sizeof(text), "%s", "No answer from the server");
        break;

    case OPENHAB_SITEMAPS_IDLE:
    default:
        break;
    }

    lv_label_set_text(sitemap_status_label, text);
}

/* Follow the cache, from ui_settings_loop().
 *
 * Nothing here is a timer: the revision moves when the fetch does, and the
 * endpoint comparison is two strings on a page nobody is scrolling. */
static void sitemaps_poll(void)
{
    if (current_tab != SETTINGS_TAB_OPENHAB)
        return;

    /* The endpoint being edited has moved away from the one the list was
     * fetched for -- somebody changed the host or the port -- so the list on
     * screen is another server's and is asked for again. */
    if (   strcmp(draft.openhab.hostname, sitemaps_host) != 0
        || (int)draft.openhab.port != sitemaps_port)
        sitemaps_request();

    if (openhab_sitemaps_revision() == sitemaps_drawn)
        return;

    sitemaps_drawn = openhab_sitemaps_revision();

    sitemap_list_rebuild();
    sitemaps_status_update();

    if (sitemaps_announce == false)
        return;

    /* Only for a fetch the Reload button asked for, and only once it has an
     * answer: FETCHING is the request being taken, which the button already
     * acknowledged. */
    if (openhab_sitemaps_state() == OPENHAB_SITEMAPS_READY)
    {
        sitemaps_announce = false;
        BEEPER_EVENT_NOTIFY();
    }
    else if (openhab_sitemaps_state() == OPENHAB_SITEMAPS_FAILED)
    {
        sitemaps_announce = false;
        BEEPER_EVENT_WARNING();
    }
}

/* One button for both questions this page asks the network.
 *
 * Not two. They are one gesture -- "look again" -- and the difference between
 * them is an implementation detail of which protocol answers which: nobody
 * standing in front of the panel wants to choose between rescanning for
 * servers and re-asking the server it already has. The WLAN page's Scan is the
 * same promise about the same kind of thing. */
/* Into the fields, and out of them again by the bar at the top like every
 * other page. Declared before the footer that uses it and defined beside the
 * Scan it sits next to. */
static void openhab_manual_event(lv_event_t *e)
{
    LV_UNUSED(e);

    openhab_manual = true;

    /* The chime for going a level deeper, which is what this is: the same one
     * the index plays for opening a section. */
    BEEPER_EVENT_LINK();
    screen_show_section(SETTINGS_TAB_OPENHAB);
}

static void openhab_scan_event(lv_event_t *e)
{
    LV_UNUSED(e);

    servers_announce = true;
    openhab_discover_request();

    sitemaps_announce = true;
    sitemaps_request();

    /* A request taken, not a value moved -- the same thing the WLAN scan's
     * button says. */
    BEEPER_EVENT_ACCEPT();
}

/* ------------------------------------------------------------------- save */

static void status_set(uint8_t tab, const char *text)
{
    if (tab < SETTINGS_TAB_COUNT && tab_status[tab] != NULL)
        lv_label_set_text(tab_status[tab], text);
}

static void save_event(lv_event_t *e)
{
    uint8_t     tab = (uint8_t)(uintptr_t)lv_event_get_user_data(e);
    const char *restart_label = NULL;
    bool        restart_needed = settings_restart_needed(&baseline, &draft, &restart_label);

    settings_config->item = draft;

    bool stored = settings_config->saveConfig();

    /* Whether or not the file was written, the draft is now what the running
     * firmware believes, so the next save must not offer a restart for a change
     * this one already applied. */
    baseline = draft;

    settings_apply_live(settings_config);

    if (stored == false)
    {
        /* settings_apply_live() has already run, so the values are in effect --
         * they just will not survive a reboot. saveConfig() returns false when
         * no config file was ever loaded, and always on the simulator, which
         * has no filesystem. No restart is offered either: a reboot is exactly
         * what would lose them. */
        status_set(tab, "Applied, not saved");
        BEEPER_EVENT_ERROR();
        return;
    }

    status_set(tab, "Saved");
    BEEPER_EVENT_ACCEPT();

#if CONFIG_OHEZ_DEBUG_UI_SETTINGS
    debug_printf("ui_settings: saved, restart needed: %d\r\n", (int)restart_needed);
#endif

    if (restart_needed == true)
    {
        char text[120];

        snprintf(text, sizeof(text),
                 "\"%s\" is only read while the device boots.\n\nRestart now?",
                 restart_label);
        confirm_restart_open(text);
    }
}

/* Play the family's signature chime at the volume being edited.
 *
 * The draft and not the saved value: the whole reason this button exists is to
 * let a level be judged before Save commits it, and a slider you cannot hear
 * until after you have kept it is not much of a control. The boot chime is
 * what it plays -- it is each family's longest statement, it is the one chime
 * with a chord in it worth hearing, and it is otherwise only audible by
 * restarting the panel.
 *
 * ui_settings_close() puts the live values back, for the user who drags this
 * to 100, presses Test, and then leaves without saving. */
static void audio_test_event(lv_event_t *e)
{
    LV_UNUSED(e);

    beeper_set_volume((uint8_t)draft.beeper.volume);
    beeper_set_enabled(draft.beeper.enabled);
    ui_beep_set_enabled(draft.beeper.enabled);

    BEEPER_EVENT_BOOT();

    status_set(SETTINGS_TAB_AUDIO, draft.beeper.enabled ? "" : "Beeper is off");
}

/* Say what the Demo button does next, and put the status line with it.
 *
 * Called from the button's own handler and from ui_settings_loop(), because the
 * tune ends by itself: a button that still said "Stop" half a minute after the
 * sound stopped would be lying, and there is no event to hang the correction
 * on. Cheap enough to call every frame -- it compares two strings and usually
 * finds them equal. */
static void audio_demo_refresh(void)
{
    if (audio_demo_button == NULL)
        return;

    bool        playing = beeper_demo_playing();
    const char *want    = playing ? "Stop" : "Demo";
    lv_obj_t   *label   = lv_obj_get_child(audio_demo_button, 0);

    if (label != NULL && strcmp(lv_label_get_text(label), want) != 0)
        lv_label_set_text(label, want);

    if (playing == false && strcmp(lv_label_get_text(tab_status[SETTINGS_TAB_AUDIO]),
                                   "Playing the demo") == 0)
        status_set(SETTINGS_TAB_AUDIO, "");
}

/* Start or stop the half-minute piece that plays the engine's whole vocabulary
 * -- see the note above beeper_demo_available() in beeper_control.hpp.
 *
 * The draft's volume and enable, exactly as Test does and for the same reason:
 * this is the control you judge a level with, and a level you cannot hear until
 * after you have saved it is not a control. ui_settings_close() puts the live
 * values back.
 *
 * Nothing here knows which engine is compiled in. The button is not built at
 * all when there is no tune, so this cannot be reached in that build -- which
 * is the whole point of beeper_demo_available() being a function rather than a
 * macro somebody would have to #if on. */
static void audio_demo_event(lv_event_t *e)
{
    LV_UNUSED(e);

    if (beeper_demo_playing() == true)
    {
        beeper_demo_stop();
        status_set(SETTINGS_TAB_AUDIO, "Stopped");
        audio_demo_refresh();
        return;
    }

    beeper_set_volume((uint8_t)draft.beeper.volume);
    beeper_set_enabled(draft.beeper.enabled);
    ui_beep_set_enabled(draft.beeper.enabled);

    if (beeper_demo_start() == false)
    {
        status_set(SETTINGS_TAB_AUDIO, "Beeper is off");
        return;
    }

    status_set(SETTINGS_TAB_AUDIO, "Playing the demo");
    audio_demo_refresh();
}

static void wlan_save_event(lv_event_t *e)
{
    LV_UNUSED(e);

    if (wlan_set_credentials(wlan_ssid_buf, wlan_psk_buf) == false)
    {
        status_set(SETTINGS_TAB_WLAN, "Needs an SSID");
        BEEPER_EVENT_ERROR();
        return;
    }

    status_set(SETTINGS_TAB_WLAN, "Connecting...");
    BEEPER_EVENT_ACCEPT();
    wlan_state_update();
}

static void restart_event(lv_event_t *e)
{
    LV_UNUSED(e);

    BEEPER_EVENT_SCREEN();
    confirm_restart_open("Restart the device now?");
}

/* -------------------------------------------------------------- WLAN tab */

static void wlan_state_update(void)
{
    if (wlan_state_label == NULL)
        return;

    char text[120];

    const char *state;

    switch (wlan_state())
    {
    case WLAN_ONLINE:
        state = "online";
        break;
    case WLAN_CONNECTING:
        state = "connecting";
        break;
    case WLAN_RETRY_WAIT:
        state = "offline, retrying";
        break;
    case WLAN_PORTAL:
        state = "not configured";
        break;
    default:
        state = "idle";
        break;
    }

    /* One line where it can be: this label competes with the scan results for
     * about five visible rows, and the RSSI and the rest of the addresses are
     * one tab away on Info. */
    if (wlan_state() == WLAN_ONLINE)
    {
        port_net_info_t net;

        port_net_info(&net);
        snprintf(text, sizeof(text), "%s: %s  %s", state,
                 (wlan_sta_ssid()[0] != '\0') ? wlan_sta_ssid() : net.ssid, net.ip);
    }
    else
    {
        snprintf(text, sizeof(text), "%s: %s", state, wlan_sta_ssid());
    }

    /* The setup access point is the only route in on a pristine device, and
     * the banner that used to say so is hidden while this screen is up. */
    const char *ap = wlan_ap_ssid();

    if (ap != NULL)
    {
        uint32_t ip = wlan_ap_ip();

        snprintf(text + strlen(text), sizeof(text) - strlen(text),
                 "\nsetup AP %s  %u.%u.%u.%u", ap, (unsigned)(ip & 0xFF),
                 (unsigned)((ip >> 8) & 0xFF), (unsigned)((ip >> 16) & 0xFF),
                 (unsigned)((ip >> 24) & 0xFF));
    }

    lv_label_set_text(wlan_state_label, text);
}

static void wlan_tab_build(lv_obj_t *rows)
{
    wlan_state_label = lv_label_create(rows);
    lv_label_set_long_mode(wlan_state_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(wlan_state_label, lv_pct(100));
    lv_obj_add_style(wlan_state_label, &ui_style_label_state, LV_PART_MAIN);
    wlan_state_update();

    wlan_ssid_row = row_create(rows, "Network (SSID)");
    row_set_value(wlan_ssid_row, (wlan_ssid_buf[0] == '\0') ? "--" : wlan_ssid_buf);
    lv_obj_add_event_cb(wlan_ssid_row, wlan_ssid_row_event, LV_EVENT_CLICKED, NULL);

    wlan_psk_row = row_create(rows, "Password");
    row_set_value(wlan_psk_row, (wlan_psk_buf[0] == '\0') ? "--" : "*****");
    lv_obj_add_event_cb(wlan_psk_row, wlan_psk_row_event, LV_EVENT_CLICKED, NULL);

    wlan_scan_list = ui_plain_container(rows);
    lv_obj_set_size(wlan_scan_list, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(wlan_scan_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(wlan_scan_list, 2, 0);

    scan_list_rebuild();
}

/* -------------------------------------------------------------- Info tab */

/* One row of the Info table. Returns the next free row, so the table's height
 * follows what was actually added: the eleven rows the openHAB page used to ask
 * for were eleven whether or not the network rows were compiled in, which left
 * the simulator with a blank first row and nine empty grey stripes. */
static uint16_t info_row(lv_obj_t *table, uint16_t row, const char *name, const char *value)
{
    lv_table_set_cell_value(table, row, 0, name);
    lv_table_set_cell_value(table, row, 1, value);

    return row + 1;
}

static void info_tab_build(lv_obj_t *rows)
{
    /* Moved here from openhab_ui.cpp's header_event_handler(), which is what
     * the status bar used to open on its own. */
    lv_obj_t *table = lv_table_create(rows);

    lv_obj_add_style(table, &ui_style_table_cell, LV_PART_ITEMS);
    /* Eleven rows do not fit 240 px, so the scrollbar is on screen and needs a
     * colour of the theme's rather than lv_theme_simple's grey. The slider's
     * indicator colour is the right one to borrow: a scrollbar thumb is the
     * same idea, and for the Material theme it happens to be the very grey
     * lv_theme_simple was supplying. */
    lv_obj_set_style_bg_color(table, lv_color_hex(ui_style_theme()->slider_indic.bg),
                              LV_PART_SCROLLBAR);
    lv_table_set_column_count(table, 2);

    int32_t table_width = lv_display_get_horizontal_resolution(NULL) - 10;
    lv_table_set_column_width(table, 0, table_width * 30 / 100);
    lv_table_set_column_width(table, 1, table_width * 70 / 100);
    lv_obj_set_size(table, lv_pct(100), lv_pct(100));

    char     buffer[50];
    uint16_t row = 0;

    /* port_millis() is the uptime: it is monotonic since boot and nothing
     * resets it, which is the whole of what the Uptime library did. */
    unsigned long long up = (unsigned long long)(port_millis() / 1000);

    snprintf(buffer, sizeof(buffer), "%llu days, %lluh %llum %llus",
             up / 86400, (up / 3600) % 24, (up / 60) % 60, up % 60);
    row = info_row(table, row, "Uptime", buffer);

    snprintf(buffer, sizeof(buffer), "%u.%02u (%s %s)", VERSION_MAJOR, VERSION_MINOR, __DATE__,
             __TIME__);
    row = info_row(table, row, "Version", buffer);

    row = info_row(table, row, "Target", TARGET_NAME);

    port_net_info_t net;

    port_net_info(&net);

    row = info_row(table, row, "Hostname", net.hostname);
    /* "SSID" is the interface name where there is no radio. The row is worth
     * keeping either way: it answers "which network am I on". */
    row = info_row(table, row, "SSID", net.ssid);
    row = info_row(table, row, "BSSID", net.bssid);

    if (net.rssi == PORT_NET_RSSI_WIRED)
        snprintf(buffer, sizeof(buffer), "wired");
    else
        snprintf(buffer, sizeof(buffer), "%i dBm (%u %%)", net.rssi,
                 openhab_ui_signal_quality(net.rssi));

    row = info_row(table, row, "RSSI", buffer);

    row = info_row(table, row, "MAC", net.mac);
    row = info_row(table, row, "IP Addr.", net.ip);
    row = info_row(table, row, "Mask", net.netmask);
    row = info_row(table, row, "Gateway", net.gateway);
    row = info_row(table, row, "DNS", net.dns);

    lv_table_set_row_count(table, row);
}

/* ------------------------------------------------------------- Fonts tab */

/* The nine faces the firmware carries, one line each: three typefaces at the
 * three sizes ui_style.cpp hands out -- captions and table cells, state lines
 * and headers, and the big value labels. Read-only, like Systeminfo, because
 * there is nothing to configure here: the page is what the Theme page's
 * choice looks like before it is made, and the 36 px lines are plain ASCII
 * because that face is the one tools/build_fonts.sh gives no accented
 * letters -- a page about type that drew placeholder boxes would defeat
 * itself. */
static void fonts_tab_build(lv_obj_t *rows)
{
    static const struct
    {
        const char      *family;
        const lv_font_t *small;
        const lv_font_t *normal;
        const lv_font_t *large;
    } faces[] = {
        {"Barlow (Material, Classic)", &custom_font_ui_16,   &custom_font_ui_22,
         &custom_font_ui_36},
        {"Rajdhani (JARVIS)",          &custom_font_hud_16,  &custom_font_hud_22,
         &custom_font_hud_36},
        {"Antonio (LCARS)",            &custom_font_lcars_16, &custom_font_lcars_22,
         &custom_font_lcars_36},
    };

    for (size_t f = 0; f < sizeof(faces) / sizeof(faces[0]); f++)
    {
        /* A rule before every family but the first, so the three blocks read
         * as three fonts rather than as one list of nine lines. The scrollbar
         * grey rather than a border, because a hairline that asks for a
         * colour of its own is a hairline that is wrong in one of the themes. */
        if (f > 0)
        {
            lv_obj_t *rule = lv_obj_create(rows);

            lv_obj_remove_flag(rule, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_set_size(rule, lv_pct(100), 2);
            lv_obj_set_style_bg_color(rule, lv_color_hex(ui_style_theme()->slider_indic.bg), 0);
            lv_obj_set_style_border_width(rule, 0, 0);
            lv_obj_set_style_radius(rule, 0, 0);
            lv_obj_set_style_pad_all(rule, 0, 0);
        }

        lv_obj_t *heading = lv_label_create(rows);

        lv_label_set_text(heading, faces[f].family);
        lv_obj_add_style(heading, &ui_style_label, LV_PART_MAIN);
        lv_obj_set_style_pad_top(heading, 2, 0);

        const lv_font_t *fonts[] = {faces[f].small, faces[f].normal, faces[f].large};
        static const uint8_t sizes[] = {16, 22, 36};

        for (size_t i = 0; i < sizeof(fonts) / sizeof(fonts[0]); i++)
        {
            char text[40];

            snprintf(text, sizeof(text), "%u px: The quick brown fox", (unsigned)sizes[i]);

            lv_obj_t *sample = lv_label_create(rows);

            lv_label_set_text(sample, text);
            lv_label_set_long_mode(sample, LV_LABEL_LONG_DOT);
            lv_obj_set_width(sample, lv_pct(100));
            lv_obj_set_style_text_font(sample, fonts[i], 0);
        }
    }
}

/* ------------------------------------------------------------- Icons tab */

/* How many rows of cells one page of the catalogue holds. Measured against
 * the panel rather than fixed, the way the rest of the chrome is: the caption
 * under each icon is what makes a cell taller than the icon alone. */
static uint8_t icons_page_rows(void)
{
    const lv_font_t *caption = ui_style_theme()->font_small;
    int32_t          vres = lv_display_get_vertical_resolution(NULL);

    /* The rows container's pad_all(4) -- screen_show_section() sets it -- at
     * top and bottom, so that a page fills the area without scrolling. */
    int16_t area_h = (int16_t)(vres - BAR_HEIGHT - FOOTER_HEIGHT - 2 * 4);
    int16_t cell_h = (int16_t)(ICON_SET_PIXEL_SIZE + lv_font_get_line_height(caption));
    int16_t rows = (int16_t)((area_h + ICON_PAGE_GAP) / (cell_h + ICON_PAGE_GAP));

    /* A panel too short for a row still gets the page: the one row the
     * container then scrolls is the old behaviour for a page's worth of
     * icons, and better than a page with nothing on it. */
    if (rows < 1)
        rows = 1;

    return (uint8_t)rows;
}

/* How many pages the catalogue takes. Counted the way the page below builds:
 * base icons only -- the state variants are the same art at other states and
 * are found through the base name (see icon_set_get()), and the generator
 * writes no base name with a hyphen in it, so one strchr() is the whole test.
 *
 * Walked every time rather than cached: a few hundred strchr()s over names
 * in flash, against a static that one page builds and a theme rebuild --
 * which is not a navigation -- would have to remember to leave alone. */
static uint16_t icons_pages(void)
{
    size_t per_page = (size_t)ICON_PAGE_COLS * icons_page_rows();
    size_t base = 0;

    for (size_t i = 0; i < icon_set_count(); i++)
    {
        const char *name = icon_set_name(i);

        if (name == NULL)
            break;

        if (strchr(name, '-') == NULL)
            base++;
    }

    return (uint16_t)((base + per_page - 1) / per_page);
}

/* The footer's four page-turning buttons. One callback with the direction
 * the button carries: the four differ only in which way and how far, and
 * four copies of the same handler would be four ways of saying so. */
static void icons_page_event(lv_event_t *e)
{
    enum icons_page_e dir = (enum icons_page_e)(uintptr_t)lv_event_get_user_data(e);
    uint16_t          pages = icons_pages();
    uint16_t          page = icons_page;

    if (dir == ICONS_PAGE_FIRST)
        page = 0;
    else if (dir == ICONS_PAGE_PREV)
        page = (page > 0) ? (uint16_t)(page - 1) : 0;
    else if (dir == ICONS_PAGE_NEXT)
        page = (uint16_t)(page + 1);
    else
        page = (uint16_t)(pages - 1);

    /* The ends are the ends. A button that cannot move the page any further
     * stays quiet rather than rebuilding what is already showing. */
    if (page >= pages)
        page = (uint16_t)(pages - 1);

    if (page == icons_page)
        return;

    icons_page = page;

    BEEPER_EVENT_TICK();

    /* The whole section rather than the grid alone, the same route the
     * openHAB page's Manual button takes: the footer carries the page number
     * and the buttons themselves, and the shared path is what keeps every
     * pointer -- the descriptors included -- correct after the delete. */
    screen_show_section(SETTINGS_TAB_ICONS);
}

/* One page of the base icons the firmware carries, one cell each: the
 * picture and the name it is looked up by. Which page of them is
 * icons_page, turned by the footer's buttons; the variant test the count
 * above makes is made again here, on the same grounds.
 *
 * Read-only like the rest of this menu, and like the Fonts page it says so
 * when there is nothing to show: the icon set is a generated file that is
 * deliberately not in the repository (see icon_set.hpp), and a build without
 * it fetches every icon from the server -- which is worth a sentence on the
 * page rather than an empty page. */
static void icons_tab_build(lv_obj_t *rows)
{
    size_t count = icon_set_count();

    if (count == 0)
    {
        lv_obj_t *note = lv_label_create(rows);

        lv_label_set_text(note,
                          "No built-in icon set in this build. Icons are fetched from "
                          "the server; run tools/build_icon_set.py to compile them in.");
        lv_label_set_long_mode(note, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(note, lv_pct(100));
        return;
    }

    uint16_t pages = icons_pages();

    /* The clamp matters a theme change from now: the row count follows the
     * caption's face, and a page that no longer exists is not the one to be
     * showing when the rebuild settles. The nearest page, not the first --
     * a theme change is not a navigation either. */
    if (icons_page >= pages)
        icons_page = (uint16_t)(pages - 1);

    int32_t hres = lv_display_get_horizontal_resolution(NULL);
    int32_t vres = lv_display_get_vertical_resolution(NULL);

    uint8_t page_rows = icons_page_rows();
    size_t  per_page = (size_t)ICON_PAGE_COLS * page_rows;
    size_t  first = (size_t)icons_page * per_page;
    size_t  shown = 0; /* base icons walked past, this page's included */
    size_t  built = 0; /* cells put on this page */

    icon_dscs = (lv_image_dsc_t *)calloc(per_page, sizeof(lv_image_dsc_t));

    if (icon_dscs == NULL)
    {
        status_set(SETTINGS_TAB_ICONS, "Not enough memory");
        return;
    }

    /* A grid of cells at computed positions rather than a flex wrap of
     * content-sized ones: the page has to know how many cells it holds before
     * it builds any of them, and the solver the menus use answers that --
     * reading the container back would give zero, because v9 has not laid it
     * out yet. */
    lv_obj_t *grid = ui_plain_container(rows);

    int16_t area_w = (int16_t)(hres - 2 * 4);
    int16_t area_h = (int16_t)(vres - BAR_HEIGHT - FOOTER_HEIGHT - 2 * 4);

    lv_obj_set_size(grid, area_w, area_h);

    struct ui_grid_s layout = {ICON_PAGE_COLS, page_rows, ICON_PAGE_GAP, 0};

    for (size_t i = 0; i < count && built < per_page; i++)
    {
        size_t                size = 0;
        const unsigned char  *data = icon_set_entry(i, &size);
        const char           *name = icon_set_name(i);

        if (data == NULL || name == NULL)
            break;

        /* A variant of one of these, not an icon of its own. */
        if (strchr(name, '-') != NULL)
            continue;

        /* An earlier page's icon: walked past, not built. */
        if (shown < first)
        {
            shown++;
            continue;
        }

        struct ui_geom_rect_s r;

        if (ui_grid_cell(&layout, area_w, area_h, (uint8_t)built, &r) == false)
            break;

        /* The same descriptor every tile's built-in icon uses, by the same
         * route widget_icon_decode_and_show() builds its -- see there. */
        icon_dscs[built].header.magic = LV_IMAGE_HEADER_MAGIC;
        icon_dscs[built].header.cf = LV_COLOR_FORMAT_I4;
        icon_dscs[built].header.flags = 0;
        icon_dscs[built].header.w = ICON_SET_PIXEL_SIZE;
        icon_dscs[built].header.h = ICON_SET_PIXEL_SIZE;
        icon_dscs[built].header.stride = ICON_SET_STRIDE;
        icon_dscs[built].data_size = (uint32_t)size;
        icon_dscs[built].data = data;

        lv_obj_t *cell = ui_plain_container(grid);

        lv_obj_set_pos(cell, r.x, r.y);
        lv_obj_set_size(cell, r.w, r.h);
        lv_obj_set_flex_flow(cell, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(cell, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);

        lv_obj_t *icon = lv_image_create(cell);

        lv_image_set_src(icon, &icon_dscs[built]);

        lv_obj_t *caption = lv_label_create(cell);

        lv_label_set_text(caption, name);
        lv_label_set_long_mode(caption, LV_LABEL_LONG_DOT);
        lv_obj_set_width(caption, lv_pct(100));
        lv_obj_set_style_text_align(caption, LV_TEXT_ALIGN_CENTER, 0);

        shown++;
        built++;
    }

    /* The page number beside the buttons that move it. Not on a one-page
     * catalogue: with nothing to turn there is nothing to number. */
    if (pages > 1)
    {
        char text[24];

        snprintf(text, sizeof(text), "Page %u of %u", (unsigned)icons_page + 1u,
                 (unsigned)pages);
        status_set(SETTINGS_TAB_ICONS, text);
    }
}

/* ------------------------------------------------------------ the screen */

/* A footer that stays put rather than one inside the scroll area: Save is the
 * one control that has to be reachable whatever the list is showing. */
/* A section heading only earns one of the visible lines where the tab holds
 * more than one section: "Sensors" above the only group of the Sensors tab
 * says nothing the bar has not already said. */
static uint8_t tab_section_count(uint8_t tab)
{
    uint8_t count = 0;

    for (size_t i = 0; i < config_field_count; i++)
        if (config_fields[i].kind == SETTINGS_SECTION && config_field_tab(i) == tab)
            count++;

    return count;
}

/* The Sensors, Other and manual openHAB pages, straight off the shared table. */
static void field_rows_build(uint8_t tab)
{
    lv_obj_t *rows = tab_rows[tab];
    bool      headings = tab_section_count(tab) > 1;

    for (size_t i = 0; i < config_field_count; i++)
    {
        const struct config_field_s *f = &config_fields[i];

        if (config_field_tab(i) != tab)
            continue;

        if (f->kind == SETTINGS_SECTION)
        {
            if (headings == true)
            {
                lv_obj_t *heading = lv_label_create(rows);

                lv_label_set_text(heading, f->label);
                /* The caption font, not the state font: a 22 px heading costs
                 * most of a row, and this list has about five. */
                lv_obj_add_style(heading, &ui_style_label, LV_PART_MAIN);
                lv_obj_set_style_pad_top(heading, 2, 0);
            }

            continue;
        }

        lv_obj_t *row = row_create(rows, f->label);

        row_refresh(row, f);
        lv_obj_add_event_cb(row, field_row_event, LV_EVENT_CLICKED, (void *)f);

        /* Kept so that a choice made in one of the lists below can refresh the
         * row that now holds it. */
        if (f == host_field())
            host_field_row = row;
        else if (f == port_field())
            port_field_row = row;
        else if (f == sitemap_field())
            sitemap_field_row = row;
    }
}

/* The servers half of the openHAB page. */
static void server_rows_build(lv_obj_t *rows)
{
    server_status_label = lv_label_create(rows);
    lv_label_set_long_mode(server_status_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(server_status_label, lv_pct(100));
    lv_obj_add_style(server_status_label, &ui_style_label_state, LV_PART_MAIN);

    server_list_obj = ui_plain_container(rows);
    lv_obj_set_size(server_list_obj, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(server_list_obj, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(server_list_obj, 2, 0);
}

/* The openHAB section: two lists, and nothing to type.
 *
 * The servers on the network, then the sitemaps the selected one serves, each
 * under a line saying what is being looked at. That is the whole page -- the
 * Host, Port and Sitemap rows moved behind the Manual button in the footer,
 * because the two lists *are* those three fields for anybody whose openHAB
 * announces itself, and a page carrying both was three rows, two headings and
 * two lists on a screen 240 px tall.
 *
 * Both fetches are started here and not by a button, because opening this page
 * is the gesture: the one thing anybody comes to it for is to point the panel
 * at a server and a sitemap, and a list that has to be asked for is a list most
 * people will never see. The footer keeps a Scan for the server that was not up
 * a moment ago. */
static void openhab_tab_build(lv_obj_t *rows)
{
    /* Behind the Manual button: the three fields and nothing else, for the
     * server that does not announce itself, the sitemap that is not on the
     * list because it has not been written yet, and the port that is not the
     * usual one. */
    if (openhab_manual == true)
    {
        field_rows_build(SETTINGS_TAB_OPENHAB);
        return;
    }

    server_rows_build(rows);

    sitemap_status_label = lv_label_create(rows);
    lv_label_set_long_mode(sitemap_status_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(sitemap_status_label, lv_pct(100));
    lv_obj_add_style(sitemap_status_label, &ui_style_label_state, LV_PART_MAIN);

    sitemap_list_obj = ui_plain_container(rows);
    lv_obj_set_size(sitemap_list_obj, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(sitemap_list_obj, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(sitemap_list_obj, 2, 0);

    /* Whatever the cache already holds is drawn at once -- a page opened a
     * second time, or after the web form asked the same question, has its list
     * immediately -- and the refresh below replaces it when the answer comes.
     *
     * sitemaps_request() before the draw, so that the status line names the
     * host the list is being fetched for rather than the one it last was. */
    sitemaps_announce = false;
    sitemaps_request();
    sitemaps_drawn = openhab_sitemaps_revision();
    sitemap_list_rebuild();
    sitemaps_status_update();

    /* And the scan, for the same reason and on the same terms: opening this
     * page is the gesture. It costs one 44 byte datagram, and a panel being
     * configured for the first time is exactly where "which server?" is the
     * question with no good answer to type. */
    servers_announce = false;
    openhab_discover_request();
    servers_drawn = openhab_discover_revision();
    server_list_rebuild();
    servers_status_update();
}

/* The bar across the top of every settings screen, and all of it is the way
 * back -- the same affordance the item screens use, for the same reason. From
 * a section it returns to the menu that lists it, from a menu to the menu
 * above, and from the root menu it closes. */
static void back_event(lv_event_t *e)
{
    LV_UNUSED(e);

    if (current_tab == MENU_ROOT)
    {
        /* ui_settings_close() plays SCREEN_OUT for itself: leaving the
         * settings screen is a surface uncovering, and the two steps before it
         * are navigation inside one. */
        ui_settings_close();
    }
    else if (current_tab == SETTINGS_TAB_OPENHAB && openhab_manual == true)
    {
        /* One level inside a section rather than out of it: Back from the
         * fields is the lists they were reached from. */
        openhab_manual = false;
        BEEPER_EVENT_LINK_BACK();
        screen_show_section(SETTINGS_TAB_OPENHAB);
    }
    else if (MENU_IS(current_tab))
    {
        BEEPER_EVENT_LINK_BACK();
        screen_show_menu(MENU_AT(current_tab)->parent);
    }
    else
    {
        BEEPER_EVENT_LINK_BACK();
        screen_show_menu(menu_of(current_tab));
    }
}

/* A close glyph at the root of the index, a chevron everywhere else: the bar
 * shows where back actually goes, and from the root that is out. */
static void back_bar_create(const char *title)
{
    ui_back_bar(screen, (current_tab == MENU_ROOT) ? LV_SYMBOL_CLOSE : LV_SYMBOL_LEFT,
                title, back_event);
}

static void index_event(lv_event_t *e)
{
    /* Arriving at a section is arriving at its first page -- the openHAB
     * manual page and the Icons catalogue's page alike. Not in
     * screen_show_target(), which is also what ui_settings_rebuild() goes
     * through: a theme change -- including the automatic night one, at any
     * moment -- must leave the page it happens on where it was. */
    openhab_manual = false;
    icons_page = 0;

    /* Here and not in screen_show_target(): that function is also the
     * programmatic entry from ui_settings_open(), which is how a pristine
     * device lands on the WLAN tab with nobody having touched anything. */
    BEEPER_EVENT_LINK();
    screen_show_target((uint8_t)(uintptr_t)lv_event_get_user_data(e));
}

/* A menu: its entries, all visible at once, all comfortably bigger than a
 * fingertip. This replaces a bar of six symbol-only tab buttons 32 px tall,
 * which had to be read as pictograms and hit as a sixth of the screen width.
 *
 * The row count follows the entry count rather than being fixed at three, so
 * a menu fills its screen instead of leaving a blank third at the bottom.
 * That does mean the cells change size between the two menus -- 151 x 89 on
 * the root's four, 151 x 53 on System's six -- which is the price of neither
 * screen looking half-finished. Both are well over the 44 px floor. */
static void screen_show_menu(uint8_t menu)
{
    const struct menu_s *m;

    if (MENU_IS(menu) == false)
        return;

    m = MENU_AT(menu);

    /* A menu is never the Audio page, so this always leaves it. */
    beeper_demo_stop();

    lv_obj_clean(screen);
    current_tab = menu;

    widget_refs_clear();

    back_bar_create(m->title);

    int32_t hres = lv_display_get_horizontal_resolution(NULL);
    int32_t vres = lv_display_get_vertical_resolution(NULL);

    lv_obj_t *grid = ui_plain_container(screen);

    lv_obj_set_pos(grid, INDEX_GAP, BAR_HEIGHT + INDEX_GAP);
    lv_obj_set_size(grid, hres - 2 * INDEX_GAP, vres - BAR_HEIGHT - 2 * INDEX_GAP);

    /* The same solver the tile grid uses, and for the same reason: reading the
     * container back would give zero, because v9 has not laid it out yet. */
    struct ui_grid_s layout = {INDEX_COLS, INDEX_ROWS(m->count), INDEX_GAP, 0};
    int16_t          area_w = (int16_t)(hres - 2 * INDEX_GAP);
    int16_t          area_h = (int16_t)(vres - BAR_HEIGHT - 2 * INDEX_GAP);

    for (uint8_t i = 0; i < m->count; i++)
    {
        uint8_t               target = m->entries[i];
        struct ui_geom_rect_s r;

        if (ui_grid_cell(&layout, area_w, area_h, i, &r) == false)
            break;

        /* An empty label rather than NULL: lv_label_set_text(NULL) leaves the
         * widget's default "Text" behind. The two labels below are the
         * content; this one only exists because ui_themed_button() makes it. */
        lv_obj_t *cell = ui_themed_button(grid, "");

        lv_obj_set_pos(cell, r.x, r.y);
        lv_obj_set_size(cell, r.w, r.h);
        lv_obj_set_style_pad_all(cell, 4, 0);
        lv_obj_add_event_cb(cell, index_event, LV_EVENT_CLICKED, (void *)(uintptr_t)target);

        lv_obj_t *glyph = lv_label_create(cell);

        lv_label_set_text(glyph, target_symbol(target));
        lv_obj_align(glyph, LV_ALIGN_LEFT_MID, 8, 0);

        lv_obj_t *name = lv_label_create(cell);

        lv_label_set_text(name, target_title(target));
        lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
        lv_obj_set_width(name, r.w - 46);
        lv_obj_align(name, LV_ALIGN_LEFT_MID, 36, 0);
    }

    ui_motion_enter(grid);
}

/* One section: the bar, a scrolling list of rows, and a footer carrying
 * whatever that section can do. */
static void screen_show_section(uint8_t tab)
{
    if (tab >= SETTINGS_TAB_COUNT)
        return;

    /* The demonstration belongs to the page its button is on: this is about to
     * delete that button, and a tune still playing with nothing left to stop it
     * is the thing to avoid.
     *
     * Conditional, and that is the whole reason this is not simply at the top
     * of both builders: rebuilding the Audio page is how a *theme change*
     * reaches it, and the automatic night schedule can ask for one at any
     * moment. Stopping the music because the clock crossed eight is not
     * something anybody would connect to a cause. */
    if (tab != SETTINGS_TAB_AUDIO)
        beeper_demo_stop();

    lv_obj_clean(screen);
    current_tab = tab;

    widget_refs_clear();

    /* The manual page is the one screen whose title is not its section's: it
     * is a page of the openHAB section rather than the section itself, and a
     * bar that said "openHAB" on both would leave Back looking like it had
     * done nothing. */
    back_bar_create((tab == SETTINGS_TAB_OPENHAB && openhab_manual == true) ? "openHAB Server"
                                                                            : target_title(tab));

    int32_t vres = lv_display_get_vertical_resolution(NULL);

    lv_obj_t *rows = ui_plain_container(screen);

    lv_obj_set_pos(rows, 0, BAR_HEIGHT);
    lv_obj_set_size(rows, lv_pct(100), vres - BAR_HEIGHT - FOOTER_HEIGHT);
    lv_obj_add_flag(rows, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(rows, LV_DIR_VER);
    lv_obj_set_flex_flow(rows, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(rows, 4, 0);
    lv_obj_set_style_pad_row(rows, 4, 0);
    lv_obj_set_style_bg_color(rows, lv_color_hex(ui_style_theme()->slider_indic.bg),
                              LV_PART_SCROLLBAR);

    tab_rows[tab] = rows;

    lv_obj_t *footer = lv_obj_create(screen);

    lv_obj_remove_flag(footer, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(footer, 0, vres - FOOTER_HEIGHT);
    lv_obj_set_size(footer, lv_pct(100), FOOTER_HEIGHT);
    lv_obj_add_style(footer, &ui_style_win_header, LV_PART_MAIN);
    lv_obj_set_style_radius(footer, 0, 0);
    lv_obj_set_flex_flow(footer, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(footer, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_hor(footer, 6, 0);
    lv_obj_set_style_pad_ver(footer, 0, 0);
    lv_obj_set_style_pad_column(footer, 4, 0);

    /* At rest this is empty -- the bar above already names the section, which
     * is the job this label used to do for the symbol-only tab buttons. An
     * action's result fills it until the user leaves. */
    tab_status[tab] = lv_label_create(footer);
    lv_label_set_text(tab_status[tab], "");
    lv_label_set_long_mode(tab_status[tab], LV_LABEL_LONG_DOT);
    lv_obj_set_flex_grow(tab_status[tab], 1);
    /* DOT only writes its dots in the last line that still fits the height, so
     * at LV_SIZE_CONTENT it never dots at all: it wraps, grows a second line,
     * and the longest message ("Applied, not saved") spills out of a bar that
     * neither scrolls nor clips. */
    lv_obj_set_height(tab_status[tab], lv_font_get_line_height(ui_style_theme()->font_normal));

    if (tab == SETTINGS_TAB_INFO)
    {
        lv_obj_add_event_cb(ui_themed_button(footer, "Restart"), restart_event, LV_EVENT_CLICKED,
                            NULL);
    }
    else if (tab == SETTINGS_TAB_FONTS)
    {
        /* Read-only, like Systeminfo, and there is nothing to save: the rows
         * on it are samples, not fields. */
    }
    else if (tab == SETTINGS_TAB_ICONS)
    {
        /* The catalogue's pages, turned from the footer because the footer is
         * where this screen keeps every action its page can take. Only when
         * there is more than one of them: a single-page catalogue -- and a
         * build without the icon set at all, which gets the note instead --
         * is a page with nothing to turn. */
        if (icons_pages() > 1)
        {
            lv_obj_add_event_cb(ui_themed_button(footer, "<<"), icons_page_event,
                                LV_EVENT_CLICKED, (void *)(uintptr_t)ICONS_PAGE_FIRST);
            lv_obj_add_event_cb(ui_themed_button(footer, "<"), icons_page_event,
                                LV_EVENT_CLICKED, (void *)(uintptr_t)ICONS_PAGE_PREV);
            lv_obj_add_event_cb(ui_themed_button(footer, ">"), icons_page_event,
                                LV_EVENT_CLICKED, (void *)(uintptr_t)ICONS_PAGE_NEXT);
            lv_obj_add_event_cb(ui_themed_button(footer, ">>"), icons_page_event,
                                LV_EVENT_CLICKED, (void *)(uintptr_t)ICONS_PAGE_LAST);
        }
    }
    else if (tab == SETTINGS_TAB_AUDIO)
    {
        lv_obj_add_event_cb(ui_themed_button(footer, "Test"), audio_test_event,
                            LV_EVENT_CLICKED, NULL);

        /* Only where there is something to demonstrate. The polyphonic engine
         * has no tune, and a button that reported "nothing to play" would be
         * worse than an absent one -- see beeper_song.h. */
        if (beeper_demo_available() == true)
        {
            audio_demo_button = ui_themed_button(footer, "Demo");

            lv_obj_add_event_cb(audio_demo_button, audio_demo_event,
                                LV_EVENT_CLICKED, NULL);

            /* Built mid-tune if the user left the page and came back: the
             * label has to arrive saying Stop. */
            audio_demo_refresh();
        }

        lv_obj_add_event_cb(ui_themed_button(footer, "Save"), save_event, LV_EVENT_CLICKED,
                            (void *)(uintptr_t)tab);
    }
    else if (tab == SETTINGS_TAB_TOUCH)
    {
        /* Only where there is something to calibrate. A capacitive panel
         * reports the pixel grid it is bonded to; there is no origin and no
         * span, and a procedure offered anyway could only write four numbers
         * that the pointer then ignores. The rows stay, because a config.json
         * is read by whichever board it lands on. */
        if (port_indev_calibratable() == true)
        {
            lv_obj_add_event_cb(ui_themed_button(footer, "Calibrate"), calibrate_event,
                                LV_EVENT_CLICKED, NULL);
        }
        else
        {
            /* Said rather than left to be inferred from a missing button. The
             * rows above are still there and still editable, because a
             * config.json is read by whichever board it lands on. */
            status_set(tab, "This panel needs none");
        }

        lv_obj_add_event_cb(ui_themed_button(footer, "Save"), save_event, LV_EVENT_CLICKED,
                            (void *)(uintptr_t)tab);
    }
    else if (tab == SETTINGS_TAB_WLAN)
    {
        lv_obj_add_event_cb(ui_themed_button(footer, "Scan"), scan_event, LV_EVENT_CLICKED, NULL);
        lv_obj_add_event_cb(ui_themed_button(footer, "Save"), wlan_save_event, LV_EVENT_CLICKED,
                            NULL);
    }
    else if (tab == SETTINGS_TAB_OPENHAB && openhab_manual == false)
    {
        /* Scan is the counterpart of the WLAN page's, and there for the same
         * cases: a server that was still starting when the page opened, or a
         * sitemap that has only just been written. Manual is the way to the
         * fields, for everything the network did not offer. */
        lv_obj_add_event_cb(ui_themed_button(footer, "Scan"), openhab_scan_event,
                            LV_EVENT_CLICKED, NULL);
        lv_obj_add_event_cb(ui_themed_button(footer, "Manual"), openhab_manual_event,
                            LV_EVENT_CLICKED, NULL);
        lv_obj_add_event_cb(ui_themed_button(footer, "Save"), save_event, LV_EVENT_CLICKED,
                            (void *)(uintptr_t)tab);
    }
    else
    {
        lv_obj_add_event_cb(ui_themed_button(footer, "Save"), save_event, LV_EVENT_CLICKED,
                            (void *)(uintptr_t)tab);
    }

    switch (tab)
    {
    case SETTINGS_TAB_WLAN:
        wlan_tab_build(rows);
        break;

    case SETTINGS_TAB_OPENHAB:
        openhab_tab_build(rows);
        break;

    case SETTINGS_TAB_INFO:
        info_tab_build(rows);
        break;

    case SETTINGS_TAB_FONTS:
        fonts_tab_build(rows);
        break;

    case SETTINGS_TAB_ICONS:
        icons_tab_build(rows);
        break;

    default:
        field_rows_build(tab);
        break;
    }

    ui_motion_enter(rows);
}

/* ------------------------------------------------------------------- API */

void ui_settings_setup(Config *config)
{
    settings_config = config;
}

void ui_settings_open(enum settings_tab_e tab)
{
    /* SETTINGS_TAB_COUNT is not out of range here: it is MENU_ROOT. */
    if (settings_config == NULL || tab > SETTINGS_TAB_COUNT)
        return;

    if (screen != NULL)
    {
        /* Already up: treat this as a request for that section, the way the
         * single open_window slot in openhab_ui.cpp stopped a second window
         * stacking -- and arriving at the Icons catalogue is arriving at its
         * first page, the same as every way of arriving below. */
        icons_page = 0;
        screen_show_target(tab);
        return;
    }

    draft = settings_config->item;
    baseline = draft;

    wlan_ssid_buf[0] = '\0';
    wlan_psk_buf[0] = '\0';
    /* The passphrase is deliberately left blank rather than prefilled, as in
     * the web form -- it is never shown back to anyone. */
    char stored_psk[WLAN_PSK_SIZE];
    wlan_credentials_get(wlan_ssid_buf, sizeof(wlan_ssid_buf), stored_psk, sizeof(stored_psk));

    scan_result_count = 0;
    scan_running = false;
    openhab_manual = false;
    icons_page = 0;

    screen = ui_screen_create();

    /* Straight to the section a caller named -- a pristine device is sent to
     * WLAN, which is now two levels down and which it should certainly not
     * have to find -- and to the root menu when the user asked for "settings"
     * rather than for something in particular. */
    screen_show_target(tab);

    ui_screen_push(screen, UI_SCREEN_SETTINGS, SETTINGS_ANIM_MS);

#if CONFIG_OHEZ_DEBUG_UI_SETTINGS
    debug_printf("ui_settings: opened on tab %u\r\n", (unsigned)tab);
#endif
}

void ui_settings_close(void)
{
    if (screen == NULL)
        return;

    overlay_close();

    /* ui_screen_pop() owns the delete: it is reached from an event on one of
     * this screen's own descendants, so the object has to outlive the handler. */
    ui_screen_pop(SETTINGS_ANIM_MS);

    screen = NULL;
    current_tab = SETTINGS_TAB_COUNT;
    rebuild_pending = false;

    widget_refs_clear();

    /* Before the chime, not after: ui_beep_play() drops everything while the
     * demonstration is sounding -- see the note there -- so closing the screen
     * on a playing tune would otherwise swallow its own closing sound.
     *
     * Usually already done, because the Audio page's back bar goes to the
     * settings root first and screen_show_menu() stops it there. This is for
     * the programmatic close, which has no back bar to go through. */
    beeper_demo_stop();

    BEEPER_EVENT_SCREEN_OUT();

    /* Undo whatever Test applied out of the draft. A no-op unless the Audio
     * page was visited, since these are the values already in force -- and
     * after Save they are the draft's anyway, because settings_apply_live()
     * has been through by then. */
    if (settings_config != NULL)
    {
        beeper_set_volume((uint8_t)settings_config->item.beeper.volume);
        beeper_set_enabled(settings_config->item.beeper.enabled);
        ui_beep_set_enabled(settings_config->item.beeper.enabled);
    }
}

const char *ui_settings_page_name(void)
{
    if (ui_settings_is_open() == false)
        return NULL;

    /* The one page that is not its section: a script that walked into the
     * fields should be told so, the same way the bar above them says so. */
    if (current_tab == SETTINGS_TAB_OPENHAB && openhab_manual == true)
        return "openHAB Server";

    return target_title(current_tab);
}

bool ui_settings_is_open(void)
{
    return screen != NULL;
}

void ui_settings_rebuild(void)
{
    if (screen == NULL)
        return;

    /* Not while the keyboard or the restart prompt is up. A theme change is not
     * always something the user just asked for: the automatic night schedule
     * requests one from openhab_ui_loop() whenever the clock crosses its
     * boundary, which can land in the middle of typing a passphrase or on top
     * of an unanswered "restart now?". Rebuilding then would throw either away.
     *
     * ui_settings_loop() picks this up once the overlay is gone. It runs from
     * loop() right after lv_timer_handler(), so by then the overlay's deferred
     * deletion has actually happened and there is nothing left to clean. */
    if (overlay != NULL)
    {
        rebuild_pending = true;
        return;
    }

    rebuild_pending = false;

    screen_show_target(current_tab);
}

#if CONFIG_IDF_TARGET_LINUX || CONFIG_OHEZ_TESTIF
bool ui_settings_open_by_name(const char *name)
{
    if (name == NULL)
        return false;

    for (uint8_t i = 0; i < SETTINGS_TAB_COUNT; i++)
    {
        if (strcasecmp(name, tab_title[i]) != 0)
            continue;

        ui_settings_open((enum settings_tab_e)i);
        return true;
    }

    /* Then the menus, by name -- so "system" reaches the System menu, which is
     * otherwise a tap in and unreachable from a script. */
    for (uint8_t m = 0; m < MENU_COUNT; m++)
    {
        bool named = strcasecmp(name, menus[m].title) == 0;

        /* What this took before the menus had names of their own. */
        if (m == 0 && strcasecmp(name, "index") == 0)
            named = true;

        if (named == false)
            continue;

        ui_settings_open(SETTINGS_TAB_COUNT);

        if (ui_settings_is_open())
            screen_show_menu((uint8_t)(SETTINGS_TAB_COUNT + m));

        return true;
    }

    return false;
}
#endif /* CONFIG_IDF_TARGET_LINUX || CONFIG_OHEZ_TESTIF */

#if CONFIG_IDF_TARGET_LINUX
void ui_settings_open_from_env(void)
{
    const char *name = getenv("OHEZ_SETTINGS");

    if (name == NULL)
        return;

    if (ui_settings_open_by_name(name) == false)
        printf("ui_settings: OHEZ_SETTINGS=%s names no page\r\n", name);
}
#endif

void ui_settings_loop(void)
{
    if (screen == NULL)
        return;

    if (rebuild_pending == true && overlay == NULL)
        ui_settings_rebuild();

    scan_poll();
    sitemaps_poll();
    servers_poll();

    /* The tune ends without an event. Only does anything on the Audio page,
     * where the button exists at all. */
    audio_demo_refresh();

    if (port_millis() >= wlan_state_refresh_deadline)
    {
        wlan_state_refresh_deadline = port_millis() + WLAN_STATE_REFRESH_INTERVAL;
        wlan_state_update();
    }
}
