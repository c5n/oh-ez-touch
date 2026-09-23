/**
 * @file testif_report.cpp
 *
 * The dumps, and the two commands that move the UI somewhere without touching
 * it: what is on screen, what the system is doing, what the settings say.
 *
 * JSON because the consumer is a script, and ArduinoJson because the firmware
 * already carries it -- the sitemap parser is built on it. A dump is assembled
 * and serialised into the caller's buffer, which is the reply datagram's, so
 * there is one size limit and it is checked in one place.
 *
 * Everything here reads. The two exceptions say so in their names: `set`
 * writes a setting, and `nav`/`settings` open a screen.
 */

#include "sdkconfig.h"

#if CONFIG_IDF_TARGET_LINUX || CONFIG_OHEZ_TESTIF

#include <stdio.h>
#include <string.h>
#include <strings.h>

#include <ArduinoJson.h>

#include "config/config.hpp"
#include "config/config_fields.hpp"
#include "control/backlight_control.hpp"
#include "mqtt/ohez_mqtt.hpp"
#include "net/wlan.hpp"
#include "openhab/openhab_connector.hpp"
#include "port/ohez_port.h"
#include "ui/items/item_screen.hpp"
#include "ui/openhab_ui.hpp"
#include "ui/ui_calibration.hpp"
#include "ui/ui_frame_stats.h"
#include "ui/ui_messagebox.hpp"
#include "ui/ui_screen.hpp"
#include "ui/ui_settings.hpp"
#include "ui/ui_style.hpp"
#include "version.h"

#include "testif_internal.hpp"

#ifndef TARGET_NAME
#define TARGET_NAME "unknown"
#endif

extern Config          config;
extern BacklightControl tft_backlight;

/* The item types by name. Kept here rather than beside the enum in
 * openhab_connector.hpp so that the device carries no table it never reads;
 * the order is the enum's, and a type added there without a name here comes
 * out as "unknown" rather than reading off the end. */
static const char *const item_type_names[] = {
    "unknown", "parent_link", "link",   "group",       "number",
    "string",  "setpoint",    "slider", "selection",   "colorpicker",
    "switch",  "rollershutter", "player",
};

static const char *item_type_name(enum ItemType type)
{
    size_t index = (size_t)type;

    if (index >= sizeof(item_type_names) / sizeof(item_type_names[0]))
        return "unknown";

    return item_type_names[index];
}

static const char *screen_name(void)
{
    switch (ui_screen_top())
    {
    case UI_SCREEN_NONE:     return "page";
    case UI_SCREEN_ITEM:     return "item";
    case UI_SCREEN_SETTINGS: return "settings";
    case UI_SCREEN_CLOCK:    return "clock";
    }

    return "unknown";
}

static const char *wlan_state_name(void)
{
    switch (wlan_state())
    {
    case WLAN_IDLE:       return "idle";
    case WLAN_CONNECTING: return "connecting";
    case WLAN_ONLINE:     return "online";
    case WLAN_RETRY_WAIT: return "retry";
    case WLAN_PORTAL:     return "portal";
    }

    return "unknown";
}

static const char *banner_kind_name(enum Messagebox::messagebox_type_e kind)
{
    switch (kind)
    {
    case Messagebox::INFO:    return "info";
    case Messagebox::WARNING: return "warning";
    case Messagebox::ERROR:   return "error";
    }

    return "unknown";
}

/**
 * Serialise into the reply buffer.
 *
 * Refused rather than truncated: half a JSON document is not a smaller answer,
 * it is one that a client cannot parse at all, and a test that silently read a
 * truncated dump would assert on the tiles that happened to fit.
 */
static const char *emit(const JsonDocument &doc, char *out, size_t out_size)
{
    size_t written = serializeJson(doc, out, out_size);

    if (written == 0 || written >= out_size)
    {
        out[0] = '\0';
        return "reply too large";
    }

    return NULL;
}

/* ------------------------------------------------------------------ screen */

static void add_banner(JsonDocument &doc)
{
    /* Whichever is up. main.cpp's covers the WLAN and the setup access point,
     * openhab_ui.cpp's covers a sitemap that will not load; both live on the
     * top layer and only one of them is ever interesting at a time. */
    const Messagebox *up = NULL;

    if (messagebox.isUp() == true)
        up = &messagebox;
    else if (openhab_ui_messagebox.isUp() == true)
        up = &openhab_ui_messagebox;

    if (up == NULL)
        return;

    JsonObject banner = doc["banner"].to<JsonObject>();

    /* The topic and the text separately, because the box keeps them apart --
     * the topic is its header title and the text its content. They used to be
     * one "topic\ntext" string because the banner was one label. */
    banner["kind"]   = banner_kind_name(up->getKind());
    banner["topic"]  = up->getTopic();
    banner["text"]   = up->getText();

    /* Folded away by the user, and reachable only through the frame's notice
     * indicator. Still up, so still reported -- a test that taps the indicator
     * needs to be able to see that it worked. */
    banner["hidden"] = up->isFolded();

    /* Whether this one carries the Restart button in its footer. Reported for
     * the same reason as `hidden`: it is a button a test can be told to find,
     * and the one thing on the panel that must never be tapped by accident. */
    banner["restart"] = up->offersRestart();
}

const char *testif_cmd_screen(const testif_cmd_t *cmd, char *out, size_t out_size)
{
    (void)cmd;

    JsonDocument doc;

    doc["screen"] = screen_name();

    JsonObject page = doc["page"].to<JsonObject>();

    page["title"]      = openhab_ui_page_title();
    page["state"]      = openhab_ui_page_state_name();
    page["generation"] = openhab_ui_page_generation();

    JsonArray tiles = page["tiles"].to<JsonArray>();

    for (size_t i = 0; i < openhab_ui_tile_count(); i++)
    {
        struct openhab_ui_tile_s info;

        if (openhab_ui_tile_info(i, &info) == false)
            break;

        JsonObject tile = tiles.add<JsonObject>();

        tile["i"]     = i;
        tile["label"] = info.label;
        tile["state"] = info.state;
        tile["type"]  = item_type_name(info.type);
        tile["x"]     = info.x;
        tile["y"]     = info.y;
        tile["w"]     = info.w;
        tile["h"]     = info.h;
    }

    JsonObject item = doc["item"].to<JsonObject>();

    item["open"] = item_screen_is_open();

    if (item_screen_is_open() == true)
    {
        item["type"] = item_type_name(item_screen_open_type());
        item["slot"] = item_screen_open_slot();
    }

    JsonObject settings = doc["settings"].to<JsonObject>();

    settings["open"] = ui_settings_is_open();

    if (ui_settings_is_open() == true)
        settings["page"] = ui_settings_page_name();

    JsonObject theme = doc["theme"].to<JsonObject>();

    theme["family"] = ui_theme_name(config.item.ui.theme);
    theme["night"]  = openhab_ui_night_active(&config);

    JsonObject backlight = doc["backlight"].to<JsonObject>();

    backlight["brightness"] = tft_backlight.currentBrightness();
    backlight["dimmed"]     = tft_backlight.isDimmed();

    add_banner(doc);

    return emit(doc, out, out_size);
}

/* ------------------------------------------------------------------ status */

const char *testif_cmd_status(const testif_cmd_t *cmd, char *out, size_t out_size)
{
    (void)cmd;

    JsonDocument doc;

    char version[16];

    snprintf(version, sizeof(version), "%u.%02u", VERSION_MAJOR, VERSION_MINOR);

    doc["version"] = version;
    doc["git"]     = VERSION_GIT_HASH;
    doc["target"]  = TARGET_NAME;
    doc["build"]   = __DATE__ " " __TIME__;
    doc["uptime"]  = port_millis();
    doc["heap"]    = port_free_heap();

    port_net_info_t net;

    port_net_info(&net);

    JsonObject network = doc["net"].to<JsonObject>();

    network["state"]     = wlan_state_name();
    network["hostname"]  = net.hostname;
    network["ssid"]      = net.ssid;
    network["mac"]       = net.mac;
    network["ip"]        = net.ip;
    network["connected"] = net.connected;

    /* A host has no radio, and PORT_NET_RSSI_WIRED is the value that says so.
     * Reporting it as a percentage would invent a signal strength. */
    if (net.rssi != PORT_NET_RSSI_WIRED)
    {
        network["rssi"]    = net.rssi;
        network["quality"] = openhab_ui_signal_quality(net.rssi);
    }

    JsonObject openhab = doc["openhab"].to<JsonObject>();

    openhab["host"]    = config.item.openhab.hostname;
    openhab["port"]    = config.item.openhab.port;
    openhab["sitemap"] = config.item.openhab.sitemap;
    openhab["page"]    = openhab_ui_page_state_name();

    doc["mqtt"]["connected"] = ohez_mqtt_connected();

    /* The whole object, not a frame rate: what a script wants to assert on is
     * the split between "render" and "wait", because that is what says whether
     * a change made the renderer faster or only made it wait longer. "valid" is
     * false until a window with frames in it has closed, and a harness that
     * reads the others before then is reading zeroes. */
    ui_frame_stats_t stats;

    ui_frame_stats_get((uint32_t)port_millis(), &stats);

    JsonObject frame = doc["frame"].to<JsonObject>();

    frame["valid"]      = stats.valid;
    frame["age_ms"]     = stats.age_ms;
    frame["window_ms"]  = stats.window_ms;
    frame["frames"]     = stats.frames;
    frame["fps"]        = stats.fps_x10 / 10.0;
    frame["frame_us"]   = stats.frame_us;
    frame["worst_us"]   = stats.frame_us_max;
    frame["render_us"]  = stats.render_us;
    frame["wait_us"]    = stats.wait_us;
    frame["pixels"]     = stats.pixels;
    frame["flushes"]    = stats.flushes_x10 / 10.0;

    return emit(doc, out, out_size);
}

/* ------------------------------------------------------------------- heap */

/* The heap's shape, as five numbers rather than the one `status` carries.
 *
 * The pair free/largest is the diagnosis a page-fetch failure already prints;
 * the block counts are what tells a leak apart from fragmentation when the
 * two move together, and min_free_ever is the high-water mark of harm since
 * boot that no spot reading of `free` can see. See port_sys.h. */
const char *testif_cmd_heap(const testif_cmd_t *cmd, char *out, size_t out_size)
{
    (void)cmd;

    struct port_heap_info_s info;

    port_heap_info(&info);

    JsonDocument doc;

    doc["free"]          = info.free;
    doc["largest"]       = info.largest;
    doc["min_free_ever"] = info.min_free_ever;
    doc["alloc_blocks"]  = info.alloc_blocks;
    doc["free_blocks"]   = info.free_blocks;

    return emit(doc, out, out_size);
}

/* ------------------------------------------------------------------ config */

static void add_field(JsonObject into, const struct config_field_s *f)
{
    char value[64];

    config_field_value_text(f, &config.item, value, sizeof(value));

    /* The MQTT client refuses to publish these for the same reason: a
     * password is not telemetry, and the interface is a convenience rather
     * than a reason to put one on a socket. */
    into[f->name] = (f->flags & SETTINGS_F_SECRET) ? "***" : value;
}

const char *testif_cmd_config(const testif_cmd_t *cmd, char *out, size_t out_size)
{
    JsonDocument doc;

    JsonObject fields = doc.to<JsonObject>();

    if (cmd->argc > 1)
    {
        const struct config_field_s *f = config_field_by_name(cmd->argv[1]);

        if (f == NULL)
            return "no such setting";

        add_field(fields, f);

        return emit(doc, out, out_size);
    }

    for (size_t i = 0; i < config_field_count; i++)
    {
        /* Section rows are headings on the settings screen, not settings. */
        if (config_fields[i].name == NULL)
            continue;

        add_field(fields, &config_fields[i]);
    }

    return emit(doc, out, out_size);
}

const char *testif_cmd_set(const testif_cmd_t *cmd, char *out, size_t out_size)
{
    (void)out;
    (void)out_size;

    if (cmd->argc < 3)
        return "want a name and a value";

    const struct config_field_s *f = config_field_by_name(cmd->argv[1]);

    if (f == NULL)
        return "no such setting";

    config.lock();

    bool stored = (f->kind == SETTINGS_TEXT)
                      ? config_field_set_text(f, &config.item, cmd->argv[2])
                      : false;

    if (f->kind != SETTINGS_TEXT)
    {
        if (f->kind == SETTINGS_ENUM)
        {
            /* Matched here rather than through config_field_enum_from_name(),
             * which falls back to the first option for a name it does not
             * know -- the right thing for a config file, and the wrong thing
             * for a command, where a misspelt theme would silently select
             * another one and the test would be asserting on nothing. */
            for (uint8_t i = 0; i < f->count; i++)
            {
                if (strcasecmp(cmd->argv[2], f->names[i]) != 0)
                    continue;

                config_field_write(f, &config.item, i);
                stored = true;
                break;
            }
        }
        else
        {
            long value = 0;

            if (testif_arg_int(cmd, 2, &value) == true)
            {
                config_field_set_number(f, &config.item, value);
                stored = true;
            }
        }
    }

    config.unlock();

    if (stored == false)
        return "value not accepted";

    /* The same two steps a save from the web form takes, and in the same
     * order: persist, then re-apply what does not need a reboot. Without the
     * second a `set` would change the file and not the panel. */
    config.saveConfig();
    settings_apply_live(&config);

    return NULL;
}

/* -------------------------------------------------------------- navigation */

const char *testif_cmd_nav(const testif_cmd_t *cmd, char *out, size_t out_size)
{
    (void)out;
    (void)out_size;

    if (cmd->argc < 2)
        return "want a tile path";

    return openhab_ui_open_item_path(cmd->argv[1]) ? NULL : "bad path";
}

const char *testif_cmd_settings(const testif_cmd_t *cmd, char *out, size_t out_size)
{
    (void)out;
    (void)out_size;

    /* With nothing more specific in mind, close it -- which is what makes
     * `settings` and `settings <page>` a pair a script can bracket with. */
    if (cmd->argc < 2)
    {
        ui_settings_close();
        return NULL;
    }

    /* The same lookup OHEZ_SETTINGS uses, so the two cannot drift apart on
     * what a page is called. */
    return ui_settings_open_by_name(cmd->argv[1]) ? NULL : "no such settings page";
}

/* The touchscreen calibration, in the three pieces a script needs: start it,
 * find out where to tap, and read what came out.
 *
 * `targets` exists so that a test taps what the firmware drew rather than four
 * coordinates copied into the script. The inset is a tenth of each axis today;
 * a layout that moved it would quietly turn every such script into a test of
 * the wrong thing, passing for the wrong reason -- the same trap the `screen`
 * dump's rectangles exist to avoid.
 *
 * `result` reports the two figures the screen puts in words, so an assertion
 * can be made about the correction itself rather than about a screenshot.
 */
const char *testif_cmd_calibrate(const testif_cmd_t *cmd, char *out, size_t out_size)
{
    if (cmd->argc < 2)
    {
        if (ui_settings_is_open() == false)
            return "settings not open";

        if (port_indev_calibratable() == false)
            return "panel needs no calibration";

        ui_calibration_open();

        return ui_calibration_is_open() ? NULL : "calibration did not open";
    }

    if (strcmp(cmd->argv[1], "targets") == 0)
    {
        int32_t  pairs[TOUCH_CAL_SAMPLES * 2];
        unsigned count = ui_calibration_targets(pairs, TOUCH_CAL_SAMPLES);
        size_t   used = 0;

        if (count == 0)
            return "no target showing";

        for (unsigned i = 0; i < count; i++)
        {
            used += (size_t)snprintf(out + used, out_size - used, "%s%d %d",
                                     (i == 0) ? "" : " ",
                                     (int)pairs[i * 2], (int)pairs[(i * 2) + 1]);
        }

        return NULL;
    }

    if (strcmp(cmd->argv[1], "step") == 0)
    {
        unsigned taken = 0;
        unsigned total = 0;

        ui_calibration_progress(&taken, &total);
        snprintf(out, out_size, "%u %u", taken, total);

        return NULL;
    }

    if (strcmp(cmd->argv[1], "result") == 0)
    {
        struct touch_cal_s was;
        struct touch_cal_s now;
        int32_t            worst = 0;
        int32_t            residual = 0;

        if (ui_calibration_result(&was, &now, &worst, &residual) == false)
            return "no result";

        snprintf(out, out_size, "%d %d %d %d %d %d %d %d %d %d",
                 (int)was.x_origin, (int)was.x_span, (int)was.y_origin, (int)was.y_span,
                 (int)now.x_origin, (int)now.x_span, (int)now.y_origin, (int)now.y_span,
                 (int)worst, (int)residual);

        return NULL;
    }

    return "want step, targets or result";
}

#endif /* CONFIG_IDF_TARGET_LINUX || CONFIG_OHEZ_TESTIF */
