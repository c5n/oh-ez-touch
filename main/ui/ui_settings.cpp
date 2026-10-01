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
 * Edits go into a draft copy of Config::item, not into the live one. Leaving
 * the page commits the draft -- there is no Save button -- so a half-typed
 * hostname never reaches wlan_setup(), and the commit can tell exactly which
 * fields moved, which is what decides whether a restart is worth offering.
 */

#include "sdkconfig.h"

#include "ui_settings.hpp"

#include "icons/icon_set.hpp"
#include "openhab_ui.hpp"
#include "config/config_fields.hpp"
#include "openhab/openhab_discover.hpp"
#include "openhab/openhab_sitemaps.hpp"
#include "control/beeper_control.hpp"
#include "peripherals/relay.hpp"
#include "peripherals/sensor_bme280.hpp"
#include "peripherals/sensor_main.hpp"
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
 * screens wear -- but slimmer than theirs, because every pixel it spends is
 * one a settings row cannot, and its two labels read as well at 40 px as at
 * the 56 an item window can afford. The rows are sized for the finger this
 * panel is operated with, and a row is now a name beside a control rather
 * than a full-width button, so 44 px -- a fingertip's floor -- remains the
 * budget the footer and the pager have to live within. */
#define BAR_HEIGHT    40
#define FOOTER_HEIGHT 40
#define ROW_HEIGHT    44

/* The gap between the rows, and the padding of the area they sit in. A page
 * counts these the way it counts its rows -- see field_pages_walk(). */
#define ROWS_PAD 4
#define ROW_GAP  4

/* One section heading, where a page carries more than one section. */
#define HEADING_H (lv_font_get_line_height(ui_style_theme()->font_small) + 2)

/* A numeric field whose range spans no more than this is a slider rather
 * than a keyboard: a range that fits the panel's width fits a finger's drag,
 * and anything wider (a port, a timeout in seconds up to a day) is faster to
 * type than to sweep. */
#define SLIDER_RANGE_MAX 100
#define SLIDER_TRACK_H  16
#define SLIDER_KNOB_W   6
#define SLIDER_WIDTH    120
#define SLIDER_VALUE_W  56

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

/* The two ways a footer button turns a multi-page screen's pages. One pair
 * of arrows for every screen that pages -- the Systeminfo screen and the
 * Icons catalogue -- rather than each inventing a footer of its own; a
 * direction and not a target, because from anywhere the ends are one tap
 * away, which is all the first/last jumps this used to have bought. */
enum pager_dir_e
{
    PAGER_PREV, /* LV_SYMBOL_LEFT  */
    PAGER_NEXT  /* LV_SYMBOL_RIGHT */
};

/* The Systeminfo screen's pages, in the order the arrows walk through them. */
enum info_page_e
{
    INFO_PAGE_GENERAL = 0, /* the firmware                    */
    INFO_PAGE_NETWORK,     /* the addresses it answers to     */
    INFO_PAGE_WIFI,        /* the radio link                  */
    INFO_PAGE_SENSORS,     /* the sensors, and the relays     */
    INFO_PAGE_COUNT
};

/* The pages' names, for the bar: the footer counts them, and the bar is
 * where "Systeminfo (General)" spells out which one is showing. */
static const char *const info_page_names[INFO_PAGE_COUNT] = {
    "General", "Network", "WiFi", "Sensors",
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

/* One per tab: the list of rows, and the footer's message label. */
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

/* Which page of the Systeminfo screen is showing, with the same lifecycle
 * as the one above for the same reasons. */
static uint8_t info_page = INFO_PAGE_GENERAL;

/* Which family the Fonts screen is showing, likewise. */
static uint8_t fonts_page = 0;

/* Which page of a section's rows is showing, and how many that section takes.
 * The rows of a section are packed one logical group per page (see
 * field_pages_walk()); pages beyond the first carry the group's remaining
 * rows without repeating its heading, and the footer turns them.
 *
 * One pair per tab rather than one shared counter: a section keeps its place
 * the way the Icons catalogue and the Systeminfo screen keep theirs, and a
 * rebuild -- a theme change -- goes through screen_show_section() and must
 * not move it. field_pages[] is recomputed on every screen_show_section(),
 * because it follows the theme's small font and the panel's height; the
 * clamp that goes with it keeps a page that no longer exists from being the
 * one showing when the rebuild settles. */
static uint8_t field_page[SETTINGS_TAB_COUNT];
static uint8_t field_pages[SETTINGS_TAB_COUNT];

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

/* The WLAN tab. The credentials are edited here and reach NVS when the page
 * is left, like every other edit on it. */
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
 * fields, and leaving either commits the same draft -- and two pages because a
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
static void info_page_event(lv_event_t *e);
static void fonts_page_event(lv_event_t *e);
static void fields_page_event(lv_event_t *e);
static void wlan_state_update(void);
static void keyboard_cancel_event(lv_event_t *e);
static void keyboard_key_event(lv_event_t *e);

/* The commit every navigation away from a page runs, and the restart a
 * commit may have earned -- defined with the save machinery, reached from
 * the handlers that navigate. */
static void page_leave(const char **restart_label);
static void restart_prompt(const char *label);

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

/* A list entry, not a settings row: a full-width button with a name on the
 * left and a detail on the right, the way the WLAN scan results and the
 * server and sitemap lists read. The whole thing is the target, because
 * choosing one of these is the point of the list. Kept beside the settings
 * row below because both are "a row", and only one of them is a setting. */
static lv_obj_t *list_row_create(lv_obj_t *parent, const char *name)
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

    /* Child 1, which is what list_row_value() writes to. */
    lv_obj_t *value_label = lv_label_create(row);

    lv_label_set_text(value_label, "");
    lv_label_set_long_mode(value_label, LV_LABEL_LONG_DOT);
    /* Capped rather than content-sized: a 31 character item name would
     * otherwise push the field's own name off the row entirely. */
    lv_obj_set_style_max_width(value_label, lv_pct(55), 0);

    return row;
}

static void list_row_value(lv_obj_t *row, const char *value)
{
    lv_label_set_text(lv_obj_get_child(row, 1), value);
}

/* -------------------------------------------------------- the settings row */

/* The control a field's value takes. An on/off value is a switch, a number
 * over a range the panel can sweep is a slider, and everything else -- a
 * name to type, an option to cycle, a number too wide to drag -- is a button
 * that shows the value and opens the keyboard. */
enum row_ctrl_e
{
    ROW_CTRL_BUTTON,
    ROW_CTRL_SWITCH,
    ROW_CTRL_SLIDER
};

static enum row_ctrl_e row_ctrl(const struct config_field_s *f)
{
    switch (f->kind)
    {
    case SETTINGS_BOOL:
        return ROW_CTRL_SWITCH;

    case SETTINGS_INT:
    case SETTINGS_UINT:
    case SETTINGS_ULONG:
        return (f->max - f->min <= SLIDER_RANGE_MAX) ? ROW_CTRL_SLIDER : ROW_CTRL_BUTTON;

    default:
        return ROW_CTRL_BUTTON;
    }
}

/* A settings row: the name on the left, the control beside it on the right.
 * The name is a plain label rather than the label of a full-width button,
 * so the control is what carries the value -- the name says what the value
 * is, the control is what changes it, and neither wears the other's job.
 *
 * The control's children, by kind, are the contract row_refresh() works to
 * and the builders below write to. */
#define ROW_CHILD_NAME   0 /* the label, every row                        */
#define ROW_CHILD_VALUE  1 /* buttons: the button; sliders: the value text */
#define ROW_CHILD_SLIDER 2 /* sliders only: the slider itself              */

static lv_obj_t *row_create(lv_obj_t *parent, const char *name)
{
    lv_obj_t *row = ui_plain_container(parent);

    lv_obj_set_size(row, lv_pct(100), ROW_HEIGHT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_hor(row, 6, 0);
    lv_obj_set_style_pad_column(row, 6, 0);

    lv_obj_t *label = lv_label_create(row);

    lv_label_set_text(label, name);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_obj_add_style(label, &ui_style_label, LV_PART_MAIN);
    lv_obj_set_flex_grow(label, 1);

    return row;
}

/* The value button, for the kinds a keyboard or a cycle answers. Content-
 * sized but capped, so a long hostname cannot push the name off the row. */
static lv_obj_t *row_value_button(lv_obj_t *row)
{
    lv_obj_t *btn = ui_themed_button(row, "");

    lv_obj_set_style_max_width(btn, lv_pct(60), 0);
    lv_label_set_long_mode(lv_obj_get_child(btn, 0), LV_LABEL_LONG_DOT);

    return btn;
}

static void row_value_set(lv_obj_t *row, const char *value)
{
    lv_obj_t *btn = lv_obj_get_child(row, ROW_CHILD_VALUE);

    lv_label_set_text(lv_obj_get_child(btn, 0), value);
}

/* The switch, for an on/off value. Styled from the same three slider
 * surfaces the item windows' controls wear, so every knob on the panel
 * looks like the same machine drew it. */
static lv_obj_t *row_switch(lv_obj_t *row)
{
    lv_obj_t *sw = lv_switch_create(row);

    lv_obj_add_style(sw, &ui_style_slider, LV_PART_MAIN);
    lv_obj_add_style(sw, &ui_style_slider_indicator, LV_PART_INDICATOR);
    lv_obj_add_style(sw, &ui_style_slider_knob, LV_PART_KNOB);
    lv_obj_set_size(sw, 44, 24);
    lv_obj_set_style_radius(sw, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_radius(sw, LV_RADIUS_CIRCLE, LV_PART_KNOB);
    lv_obj_set_style_pad_all(sw, 0, LV_PART_KNOB);

    return sw;
}

/* The slider, for a number with a range that fits it: the value as text at
 * the right edge of the row, and a track as wide as the name can spare.
 *
 * The knob is the full height of the track with a thin marker at the fill
 * edge, laid out the way the item windows lay theirs -- see item_slider.cpp:
 * lv_slider takes the knob's size from the track and then adds the pads, so
 * the negative padding is what makes it a marker rather than a block. */
static lv_obj_t *row_slider(lv_obj_t *row)
{
    lv_obj_t *slider = lv_slider_create(row);

    lv_obj_add_style(slider, &ui_style_slider, LV_PART_MAIN);
    lv_obj_add_style(slider, &ui_style_slider_indicator, LV_PART_INDICATOR);
    lv_obj_add_style(slider, &ui_style_slider_knob, LV_PART_KNOB);
    lv_obj_set_size(slider, SLIDER_WIDTH, SLIDER_TRACK_H);
    lv_obj_set_style_radius(slider, 6, LV_PART_MAIN);
    lv_obj_set_style_radius(slider, 6, LV_PART_INDICATOR);
    lv_obj_set_style_pad_all(slider, 0, LV_PART_KNOB);
    lv_obj_set_style_pad_hor(slider, -(SLIDER_TRACK_H - SLIDER_KNOB_W) / 2, LV_PART_KNOB);

    return slider;
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

/* The same, with the unit the label no longer carries written after the
 * number: the name says what it is, the value says how much of it. */
static void field_value_with_unit(const struct config_field_s *f, char *buffer, size_t size)
{
    field_value_text(f, &draft, buffer, size);

    if (f->unit != NULL && f->unit[0] != '\0')
        snprintf(buffer + strlen(buffer), size - strlen(buffer), " %s", f->unit);
}

/* What a SETTINGS_COLOR row offers on the panel. A short list of names
 * rather than a picker: a finger on a 320 px screen chooses between a dozen
 * colours far better than it drags a hue, and any other value is still one
 * <input type=color> away in the web form -- the row then says "Custom". */
static const struct
{
    const char *name;
    uint32_t    rgb;
} color_palette[] = {
    {"Black", 0x000000},  {"White", 0xFFFFFF},    {"Warm white", 0xFFE4B5},
    {"Grey", 0x808080},   {"Dark grey", 0x303030}, {"Red", 0xD03020},
    {"Orange", 0xFF8000}, {"Amber", 0xFFB000},     {"Yellow", 0xFFE040},
    {"Green", 0x30C050},  {"Cyan", 0x30C0D0},      {"Blue", 0x3060E0},
};

#define COLOR_PALETTE_COUNT (sizeof(color_palette) / sizeof(color_palette[0]))

/* Black or white, whichever reads on `rgb`: the usual luma weights, which is
 * all a label on a swatch needs. */
static lv_color_t color_contrast(uint32_t rgb)
{
    uint32_t luma = ((rgb >> 16) & 0xFF) * 299 + ((rgb >> 8) & 0xFF) * 587 + (rgb & 0xFF) * 114;

    return lv_color_hex(luma > 128000 ? 0x000000 : 0xFFFFFF);
}

/* The value button of a colour row is the swatch: filled with the colour and
 * labelled with its palette name, or "Custom" and the hex for anything the
 * palette does not have. */
static void color_row_paint(lv_obj_t *row, const struct config_field_s *f, char *buffer,
                            size_t size)
{
    uint32_t  rgb = (uint32_t)config_field_read(f, &draft);
    lv_obj_t *btn = lv_obj_get_child(row, ROW_CHILD_VALUE);

    snprintf(buffer, size, "Custom %06lX", (unsigned long)rgb);

    for (size_t i = 0; i < COLOR_PALETTE_COUNT; i++)
        if (color_palette[i].rgb == rgb)
            snprintf(buffer, size, "%s", color_palette[i].name);

    lv_obj_set_style_bg_color(btn, lv_color_hex(rgb), LV_PART_MAIN);
    lv_obj_set_style_bg_grad_color(btn, lv_color_hex(rgb), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_text_color(lv_obj_get_child(btn, 0), color_contrast(rgb), LV_PART_MAIN);
}

static void row_refresh(lv_obj_t *row, const struct config_field_s *f)
{
    char buffer[VALUE_BUFFER_LEN];

    switch (row_ctrl(f))
    {
    case ROW_CTRL_SWITCH:
    {
        lv_obj_t *sw = lv_obj_get_child(row, ROW_CHILD_VALUE);

        if (config_field_read(f, &draft) != 0)
            lv_obj_add_state(sw, LV_STATE_CHECKED);
        else
            lv_obj_remove_state(sw, LV_STATE_CHECKED);
        break;
    }

    case ROW_CTRL_SLIDER:
    {
        field_value_with_unit(f, buffer, sizeof(buffer));

        lv_label_set_text(lv_obj_get_child(row, ROW_CHILD_VALUE), buffer);
        lv_slider_set_value(lv_obj_get_child(row, ROW_CHILD_SLIDER),
                            config_field_read(f, &draft), LV_ANIM_OFF);
        break;
    }

    default:
        field_value_with_unit(f, buffer, sizeof(buffer));

        if (f->kind == SETTINGS_COLOR)
            color_row_paint(row, f, buffer, sizeof(buffer));

        row_value_set(row, buffer);
        break;
    }
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

    lv_obj_set_ignore_layout(overlay, true);
    lv_obj_set_scrollable(overlay, false);
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
     * what was just measured and a later commit does not put the old numbers
     * back; the baseline so that the commit is then not told a restart-flagged
     * field moved; the live config because that is what saveConfig() writes. */
    draft.touch.x_origin = (unsigned int)cal->x_origin;
    draft.touch.x_span = (unsigned int)cal->x_span;
    draft.touch.y_origin = (unsigned int)cal->y_origin;
    draft.touch.y_span = (unsigned int)cal->y_span;

    baseline.touch = draft.touch;
    settings_config->item.touch = draft.touch;

    bool stored = settings_config->saveConfig();

    /* Applies the calibration to the pointer, among everything else it
     * re-applies. Unconditional, like draft_commit()'s: a calibration that
     * could not be written is still a calibration that works until the next
     * boot, and the user finds that out from the footer rather than from the
     * panel. */
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

    /* Does not return on either target: the simulator re-execs itself, which
     * is a reboot in everything but name -- see linux/port_sys.c. */
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
                row_value_set(edit_row, (text[0] == '\0') ? "--" : "*****");
            else
                row_value_set(edit_row, (text[0] == '\0') ? "--" : text);
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

static void color_choice_event(lv_event_t *e)
{
    size_t index = (size_t)(uintptr_t)lv_event_get_user_data(e);

    if (edit_field == NULL || index >= COLOR_PALETTE_COUNT)
        return;

    config_field_write(edit_field, &draft, (int32_t)color_palette[index].rgb);

    if (edit_row != NULL)
        row_refresh(edit_row, edit_field);

    BEEPER_EVENT_CHANGE();
    overlay_close();
}

static void color_cancel_event(lv_event_t *e)
{
    LV_UNUSED(e);

    overlay_close();
}

/* The palette: the field's name over a grid of swatches, one tap to choose.
 * Built the way the keyboard is -- an overlay over the rows, closed by the
 * choice or by the X -- so it backs out the same way. */
static void color_palette_open(lv_obj_t *row, const struct config_field_s *f)
{
    lv_obj_t *root = overlay_create();

    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(root, 4, 0);

    lv_obj_t *title_row = ui_plain_container(root);
    lv_obj_set_size(title_row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(title_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(title_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    lv_obj_t *label = lv_label_create(title_row);
    lv_label_set_text(label, f->label);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_obj_add_style(label, &ui_style_label, LV_PART_MAIN);
    lv_obj_set_flex_grow(label, 1);

    lv_obj_add_event_cb(ui_themed_button(title_row, LV_SYMBOL_CLOSE), color_cancel_event,
                        LV_EVENT_CLICKED, NULL);

    lv_obj_t *grid = ui_plain_container(root);
    lv_obj_set_width(grid, lv_pct(100));
    lv_obj_set_flex_grow(grid, 1);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_SPACE_EVENLY,
                          LV_FLEX_ALIGN_SPACE_EVENLY);
    lv_obj_set_style_pad_row(grid, 4, 0);
    lv_obj_set_style_pad_column(grid, 4, 0);

    uint32_t current = (uint32_t)config_field_read(f, &draft);

    for (size_t i = 0; i < COLOR_PALETTE_COUNT; i++)
    {
        lv_obj_t *swatch = ui_themed_button(grid, color_palette[i].name);
        lv_color_t color = lv_color_hex(color_palette[i].rgb);

        /* Four to a row, three rows: a quarter of the width less the gaps. */
        lv_obj_set_size(swatch, lv_pct(23), lv_pct(30));
        lv_obj_set_style_bg_color(swatch, color, LV_PART_MAIN);
        lv_obj_set_style_bg_grad_color(swatch, color, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(swatch, LV_OPA_COVER, LV_PART_MAIN);

        lv_obj_t *text = lv_obj_get_child(swatch, 0);

        lv_label_set_long_mode(text, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(text, lv_pct(100));
        lv_obj_set_style_text_align(text, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(text, color_contrast(color_palette[i].rgb), LV_PART_MAIN);

        /* The colour in effect, outlined so it can be found again. */
        if (color_palette[i].rgb == current)
        {
            lv_obj_set_style_border_width(swatch, 3, LV_PART_MAIN);
            lv_obj_set_style_border_color(swatch, color_contrast(color_palette[i].rgb),
                                          LV_PART_MAIN);
        }

        lv_obj_add_event_cb(swatch, color_choice_event, LV_EVENT_CLICKED, (void *)(uintptr_t)i);
    }

    BEEPER_EVENT_SCREEN();

    /* After overlay_create(), which clears these. */
    edit_field = f;
    edit_row = row;
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

/* A switch moved: the one gesture the row knows, straight into the draft.
 * LVGL has already set the new state before the event runs, so it is read
 * back rather than derived -- the sound follows the value even if a theme
 * ever refuses to draw one. */
static void field_switch_event(lv_event_t *e)
{
    const struct config_field_s *f =
        (const struct config_field_s *)lv_event_get_user_data(e);
    lv_obj_t *sw = (lv_obj_t *)lv_event_get_current_target(e);
    bool on = lv_obj_has_state(sw, LV_STATE_CHECKED);

    config_field_write(f, &draft, on ? 1 : 0);

    if (on)
        BEEPER_EVENT_TOGGLE_ON();
    else
        BEEPER_EVENT_TOGGLE_OFF();
}

/* A slider moved: the draft follows the knob rather than waiting for the
 * release, so a value dragged to and past is the value shown. No sound per
 * step -- a sweep is one gesture, not forty, and the release says it. */
static void field_slider_event(lv_event_t *e)
{
    const struct config_field_s *f =
        (const struct config_field_s *)lv_event_get_user_data(e);
    lv_obj_t *slider = (lv_obj_t *)lv_event_get_current_target(e);
    char buffer[VALUE_BUFFER_LEN];

    config_field_set_number(f, &draft, lv_slider_get_value(slider));

    field_value_with_unit(f, buffer, sizeof(buffer));
    lv_label_set_text(lv_obj_get_child(lv_obj_get_parent(slider), ROW_CHILD_VALUE), buffer);
}

static void field_slider_release_event(lv_event_t *e)
{
    LV_UNUSED(e);

    BEEPER_EVENT_CHANGE();
}

/* The value button: a tap cycles an enum -- the longest is four options,
 * same calculus as before -- and opens the keyboard for everything that
 * has to be typed. */
static void field_row_event(lv_event_t *e)
{
    const struct config_field_s *f =
        (const struct config_field_s *)lv_event_get_user_data(e);
    lv_obj_t *row = lv_obj_get_parent((lv_obj_t *)lv_event_get_current_target(e));

    if (f->kind == SETTINGS_ENUM)
    {
        int32_t next = config_field_read(f, &draft) + 1;

        if (next >= (int32_t)f->count)
            next = 0;

        config_field_write(f, &draft, next);
        row_refresh(row, f);
        BEEPER_EVENT_CHANGE();
        return;
    }

    if (f->kind == SETTINGS_COLOR)
    {
        color_palette_open(row, f);
        return;
    }

    field_edit_open(row, f);
}

static void wlan_ssid_row_event(lv_event_t *e)
{
    buffer_edit_open(lv_obj_get_parent((lv_obj_t *)lv_event_get_current_target(e)),
                     "Network (SSID)", wlan_ssid_buf, sizeof(wlan_ssid_buf), false);
}

static void wlan_psk_row_event(lv_event_t *e)
{
    buffer_edit_open(lv_obj_get_parent((lv_obj_t *)lv_event_get_current_target(e)),
                     "Password", wlan_psk_buf, sizeof(wlan_psk_buf), true);
}

static void scan_result_event(lv_event_t *e)
{
    size_t index = (size_t)(uintptr_t)lv_event_get_user_data(e);

    if (index >= scan_result_count)
        return;

    strlcpy(wlan_ssid_buf, scan_results[index].ssid, sizeof(wlan_ssid_buf));

    if (wlan_ssid_row != NULL)
        row_value_set(wlan_ssid_row, wlan_ssid_buf);

    if (scan_results[index].encrypted == false)
    {
        /* Nothing left to ask for -- and the passphrase of whatever was
         * selected before must not be carried over to an open network. */
        wlan_psk_buf[0] = '\0';

        if (wlan_psk_row != NULL)
            row_value_set(wlan_psk_row, "--");

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

        lv_obj_t *row = list_row_create(wlan_scan_list, scan_results[i].ssid);
        list_row_value(row, value);
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

        lv_obj_t *row = list_row_create(server_list_obj, text);

        list_row_value(row, value);
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

        lv_obj_t *row = list_row_create(sitemap_list_obj, text);

        /* The label, which is what a sitemap is called rather than what it is
         * named. openHAB does not require one; SitemapList falls back to the
         * name, so this column is never blank. */
        list_row_value(row, openhab_sitemaps_label(i));
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

    /* Leaving the lists is committing them, like leaving any page -- picking
     * a server or a sitemap is an edit like any other. */
    const char *restart_label = NULL;

    page_leave(&restart_label);

    openhab_manual = true;

    /* The chime for going a level deeper, which is what this is: the same one
     * the index plays for opening a section. */
    BEEPER_EVENT_LINK();
    screen_show_section(SETTINGS_TAB_OPENHAB);

    restart_prompt(restart_label);
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
    {
        /* A paged screen hides its label so the pager can have the bar's
         * exact middle (see footer_pager()); a message is worth more than
         * that, so saying one brings the label back. */
        lv_obj_clear_flag(tab_status[tab], LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(tab_status[tab], text);
    }
}

/* Whether anything in the draft has moved off the baseline. The same
 * comparison settings_restart_needed() makes over the flagged fields, but
 * over all of them: this is what decides whether leaving the page is worth
 * a write at all. */
static bool draft_changed(void)
{
    for (size_t i = 0; i < config_field_count; i++)
    {
        const struct config_field_s *f = &config_fields[i];

        if (f->kind == SETTINGS_SECTION)
            continue;

        if (f->kind == SETTINGS_TEXT)
        {
            if (strcmp(config_field_text(f, &draft), config_field_text(f, &baseline)) != 0)
                return true;
        }
        else if (config_field_read(f, &draft) != config_field_read(f, &baseline))
        {
            return true;
        }
    }

    return false;
}

/* The whole of what the Save button used to do, minus the footer it used to
 * say it through: the draft becomes the live config, is written to the file,
 * and everything that can be applied without a reboot is applied. The status
 * line is gone because the page it stood on is being left -- the accept
 * chime is the receipt.
 *
 * On a restart-flagged field, *restart_label is set for the caller to act
 * on once the navigation it is in the middle of has settled: the prompt is
 * an overlay on this screen, and opening it before the screen is rebuilt
 * would build it just in time for lv_obj_clean() to delete it. NULL means
 * no prompt is owed. */
static void draft_commit(const char **restart_label)
{
    const char *label = NULL;
    bool restart_needed = settings_restart_needed(&baseline, &draft, &label);

    settings_config->item = draft;

    bool stored = settings_config->saveConfig();

    /* Whether or not the file was written, the draft is now what the running
     * firmware believes, so the next comparison must not offer a restart for
     * a change this one already applied. */
    baseline = draft;

    settings_apply_live(settings_config);

    if (stored == false)
    {
        /* settings_apply_live() has already run, so the values are in effect
         * -- they just will not survive a reboot. saveConfig() returns false
         * when no config file was ever loaded, and always on the simulator,
         * which has no filesystem. No restart is offered either: a reboot is
         * exactly what would lose them. */
        BEEPER_EVENT_ERROR();
        return;
    }

    BEEPER_EVENT_ACCEPT();

#if CONFIG_OHEZ_DEBUG_UI_SETTINGS
    debug_printf("ui_settings: saved, restart needed: %d\r\n", (int)restart_needed);
#endif

    if (restart_needed == true && restart_label != NULL)
        *restart_label = label;
}

/* The WLAN credentials are not part of Config -- they live in NVS -- so the
 * draft says nothing about them and this is their commit, on the same
 * gesture as every other edit on the page: leaving it.
 *
 * Only when something moved. Re-applying the stored pair would tear the
 * station off the network it is on just to rejoin it, so the SSID is
 * compared against what the screen loaded and the passphrase against what
 * is stored -- an empty passphrase buffer means "not retyped", which is how
 * the screen deliberately leaves it. */
static void wlan_credentials_commit(void)
{
    if (wlan_ssid_buf[0] == '\0')
        return;

    char stored_ssid[WLAN_SSID_SIZE];
    char stored_psk[WLAN_PSK_SIZE];

    wlan_credentials_get(stored_ssid, sizeof(stored_ssid), stored_psk, sizeof(stored_psk));

    if (strcmp(wlan_ssid_buf, stored_ssid) == 0 &&
        (wlan_psk_buf[0] == '\0' || strcmp(wlan_psk_buf, stored_psk) == 0))
        return;

    if (wlan_set_credentials(wlan_ssid_buf, wlan_psk_buf) == false)
    {
        BEEPER_EVENT_ERROR();
        return;
    }

    BEEPER_EVENT_ACCEPT();
}

/* Leaving a page is what saves it. Every path away from a section runs
 * through here -- the back bar, the Manual button, the control interface's
 * `settings <page>`, the close -- so a value changed is a value kept,
 * without a Save button anyone could forget or ignore. Menus carry no edits
 * and are nobody's page, so leaving one commits nothing.
 *
 * A restart is only ever *offered*: the panel never reboots itself out from
 * under a navigation. */
static void page_leave(const char **restart_label)
{
    if (settings_config == NULL || screen == NULL)
        return;

    if (MENU_IS(current_tab))
        return;

    if (current_tab == SETTINGS_TAB_WLAN)
        wlan_credentials_commit();

    if (draft_changed() == true)
        draft_commit(restart_label);
}

/* The restart a commit may have earned, asked once the navigation that
 * caused it has finished and the screen is standing still. */
static void restart_prompt(const char *label)
{
    if (label == NULL)
        return;

    char text[120];

    snprintf(text, sizeof(text),
             "\"%s\" is only read while the device boots.\n\nRestart now?",
             label);
    confirm_restart_open(text);
}

/* Play the family's signature chime at the volume being edited.
 *
 * The draft and not the saved value: the whole reason this button exists is to
 * let a level be judged before the page is left and commits it, and a slider
 * you cannot hear until after it is kept is not much of a control. The boot
 * chime is what it plays -- it is each family's longest statement, it is the
 * one chime with a chord in it worth hearing, and it is otherwise only
 * audible by restarting the panel.
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
    lv_obj_add_event_cb(row_value_button(wlan_ssid_row), wlan_ssid_row_event, LV_EVENT_CLICKED,
                        NULL);
    row_value_set(wlan_ssid_row, (wlan_ssid_buf[0] == '\0') ? "--" : wlan_ssid_buf);

    wlan_psk_row = row_create(rows, "Password");
    lv_obj_add_event_cb(row_value_button(wlan_psk_row), wlan_psk_row_event, LV_EVENT_CLICKED,
                        NULL);
    row_value_set(wlan_psk_row, (wlan_psk_buf[0] == '\0') ? "--" : "*****");

    wlan_scan_list = ui_plain_container(rows);
    lv_obj_set_size(wlan_scan_list, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(wlan_scan_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(wlan_scan_list, 2, 0);

    scan_list_rebuild();
}

/* -------------------------------------------------------------- Info tab */

/* One plain line of the Info page: the small font at the full width of the
 * page, with dots on the end of whatever does not fit. A line that wrapped
 * would grow a second, and the last line of the page would sink below the
 * fold -- and the whole layout below exists so that nothing on this page
 * scrolls. */
static void info_line(lv_obj_t *rows, const char *text)
{
    lv_obj_t *line = lv_label_create(rows);

    lv_label_set_text(line, text);
    lv_label_set_long_mode(line, LV_LABEL_LONG_DOT);
    lv_obj_set_width(line, lv_pct(100));
    lv_obj_add_style(line, &ui_style_label, LV_PART_MAIN);
}

/* The label column of a page's labeled lines, as wide as the longest label
 * that page uses plus a little air. Measured against the live family's small
 * face rather than fixed, so the values line up no matter which of the three
 * is drawing them -- a proportional face's spaces will not do it -- and
 * ui_settings_rebuild() redraws the page on a theme change anyway. Per page
 * rather than once for the screen, so a page of short labels ("IP:", "DNS:")
 * is not sized by another page's "Gateway:". */
static int32_t info_label_width(const char *longest)
{
    const lv_font_t *font = ui_style_theme()->font_small;
    int32_t          width = 4;

    for (const char *p = longest; *p != '\0'; p++)
        width += lv_font_get_glyph_width(font, *p, *(p + 1)) + ui_style_theme()->letter_space;

    return width;
}

/* One labeled line: the name in the column above, the value in what is left,
 * dotted when it does not fit. A hostname or an SSID can be 32 characters,
 * and the page has no room for a second line of either. */
static void info_row(lv_obj_t *rows, int32_t label_width, const char *name, const char *value)
{
    lv_obj_t *row = ui_plain_container(rows);

    lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);

    lv_obj_t *label = lv_label_create(row);

    lv_label_set_text(label, name);
    lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
    lv_obj_set_width(label, label_width);
    lv_obj_add_style(label, &ui_style_label, LV_PART_MAIN);

    lv_obj_t *val = lv_label_create(row);

    lv_label_set_text(val, value);
    lv_label_set_long_mode(val, LV_LABEL_LONG_DOT);
    lv_obj_set_flex_grow(val, 1);
    lv_obj_add_style(val, &ui_style_label, LV_PART_MAIN);
}

/* One of a page's blank lines. Not a line of text -- the pages are sized to
 * the family that runs tallest -- but the break it stands for, at a height
 * that does. */
static void info_gap(lv_obj_t *rows)
{
    lv_obj_t *gap = ui_plain_container(rows);

    lv_obj_set_size(gap, lv_pct(100), 8);
}

/* --------------------------------------------------------- the four pages */

/* Page one: the firmware itself -- what is running, since when, and how much
 * memory it has to spare. The heap pair is the web status page's diagnosis,
 * and the largest block is what tells "full" from "fragmented". */
static void info_general_page(lv_obj_t *rows)
{
    char    buffer[64];
    int32_t labels = info_label_width("Largest:");

    snprintf(buffer, sizeof(buffer), "%s %u.%02u (%s)", TARGET_NAME, VERSION_MAJOR, VERSION_MINOR,
             __DATE__);
    info_line(rows, buffer);

    info_gap(rows);

    /* port_millis() is the uptime: it is monotonic since boot and nothing
     * resets it, which is the whole of what the Uptime library did. */
    unsigned long long up = (unsigned long long)(port_millis() / 1000);

    snprintf(buffer, sizeof(buffer), "%llu days, %lluh %llum %llus", up / 86400,
             (up / 3600) % 24, (up / 60) % 60, up % 60);
    info_row(rows, labels, "Uptime:", buffer);

    snprintf(buffer, sizeof(buffer), "%u bytes", (unsigned)port_free_heap());
    info_row(rows, labels, "Heap:", buffer);

    /* Zero is the port's documented "cannot say": the host has no way to
     * measure its own fragmentation, and a number that is really a shrug
     * would answer the wrong question. */
    if (port_largest_free_block() > 0)
        snprintf(buffer, sizeof(buffer), "%u bytes", (unsigned)port_largest_free_block());
    else
        snprintf(buffer, sizeof(buffer), "--");

    info_row(rows, labels, "Largest:", buffer);
}

/* Page two: the addresses the panel answers to. This is the half the old
 * table had to drop for want of room: netmask, gateway and DNS are back,
 * and the MAC moves here from the IP line's parens, spelled out at last --
 * the page is the one thing the addresses page never was, all of a piece. */
static void info_network_page(lv_obj_t *rows)
{
    port_net_info_t net;
    int32_t         labels = info_label_width("Gateway:");

    port_net_info(&net);

    info_row(rows, labels, "Name:", net.hostname);
    info_row(rows, labels, "IP:", net.ip);
    info_row(rows, labels, "Mask:", net.netmask);
    info_row(rows, labels, "Gateway:", net.gateway);
    info_row(rows, labels, "DNS:", net.dns);
    info_row(rows, labels, "MAC:", net.mac);
}

/* Page three: the radio link. On a wired host the SSID is the interface
 * name and the BSSID the MAC -- the page still answers "which network am I
 * on", the only way a host can, and RSSI says "wired" for the same reason. */
static void info_wifi_page(lv_obj_t *rows)
{
    char            buffer[64];
    port_net_info_t net;
    int32_t         labels = info_label_width("BSSID:");

    port_net_info(&net);

    info_row(rows, labels, "SSID:", net.ssid);
    info_row(rows, labels, "BSSID:", net.bssid);

    if (net.rssi == PORT_NET_RSSI_WIRED)
        snprintf(buffer, sizeof(buffer), "wired");
    else
        snprintf(buffer, sizeof(buffer), "%i dBm (%u %%)", net.rssi,
                 openhab_ui_signal_quality(net.rssi));

    info_row(rows, labels, "RSSI:", buffer);
}

/* Page four: what the panel can sense, and what it is switching. The BME280
 * reading is taken for the page rather than cached: the chip is in forced
 * mode, one conversion per call, and a page built on demand might as well
 * ask for its own -- sensor_bme280.hpp separates taking a reading from
 * publishing one for exactly this. Only when the Sensors setting is on and
 * the chip answered at boot, so the page never runs a bus the configuration
 * left dark.
 *
 * The relays are one row rather than one each: the largest board has three,
 * and beside the sensor's three that would be seven lines, where the page
 * has room for four and a gap at the family that runs tallest. */
static void info_sensors_page(lv_obj_t *rows)
{
    char    buffer[64];
    int32_t labels = info_label_width("BME280:");

    if (sensor_main_bme280_active() == true)
    {
        float temperature_c = 0.0f;
        float humidity_pct  = 0.0f;
        float pressure_hpa  = 0.0f;

        if (sensor_bme280_read(&temperature_c, &humidity_pct, &pressure_hpa) == true)
        {
            /* One decimal is a display's; the MQTT topic's three are for an
             * item history, and a page nobody stares at for minutes wants
             * none of them. */
            snprintf(buffer, sizeof(buffer), "%.1f °C", temperature_c);
            info_row(rows, labels, "Temp:", buffer);
            snprintf(buffer, sizeof(buffer), "%.1f %%", humidity_pct);
            info_row(rows, labels, "Hum:", buffer);
            snprintf(buffer, sizeof(buffer), "%.0f hPa", pressure_hpa);
            info_row(rows, labels, "Press:", buffer);
        }
        else
        {
            /* Answered at boot, silent now. The page says so rather than
             * repeating the last value or a zero: a stale reading on a
             * status page is a lie with its timestamp missing. */
            info_row(rows, labels, "BME280:", "--");
        }
    }
    else
        info_row(rows, labels, "BME280:", "off");

    info_gap(rows);

    if (relay_count() == 0)
    {
        snprintf(buffer, sizeof(buffer), "none");
    }
    else
    {
        /* One row, the states numbered the way the wall plate and the MQTT
         * topics number them -- port_relay.h says why it is the port that
         * counts from zero instead. */
        size_t used = 0;

        buffer[0] = '\0';

        for (unsigned i = 0; i < relay_count(); i++)
        {
            int written = snprintf(buffer + used, sizeof(buffer) - used, "%s%u:%s",
                                  (used > 0) ? " " : "", i + 1,
                                  relay_state(i) ? "ON" : "OFF");

            if (written < 0 || (size_t)written >= sizeof(buffer) - used)
                break;

            used += (size_t)written;
        }
    }

    info_row(rows, labels, "Relays:", buffer);
}

/* The Systeminfo screen: four pages turned by the footer's arrows, each one
 * few enough lines to fit between the bar and the footer without scrolling
 * -- the budget is Antonio's, the family that runs tallest: a 16 px line at
 * 23 px, six of them in the 140 px the chrome leaves of a 240 px panel, and
 * screen_show_section() drops the frame padding for this tab. What one page
 * cannot carry goes on the next one rather than below the fold, which is
 * the whole answer to what the scrolling table this replaced was asked.
 *
 * Moved here from openhab_ui.cpp's header_event_handler(), which is what
 * the status bar used to open on its own. */
static void info_tab_build(lv_obj_t *rows)
{
    switch (info_page)
    {
    case INFO_PAGE_NETWORK:
        info_network_page(rows);
        break;

    case INFO_PAGE_WIFI:
        info_wifi_page(rows);
        break;

    case INFO_PAGE_SENSORS:
        info_sensors_page(rows);
        break;

    default:
        info_general_page(rows);
        break;
    }
}

/* ------------------------------------------------------------- Fonts tab */

/* The three typefaces the firmware carries, one page each: a page shows a
 * family's three sizes together, because a specimen answers "what does this
 * family look like" and its own sizes are what it is a specimen *of*. All
 * three are compiled in unconditionally (see ui_style.cpp's font table), so
 * the pager always has three pages to turn. */
static const struct
{
    const char      *family;
    const lv_font_t *small;
    const lv_font_t *normal;
    const lv_font_t *large;
} font_faces[] = {
    {"Barlow (Material, Classic)", &custom_font_ui_16,   &custom_font_ui_22,
     &custom_font_ui_36},
    {"Rajdhani (JARVIS)",          &custom_font_hud_16,  &custom_font_hud_22,
     &custom_font_hud_36},
    {"Antonio (LCARS)",            &custom_font_lcars_16, &custom_font_lcars_22,
     &custom_font_lcars_36},
};

#define FONT_FACE_COUNT (sizeof(font_faces) / sizeof(font_faces[0]))

/* The Fonts screen's two arrows, the same shape as the other paged screens'.
 * The whole section is rebuilt rather than the lines alone: the footer
 * carries the page number, and the shared path is what keeps every pointer
 * correct after the delete. */
static void fonts_page_event(lv_event_t *e)
{
    bool    next = (lv_event_get_user_data(e) == (void *)(uintptr_t)PAGER_NEXT);
    uint8_t page = fonts_page;

    /* The ends are the ends. A button that cannot move the page any further
     * stays quiet rather than rebuilding what is already showing. */
    if (next == true && page < FONT_FACE_COUNT - 1)
        page++;
    else if (next == false && page > 0)
        page--;

    if (page == fonts_page)
        return;

    fonts_page = page;

    BEEPER_EVENT_TICK();

    screen_show_section(SETTINGS_TAB_FONTS);
}

/* One family's page: the name, and the three sizes ui_style.cpp hands out --
 * captions and the Systeminfo lines, state lines and headers, and the big
 * value labels. Read-only, like Systeminfo, because there is nothing to
 * configure here: the page is what the Theme page's choice looks like before
 * it is made. The 36 px line is plain ASCII because that face is the one
 * tools/build_fonts.sh gives no accented letters -- a page about type that
 * drew placeholder boxes would defeat itself.
 *
 * Every line is one line, dotted when it does not fit, never wrapped: a
 * specimen line that wrapped would grow a second line of the very size it
 * is there to show. LVGL's DOT mode dots only what overflows the label's
 * height -- at LV_SIZE_CONTENT nothing ever does, the line simply wraps --
 * so the height is pinned to one line of the line's own face. The footer's
 * status label does the same for the same reason (see screen_show_section).
 * The fullest page, Antonio's, measures 141 of the 144 px between bar and
 * footer, so no page scrolls. */
static void fonts_tab_build(lv_obj_t *rows)
{
    lv_obj_t *heading = lv_label_create(rows);

    lv_label_set_text(heading, font_faces[fonts_page].family);
    lv_label_set_long_mode(heading, LV_LABEL_LONG_DOT);
    lv_obj_set_width(heading, lv_pct(100));
    lv_obj_set_height(heading, lv_font_get_line_height(ui_style_theme()->font_small));
    lv_obj_add_style(heading, &ui_style_label, LV_PART_MAIN);
    lv_obj_set_style_pad_top(heading, 2, 0);

    const lv_font_t *fonts[] = {font_faces[fonts_page].small, font_faces[fonts_page].normal,
                                font_faces[fonts_page].large};
    static const uint8_t sizes[] = {16, 22, 36};

    for (size_t i = 0; i < sizeof(fonts) / sizeof(fonts[0]); i++)
    {
        char text[40];

        snprintf(text, sizeof(text), "%u px: The quick brown fox", (unsigned)sizes[i]);

        lv_obj_t *sample = lv_label_create(rows);

        lv_label_set_text(sample, text);
        lv_label_set_long_mode(sample, LV_LABEL_LONG_DOT);
        lv_obj_set_width(sample, lv_pct(100));
        lv_obj_set_height(sample, lv_font_get_line_height(fonts[i]));
        lv_obj_set_style_text_font(sample, fonts[i], 0);
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

/* The catalogue's two arrows. One callback with the direction the button
 * carries, like the Systeminfo screen's: the shared pager hands both the
 * same pair, and what a screen does with them is its own one counter. */
static void icons_page_event(lv_event_t *e)
{
    bool     next = (lv_event_get_user_data(e) == (void *)(uintptr_t)PAGER_NEXT);
    uint16_t pages = icons_pages();
    uint16_t page = icons_page;

    /* The ends are the ends. A button that cannot move the page any further
     * stays quiet rather than rebuilding what is already showing. */
    if (next == true && page < pages - 1)
        page++;
    else if (next == false && page > 0)
        page--;

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

/* The Systeminfo screen's two arrows, the same shape as the catalogue's --
 * the two differ only in the counter they turn and the section they rebuild. */
static void info_page_event(lv_event_t *e)
{
    bool    next = (lv_event_get_user_data(e) == (void *)(uintptr_t)PAGER_NEXT);
    uint8_t page = info_page;

    /* The ends are the ends, as above: a button that cannot move the page
     * any further stays quiet rather than rebuilding what is showing. */
    if (next == true && page < INFO_PAGE_COUNT - 1)
        page++;
    else if (next == false && page > 0)
        page--;

    if (page == info_page)
        return;

    info_page = page;

    BEEPER_EVENT_TICK();

    /* The whole section, for the same reason the catalogue's does: the
     * footer between the arrows carries the page number, and the shared
     * path is what keeps every pointer correct after the delete. */
    screen_show_section(SETTINGS_TAB_INFO);
}

/* A section's own two arrows, for the tabs whose rows come off the shared
 * table and do not fit on one page -- the same shape as the read-only
 * screens' pager, over the section's own counter. Nothing to save on the
 * way: turning a page is not leaving it, and the draft travels with the
 * section until the back bar commits it. */
static void fields_page_event(lv_event_t *e)
{
    bool    next = (lv_event_get_user_data(e) == (void *)(uintptr_t)PAGER_NEXT);
    uint8_t tab = current_tab;
    uint8_t page = field_page[tab];

    /* The ends are the ends, and the count is the build's: a button that
     * cannot move the page any further stays quiet rather than rebuilding
     * what is already showing. */
    if (next == true && page + 1 < field_pages[tab])
        page++;
    else if (next == false && page > 0)
        page--;

    if (page == field_page[tab])
        return;

    field_page[tab] = page;

    BEEPER_EVENT_TICK();

    screen_show_section(tab);
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

    /* The page number lives in the footer's pager, beside the arrows that
     * move it, rather than in the status label the other screens use: it is
     * what the arrows change, and the shared pager is where a screen that
     * pages counts its pages. */
}

/* ------------------------------------------------------------ the screen */

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

/* How many vertical pixels a section's rows have to fit in. Measured against
 * the panel and the chrome of the moment, because the pages are sized by
 * what does not fit: a section is split rather than scrolled. */
static int32_t field_page_capacity(void)
{
    int32_t vres = lv_display_get_vertical_resolution(NULL);

    return vres - BAR_HEIGHT - FOOTER_HEIGHT - 2 * ROWS_PAD;
}

/* One field's row: the name, and the control its value takes. */
static void field_row_build(lv_obj_t *rows, const struct config_field_s *f)
{
    lv_obj_t *row = row_create(rows, f->label);

    switch (row_ctrl(f))
    {
    case ROW_CTRL_SWITCH:
    {
        lv_obj_t *sw = row_switch(row);

        if (config_field_read(f, &draft) != 0)
            lv_obj_add_state(sw, LV_STATE_CHECKED);

        lv_obj_add_event_cb(sw, field_switch_event, LV_EVENT_VALUE_CHANGED, (void *)f);
        break;
    }

    case ROW_CTRL_SLIDER:
    {
        lv_obj_t *value = lv_label_create(row);

        lv_label_set_text(value, "");
        lv_obj_add_style(value, &ui_style_label, LV_PART_MAIN);
        lv_obj_set_width(value, SLIDER_VALUE_W);
        lv_obj_set_style_text_align(value, LV_TEXT_ALIGN_RIGHT, 0);

        lv_obj_t *slider = row_slider(row);

        lv_slider_set_range(slider, f->min, f->max);
        lv_obj_add_event_cb(slider, field_slider_event, LV_EVENT_VALUE_CHANGED, (void *)f);
        lv_obj_add_event_cb(slider, field_slider_release_event, LV_EVENT_RELEASED, NULL);
        /* The contact tick without the plate deformation: a slider is dragged
         * rather than pressed, so ui_motion_pressable()'s visual half would be
         * wrong on it while the acknowledgement is still right. */
        ui_beep_attach_press(slider);
        break;
    }

    default:
    {
        lv_obj_t *btn = row_value_button(row);

        lv_obj_add_event_cb(btn, field_row_event, LV_EVENT_CLICKED, (void *)f);
        break;
    }
    }

    row_refresh(row, f);

    /* Kept so that a choice made in one of the lists below can refresh the
     * row that now holds it. */
    if (f == host_field())
        host_field_row = row;
    else if (f == port_field())
        port_field_row = row;
    else if (f == sitemap_field())
        sitemap_field_row = row;
}

/* Walk a tab's rows, packing them into pages and building the one showing.
 *
 * A page holds one logical group: a section always begins a fresh page, so
 * "MQTT Broker" and "MQTT Publishing" are never half of each other, and a
 * group with more rows than a page holds continues on the next one without
 * repeating its heading. The rows are counted, not scrolled -- whatever
 * does not fit is a page nobody could reach, and the footer's pager is the
 * way to it.
 *
 * Both jobs in one walk: screen_show_section() asks for the page count
 * before it builds the footer that turns them, and then asks for the rows
 * of the page it settled on. `build` says which; the arithmetic is shared
 * because the count it returns has to be the count it laid out. */
static uint8_t field_pages_walk(uint8_t tab, bool build)
{
    lv_obj_t *rows = build ? tab_rows[tab] : NULL;
    bool      headings = tab_section_count(tab) > 1;
    int32_t   capacity = field_page_capacity();
    int32_t   y = 0;
    uint8_t   page = 0;

    for (size_t i = 0; i < config_field_count; i++)
    {
        const struct config_field_s *f = &config_fields[i];

        if (config_field_tab(i) != tab)
            continue;

        if (f->kind == SETTINGS_SECTION)
        {
            /* A new group is a new page, unless it is the first thing on
             * the one being counted. */
            if (y > 0)
            {
                page++;
                y = 0;
            }

            if (headings == true)
            {
                if (build == true && page == field_page[tab])
                {
                    lv_obj_t *heading = lv_label_create(rows);

                    lv_label_set_text(heading, f->label);
                    /* The caption font, not the state font: a 22 px heading
                     * costs most of a row, and a page holds three. */
                    lv_obj_add_style(heading, &ui_style_label, LV_PART_MAIN);
                    lv_obj_set_style_pad_top(heading, 2, 0);
                }

                y += HEADING_H + ROW_GAP;
            }

            continue;
        }

        if (y > 0 && y + ROW_HEIGHT > capacity)
        {
            page++;
            y = 0;
        }

        if (build == true && page == field_page[tab])
            field_row_build(rows, f);

        y += ROW_HEIGHT + ROW_GAP;
    }

    return (uint8_t)(page + 1);
}

/* The rows the current page of a section holds -- and nothing to scroll: the
 * walk above has already made sure they fit between the bar and the footer. */
static void field_rows_build(uint8_t tab)
{
    lv_obj_t *rows = tab_rows[tab];

    lv_obj_set_scrollable(rows, false);

    field_pages_walk(tab, true);
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
 * above, and from the root menu it closes.
 *
 * It is also, from a section, the save: the page being left commits its
 * draft on the way through, before the navigation deletes the page it was
 * a draft of. The restart a commit may have earned is asked about after the
 * navigation, once the screen has stopped rebuilding -- a prompt is an
 * overlay on this screen, and this bar is on its way somewhere. */
static void back_event(lv_event_t *e)
{
    LV_UNUSED(e);

    const char *restart_label = NULL;

    page_leave(&restart_label);

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

    restart_prompt(restart_label);
}

/* A close glyph at the root of the index, a chevron everywhere else: the bar
 * shows where back actually goes, and from the root that is out. */
static void back_bar_create(const char *title)
{
    ui_back_bar(screen, (current_tab == MENU_ROOT) ? LV_SYMBOL_CLOSE : LV_SYMBOL_LEFT,
                title, BAR_HEIGHT, back_event);
}

static void index_event(lv_event_t *e)
{
    uint8_t target = (uint8_t)(uintptr_t)lv_event_get_user_data(e);

    /* Arriving at a section is arriving at its first page -- the openHAB
     * manual page, the Icons catalogue's, the Systeminfo screen's, the Fonts
     * screen's and the paged sections' alike. Not in screen_show_target(),
     * which is also what ui_settings_rebuild() goes through: a theme change
     * -- including the automatic night one, at any moment -- must leave the
     * page it happens on where it was. */
    openhab_manual = false;
    icons_page = 0;
    info_page = 0;
    fonts_page = 0;

    if (target < SETTINGS_TAB_COUNT)
        field_page[target] = 0;

    /* Here and not in screen_show_target(): that function is also the
     * programmatic entry from ui_settings_open(), which is how a pristine
     * device lands on the WLAN tab with nobody having touched anything. */
    BEEPER_EVENT_LINK();
    screen_show_target(target);
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

/* A flex-grow nothing, for centring a footer's pager: the status label's
 * grow goes to one of these either side of the group instead, which pins it
 * to the middle of the bar whatever the number between the arrows is. */
static lv_obj_t *footer_spacer(lv_obj_t *footer)
{
    lv_obj_t *spacer = ui_plain_container(footer);

    lv_obj_set_size(spacer, 0, 0);
    lv_obj_set_flex_grow(spacer, 1);

    return spacer;
}

/* The shared page turner, and the whole of the multi-page style: arrows
 * either side of the page number, dead centre in the bar, the same on every
 * screen that pages.
 *
 * The status label goes hidden rather than merely empty: a zero-width child
 * still pays the bar's column gap, and the pager would sit two pixels right
 * of the middle. Nothing a paged screen does fills the label -- the screens
 * that page are the read-only ones -- but status_set() brings it back if one
 * ever does, at the price of the pager's exact centre.
 *
 * The number is fixed-width, the text centred in it: a "1/4" narrower than a
 * "4/4" by a hair would rock both arrows a pixel each way once a page, and
 * the number is the one thing in the group that changes when they are
 * tapped. Measured against the widest page the screen can show, so a tenth
 * page would widen it honestly rather than dot it. */
static void footer_pager(uint8_t tab, lv_obj_t *footer, lv_event_cb_t event, uint16_t page,
                        uint16_t pages)
{
    const lv_font_t *font = ui_style_theme()->font_normal;
    int32_t          number_w = 0;

    lv_obj_add_flag(tab_status[tab], LV_OBJ_FLAG_HIDDEN);

    footer_spacer(footer);

    lv_obj_add_event_cb(ui_themed_button(footer, LV_SYMBOL_LEFT), event, LV_EVENT_CLICKED,
                        (void *)(uintptr_t)PAGER_PREV);

    for (unsigned p = 1; p <= pages; p++)
    {
        /* Sized for the widest "65535/65535": pages is a runtime count here,
         * so the compiler can no longer prove what the constant page count of
         * the screen this was written for used to make obvious. */
        char    text[12];
        int32_t w = 0;

        snprintf(text, sizeof(text), "%u/%u", p, (unsigned)pages);

        for (const char *c = text; *c != '\0'; c++)
            w += lv_font_get_glyph_width(font, *c, *(c + 1))
                 + ui_style_theme()->letter_space;

        if (w > number_w)
            number_w = w;
    }

    char     text[12];
    lv_obj_t *leaf = lv_label_create(footer);

    snprintf(text, sizeof(text), "%u/%u", (unsigned)page + 1, (unsigned)pages);
    lv_label_set_text(leaf, text);
    lv_obj_set_width(leaf, number_w);
    lv_obj_set_style_text_align(leaf, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_add_event_cb(ui_themed_button(footer, LV_SYMBOL_RIGHT), event, LV_EVENT_CLICKED,
                        (void *)(uintptr_t)PAGER_NEXT);

    footer_spacer(footer);
}

/* One section: the bar, the section's list of rows, and a footer carrying
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
     * done nothing. The Systeminfo screen's pages get the same treatment in
     * the other direction: the section's name names four pages, and the bar
     * says which of them is showing. */
    char title[48];

    snprintf(title, sizeof(title), "%s",
             (tab == SETTINGS_TAB_OPENHAB && openhab_manual == true) ? "openHAB Server"
                                                                     : target_title(tab));

    if (tab == SETTINGS_TAB_INFO)
        snprintf(title + strlen(title), sizeof(title) - strlen(title), " (%s)",
                 info_page_names[info_page]);

    back_bar_create(title);

    int32_t vres = lv_display_get_vertical_resolution(NULL);

    lv_obj_t *rows = ui_plain_container(screen);

    lv_obj_set_pos(rows, 0, BAR_HEIGHT);
    lv_obj_set_size(rows, lv_pct(100), vres - BAR_HEIGHT - FOOTER_HEIGHT);
    lv_obj_set_scrollable(rows, false);
    lv_obj_set_flex_flow(rows, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(rows, 4, 0);
    lv_obj_set_style_pad_row(rows, 4, 0);

    /* The Info pages are the tightest fit, and the fullest of them -- six
     * address rows -- fits only with the padding reduced: at the tallest small
     * font it measures 138 of the 140 px between bar and footer, which the
     * 4 px padding alone would push past. The gaps between the lines go with
     * it -- each page draws its own. */
    if (tab == SETTINGS_TAB_INFO)
    {
        lv_obj_set_style_pad_ver(rows, 2, 0);
        lv_obj_set_style_pad_row(rows, 0, 0);
    }

    tab_rows[tab] = rows;

    lv_obj_t *footer = lv_obj_create(screen);

    lv_obj_set_scrollable(footer, false);
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
        /* Read-only, with nothing to scan for: the pager is the only thing
         * this screen's footer carries. */
        footer_pager(tab, footer, info_page_event, info_page, INFO_PAGE_COUNT);
    }
    else if (tab == SETTINGS_TAB_FONTS)
    {
        /* Read-only, like Systeminfo: the rows on it are samples, not
         * fields. The pager turns the families, one page each, in the same
         * footer every paged screen wears. */
        footer_pager(tab, footer, fonts_page_event, fonts_page, FONT_FACE_COUNT);
    }
    else
    {
        /* The one control that is no longer here is Save: leaving the page
         * commits, and a button that did what was going to happen anyway
         * was a button that only taught the wrong habit. What is left is
         * what a page can *do* -- look again, listen, calibrate, go deeper
         * -- and then the pager, if its rows did not fit. */
        if (tab == SETTINGS_TAB_AUDIO)
        {
            lv_obj_add_event_cb(ui_themed_button(footer, "Test"), audio_test_event,
                                LV_EVENT_CLICKED, NULL);

            /* Only where there is something to demonstrate. The polyphonic
             * engine has no tune, and a button that reported "nothing to
             * play" would be worse than an absent one -- see beeper_song.h. */
            if (beeper_demo_available() == true)
            {
                audio_demo_button = ui_themed_button(footer, "Demo");

                lv_obj_add_event_cb(audio_demo_button, audio_demo_event,
                                    LV_EVENT_CLICKED, NULL);

                /* Built mid-tune if the user left the page and came back: the
                 * label has to arrive saying Stop. */
                audio_demo_refresh();
            }
        }
        else if (tab == SETTINGS_TAB_TOUCH)
        {
            /* Only where there is something to calibrate. A capacitive panel
             * reports the pixel grid it is bonded to; there is no origin and
             * no span, and a procedure offered anyway could only write four
             * numbers that the pointer then ignores. The rows stay, because
             * a config.json is read by whichever board it lands on. */
            if (port_indev_calibratable() == true)
            {
                lv_obj_add_event_cb(ui_themed_button(footer, "Calibrate"), calibrate_event,
                                    LV_EVENT_CLICKED, NULL);
            }
            else
            {
                /* Said rather than left to be inferred from a missing
                 * button. The rows above are still there and still
                 * editable, because a config.json is read by whichever
                 * board it lands on. */
                status_set(tab, "This panel needs none");
            }
        }
        else if (tab == SETTINGS_TAB_WLAN)
        {
            lv_obj_add_event_cb(ui_themed_button(footer, "Scan"), scan_event,
                                LV_EVENT_CLICKED, NULL);
        }
        else if (tab == SETTINGS_TAB_ICONS)
        {
            /* The catalogue's pages, in the shared pager like the read-only
             * screens'. Only when there is more than one of them: a
             * single-page catalogue -- and a build without the icon set at
             * all, which gets the note instead -- is a page with nothing
             * to turn. */
            uint16_t pages = icons_pages();

            if (pages > 1)
                footer_pager(tab, footer, icons_page_event, icons_page, pages);
        }
        else if (tab == SETTINGS_TAB_OPENHAB && openhab_manual == false)
        {
            /* Scan is the counterpart of the WLAN page's, and there for the
             * same cases: a server that was still starting when the page
             * opened, or a sitemap that has only just been written. Manual
             * is the way to the fields, for everything the network did not
             * offer. */
            lv_obj_add_event_cb(ui_themed_button(footer, "Scan"), openhab_scan_event,
                                LV_EVENT_CLICKED, NULL);
            lv_obj_add_event_cb(ui_themed_button(footer, "Manual"), openhab_manual_event,
                                LV_EVENT_CLICKED, NULL);
        }

        /* The pager, for the tabs whose rows come off the shared table and
         * did not fit on one page -- one logical group per page, turned by
         * the footer like every paged screen this UI has. The count is
         * asked for here, before the rows are built, because the footer is
         * built first and the walk is what knows. The clamp matters a theme
         * change from now: the capacity follows the small font, and a page
         * that no longer exists is not the one to be showing when the
         * rebuild settles -- the nearest page, not the first, because a
         * theme change is not a navigation. */
        if (tab != SETTINGS_TAB_WLAN && tab != SETTINGS_TAB_ICONS &&
            (tab != SETTINGS_TAB_OPENHAB || openhab_manual == true))
        {
            field_pages[tab] = field_pages_walk(tab, false);

            if (field_page[tab] >= field_pages[tab])
                field_page[tab] = (uint8_t)(field_pages[tab] - 1);

            if (field_pages[tab] > 1)
                footer_pager(tab, footer, fields_page_event, field_page[tab],
                            field_pages[tab]);
        }
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

    /* Not the Fonts screen. Its lines are the subject rather than the
     * furniture: a specimen that fades and slides in is briefly not the
     * shape its family draws it in, and a page turned to compare type wants
     * every line at its final place and weight the moment it appears. */
    if (tab != SETTINGS_TAB_FONTS)
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
         * stacking. The page being left commits like any other -- this is the
         * control interface's way around the screen, and it owes the draft
         * the same save the back bar does -- and arriving at the Icons
         * catalogue is arriving at its first page, the same as every way of
         * arriving below. */
        const char *restart_label = NULL;

        page_leave(&restart_label);

        icons_page = 0;

        if (tab < SETTINGS_TAB_COUNT)
            field_page[tab] = 0;

        screen_show_target(tab);

        restart_prompt(restart_label);
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

    if (tab < SETTINGS_TAB_COUNT)
        field_page[tab] = 0;

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

    /* Closing is leaving a page too -- the last one, and often straight from
     * a section (the dimmed clock pushes in, the control interface asks for
     * `settings`). The draft commits with no restart offered: the prompt is
     * an overlay on a screen that is on its way out, and a reboot that
     * answered a close the user had already moved on from would be a
     * surprise on a different scale than the ones this screen gives. */
    page_leave(NULL);

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

    /* Undo whatever Test applied out of the draft. Usually a no-op by now,
     * because leaving the Audio page commits the draft and the values are
     * the live config's -- but the close can come while the page is still
     * up, and a volume tested and abandoned is not a volume to keep. */
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
