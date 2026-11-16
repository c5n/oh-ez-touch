/**
 * @file test_ha_model.cpp
 *
 * The Home Assistant connector's pure parts: the WebSocket frame header, the
 * Lovelace view to tiles, the entity states to tile types and panel states,
 * and the panel's commands to call_service messages.
 *
 * The fixtures are real answers of Home Assistant 2026.11.1 with the demo
 * integration: fixtures/ha_dashboard.json is lovelace/config for a test
 * dashboard, fixtures/ha_states.json the first message of subscribe_entities
 * for sixteen of its entities (context ids and the media player's picture
 * URL removed).
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>

#include <unity.h>

#include "homeassistant/ha_model.hpp"
#include "homeassistant/ha_ws.h"
#include "icons/icon_set.hpp"
#include "test_suites.hpp"

static std::string load(const char *name)
{
    char path[512];

    snprintf(path, sizeof(path), "%s/%s", TEST_FIXTURE_DIR, name);

    FILE *f = fopen(path, "rb");

    TEST_ASSERT_NOT_NULL_MESSAGE(f, path);

    std::string text;
    char        buf[1024];
    size_t      n;

    while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
        text.append(buf, n);

    fclose(f);

    return text;
}

/* ------------------------------------------------------------ frame headers */

static void test_ws_header_lengths(void)
{
    const uint8_t mask[4] = {1, 2, 3, 4};
    uint8_t       h[HA_WS_HEADER_MAX];
    struct ha_ws_header_s d;

    TEST_ASSERT_EQUAL(6, ha_ws_encode_header(h, HA_WS_OP_TEXT, 125, mask));
    TEST_ASSERT_EQUAL_HEX8(0x81, h[0]);
    TEST_ASSERT_EQUAL_HEX8(0x80 | 125, h[1]);

    TEST_ASSERT_EQUAL(8, ha_ws_encode_header(h, HA_WS_OP_TEXT, 126, mask));
    TEST_ASSERT_EQUAL(1, ha_ws_decode_header(h, 8, &d));
    TEST_ASSERT_EQUAL(126, d.length);
    TEST_ASSERT_TRUE(d.masked);
    TEST_ASSERT_EQUAL_MEMORY(mask, d.mask, 4);

    TEST_ASSERT_EQUAL(14, ha_ws_encode_header(h, HA_WS_OP_BIN, 70000, mask));
    TEST_ASSERT_EQUAL(1, ha_ws_decode_header(h, 14, &d));
    TEST_ASSERT_EQUAL(70000, d.length);
    TEST_ASSERT_EQUAL(HA_WS_OP_BIN, d.opcode);
    TEST_ASSERT_EQUAL(14, d.header_len);
}

static void test_ws_decode_server_frames(void)
{
    struct ha_ws_header_s d;

    /* A server frame is not masked. */
    const uint8_t text[] = {0x81, 0x05};

    TEST_ASSERT_EQUAL(1, ha_ws_decode_header(text, sizeof(text), &d));
    TEST_ASSERT_TRUE(d.fin);
    TEST_ASSERT_FALSE(d.masked);
    TEST_ASSERT_EQUAL(5, d.length);
    TEST_ASSERT_EQUAL(2, d.header_len);

    /* The first frame of a fragmented message, length in 16 bits. */
    const uint8_t first[] = {0x01, 126, 0x40, 0x00};

    TEST_ASSERT_EQUAL(0, ha_ws_decode_header(first, 3, &d));
    TEST_ASSERT_EQUAL(1, ha_ws_decode_header(first, 4, &d));
    TEST_ASSERT_FALSE(d.fin);
    TEST_ASSERT_EQUAL(16384, d.length);

    /* Refused: a reserved bit, a long ping, a fragmented close. */
    const uint8_t rsv[] = {0xC1, 0x01};
    const uint8_t long_ping[] = {0x89, 126, 0x00, 0x80};
    const uint8_t frag_close[] = {0x08, 0x00};

    TEST_ASSERT_EQUAL(-1, ha_ws_decode_header(rsv, sizeof(rsv), &d));
    TEST_ASSERT_EQUAL(-1, ha_ws_decode_header(long_ping, sizeof(long_ping), &d));
    TEST_ASSERT_EQUAL(-1, ha_ws_decode_header(frag_close, sizeof(frag_close), &d));
}

/* -------------------------------------------------------------------- views */

static void test_ref_and_view_match(void)
{
    char view[HA_VIEW_LEN];

    ha_ref_view("ha:ohez-panel", view, sizeof(view));
    TEST_ASSERT_EQUAL_STRING("", view);
    ha_ref_view("ha:ohez-panel/media", view, sizeof(view));
    TEST_ASSERT_EQUAL_STRING("media", view);
    ha_ref_view("ha:/#2", view, sizeof(view));
    TEST_ASSERT_EQUAL_STRING("#2", view);

    TEST_ASSERT_TRUE(ha_view_matches("", 0, "home"));
    TEST_ASSERT_FALSE(ha_view_matches("", 1, "media"));
    TEST_ASSERT_TRUE(ha_view_matches("media", 1, "media"));
    TEST_ASSERT_FALSE(ha_view_matches("media", 1, NULL));
    TEST_ASSERT_TRUE(ha_view_matches("#2", 2, NULL));
}

/* Build view `index` of the fixture dashboard the way the connector does:
 * each view through the filter, the views list from all of them. */
static void build_view(size_t index, struct ha_page_s *page, struct ha_view_s *views,
                       size_t *view_count)
{
    std::string  text = load("ha_dashboard.json");
    JsonDocument all;

    TEST_ASSERT_FALSE(deserializeJson(all, text));

    JsonArrayConst list = all["result"]["views"];

    *view_count = 0;

    for (JsonVariantConst v : list)
    {
        ha_view_summary(v, *view_count, &views[*view_count]);
        (*view_count)++;
    }

    JsonDocument filter;
    JsonDocument view;
    std::string  one;

    ha_view_filter(filter);
    serializeJson(list[index], one);
    TEST_ASSERT_FALSE(deserializeJson(view, one, DeserializationOption::Filter(filter)));

    memset(page, 0, sizeof(*page));
    ha_view_build(view.as<JsonVariantConst>(), index == 0, "Clock", views, *view_count, page);
}

static struct ha_page_s page;

static void test_view_summaries(void)
{
    struct ha_view_s views[HA_VIEW_MAX];
    size_t           count;

    build_view(0, &page, views, &count);

    TEST_ASSERT_EQUAL(3, count);
    TEST_ASSERT_EQUAL_STRING("Home", views[0].title);
    TEST_ASSERT_EQUAL_STRING("home", views[0].target);
    TEST_ASSERT_EQUAL_STRING("media", views[1].target);
    TEST_ASSERT_TRUE(views[1].subview);
    TEST_ASSERT_FALSE(views[2].subview);
}

static void test_root_view_tiles_and_clock(void)
{
    struct ha_view_s views[HA_VIEW_MAX];
    size_t           count;

    build_view(0, &page, views, &count);

    TEST_ASSERT_TRUE(page.root);
    TEST_ASSERT_EQUAL_STRING("Home", page.title);
    TEST_ASSERT_EQUAL(ITEM_COUNT_MAX, page.tile_count);

    /* The cards in order; the entities card titled "Clock" is not a tile. */
    TEST_ASSERT_EQUAL_STRING("light.ceiling_lights", page.tiles[0].entity);
    TEST_ASSERT_EQUAL_STRING("tile", page.tiles[0].card);
    TEST_ASSERT_EQUAL_STRING("switch.decorative_lights", page.tiles[1].entity);
    TEST_ASSERT_EQUAL_STRING("Deko", page.tiles[1].name);
    TEST_ASSERT_EQUAL_STRING("sensor.outside_temperature", page.tiles[2].entity);
    TEST_ASSERT_EQUAL_STRING("cover.living_room_window", page.tiles[3].entity);
    TEST_ASSERT_EQUAL_STRING("thermostat", page.tiles[4].card);

    /* The navigate button. */
    TEST_ASSERT_EQUAL(HA_TILE_LINK, page.tiles[5].kind);
    TEST_ASSERT_EQUAL_STRING("media", page.tiles[5].view);
    TEST_ASSERT_EQUAL_STRING("Media", page.tiles[5].name);

    TEST_ASSERT_EQUAL(2, page.clock_count);
    TEST_ASSERT_EQUAL_STRING("sensor.outside_temperature", page.clock[0].entity);
    TEST_ASSERT_EQUAL_STRING("sensor.outside_humidity", page.clock[1].entity);
}

static void test_sections_subview(void)
{
    struct ha_view_s views[HA_VIEW_MAX];
    size_t           count;

    build_view(1, &page, views, &count);

    TEST_ASSERT_FALSE(page.root);
    TEST_ASSERT_EQUAL(5, page.tile_count);
    TEST_ASSERT_EQUAL(HA_TILE_BACK, page.tiles[0].kind);
    TEST_ASSERT_EQUAL_STRING("Home", page.tiles[0].name);

    /* The heading card is no tile. */
    TEST_ASSERT_EQUAL_STRING("media_player.living_room", page.tiles[1].entity);
    TEST_ASSERT_EQUAL_STRING("number.volume", page.tiles[2].entity);
    TEST_ASSERT_EQUAL_STRING("select.speed", page.tiles[3].entity);
    TEST_ASSERT_EQUAL_STRING("light", page.tiles[4].card);
    TEST_ASSERT_EQUAL(0, page.clock_count);
}

static void test_glance_and_stack_flattened(void)
{
    struct ha_view_s views[HA_VIEW_MAX];
    size_t           count;

    build_view(2, &page, views, &count);

    TEST_ASSERT_EQUAL(6, page.tile_count);
    TEST_ASSERT_EQUAL_STRING("binary_sensor.movement_backyard", page.tiles[1].entity);
    TEST_ASSERT_EQUAL_STRING("glance", page.tiles[1].card);
    TEST_ASSERT_EQUAL_STRING("lock.front_door", page.tiles[2].entity);
    TEST_ASSERT_EQUAL_STRING("sensor.outside_humidity", page.tiles[3].entity);
    TEST_ASSERT_EQUAL_STRING("light.bed_light", page.tiles[4].entity);
    TEST_ASSERT_EQUAL_STRING("fan.living_room_fan", page.tiles[5].entity);
}

static void test_root_links_to_other_views(void)
{
    struct ha_view_s views[HA_VIEW_MAX];
    size_t           count;

    build_view(0, &page, views, &count);

    /* With room on the page: a link to "Outside", none to the subview, and
     * none twice. */
    page.tile_count = 2;
    ha_page_add_view_links(&page, views, count);

    TEST_ASSERT_EQUAL(3, page.tile_count);
    TEST_ASSERT_EQUAL(HA_TILE_LINK, page.tiles[2].kind);
    TEST_ASSERT_EQUAL_STRING("outside", page.tiles[2].view);
    TEST_ASSERT_EQUAL_STRING("Outside", page.tiles[2].name);

    ha_page_add_view_links(&page, views, count);
    TEST_ASSERT_EQUAL(3, page.tile_count);
}

/* ----------------------------------------------------------------- entities */

static JsonDocument states;

static void load_states(void)
{
    static std::string text;

    if (states.isNull())
    {
        text = load("ha_states.json");
        TEST_ASSERT_FALSE(deserializeJson(states, text));
    }
}

/* Classify one entity of the fixture, as the connector does for a tile. */
static struct ha_tile_s tile;
static struct ha_entity_s entity;

static void classify(const char *id, const char *card = "tile")
{
    load_states();
    memset(&tile, 0, sizeof(tile));
    tile.kind = HA_TILE_ENTITY;
    strlcpy(tile.entity, id, sizeof(tile.entity));
    strlcpy(tile.card, card, sizeof(tile.card));
    ha_tile_apply_state(&tile, states["event"]["a"][id], &entity);
}

static void test_lights(void)
{
    classify("light.ceiling_lights");
    TEST_ASSERT_EQUAL(type_colorpicker, tile.type);
    TEST_ASSERT_EQUAL_STRING("Ceiling Lights", tile.label);
    TEST_ASSERT_EQUAL_STRING("light", tile.icon_name);
    /* hs_color [28.55, 67.974], brightness 94 of 255. */
    TEST_ASSERT_EQUAL_STRING("29,68,37", tile.state);

    classify("light.bed_light");
    TEST_ASSERT_EQUAL(type_colorpicker, tile.type);
    TEST_ASSERT_EQUAL_STRING("0", strrchr(tile.state, ',') + 1);

    /* A light that dims and has no colour is a slider. */
    JsonDocument dim;

    deserializeJson(dim, "{\"s\":\"on\",\"a\":{\"supported_color_modes\":[\"brightness\"],"
                         "\"brightness\":128,\"friendly_name\":\"Hall\"}}");
    memset(&tile, 0, sizeof(tile));
    strlcpy(tile.entity, "light.hall", sizeof(tile.entity));
    ha_tile_apply_state(&tile, dim.as<JsonVariantConst>(), &entity);
    TEST_ASSERT_EQUAL(type_slider, tile.type);
    TEST_ASSERT_EQUAL_STRING("50", tile.state);
    TEST_ASSERT_EQUAL_STRING("%.0f %%", tile.pattern);

    /* And one that only switches is a switch. */
    deserializeJson(dim, "{\"s\":\"off\",\"a\":{\"supported_color_modes\":[\"onoff\"]}}");
    ha_tile_apply_state(&tile, dim.as<JsonVariantConst>(), &entity);
    TEST_ASSERT_EQUAL(type_switch, tile.type);
    TEST_ASSERT_EQUAL_STRING("OFF", tile.state);
    TEST_ASSERT_EQUAL_STRING("light.hall", tile.label);
}

static void test_covers(void)
{
    classify("cover.living_room_window");
    TEST_ASSERT_EQUAL(type_rollershutter, tile.type);
    /* Home Assistant's 100 is open, openHAB's 0. */
    TEST_ASSERT_EQUAL_STRING("0", tile.state);
    TEST_ASSERT_EQUAL_STRING("rollershutter", tile.icon_name);

    classify("cover.garage_door");
    TEST_ASSERT_EQUAL_STRING("100", tile.state);
    TEST_ASSERT_EQUAL_STRING("garagedoor", tile.icon_name);
}

static void test_climate_and_numbers(void)
{
    classify("climate.heatpump", "thermostat");
    TEST_ASSERT_EQUAL(type_setpoint, tile.type);
    TEST_ASSERT_EQUAL_STRING("21", tile.state);
    TEST_ASSERT_EQUAL_FLOAT(7.0f, tile.min);
    TEST_ASSERT_EQUAL_FLOAT(35.0f, tile.max);
    TEST_ASSERT_EQUAL_FLOAT(0.5f, tile.step);
    TEST_ASSERT_EQUAL_STRING("%.1f °C", tile.pattern);

    classify("number.volume");
    TEST_ASSERT_EQUAL(type_slider, tile.type);
    TEST_ASSERT_EQUAL_STRING("42", tile.state);
    TEST_ASSERT_EQUAL_FLOAT(100.0f, tile.max);

    classify("sensor.outside_temperature");
    TEST_ASSERT_EQUAL(type_number, tile.type);
    TEST_ASSERT_TRUE(tile.read_only);
    TEST_ASSERT_EQUAL_STRING("15.6", tile.state);
    TEST_ASSERT_EQUAL_STRING("%.1f °C", tile.pattern);
    TEST_ASSERT_EQUAL_STRING("temperature", tile.icon_name);

    classify("sensor.outside_humidity");
    TEST_ASSERT_EQUAL_STRING("%.0f %%", tile.pattern);
    TEST_ASSERT_EQUAL_STRING("humidity", tile.icon_name);
}

static void test_switches_and_others(void)
{
    classify("switch.ac");
    TEST_ASSERT_EQUAL(type_switch, tile.type);
    TEST_ASSERT_EQUAL_STRING("OFF", tile.state);

    classify("lock.front_door");
    TEST_ASSERT_EQUAL(type_switch, tile.type);
    TEST_ASSERT_EQUAL_STRING("ON", tile.state);
    TEST_ASSERT_EQUAL_STRING("lock", tile.icon_name);

    /* A button is a moment: the tile never shows it on, so a tap sends ON. */
    classify("button.push");
    TEST_ASSERT_EQUAL(type_switch, tile.type);
    TEST_ASSERT_EQUAL_STRING("OFF", tile.state);

    classify("binary_sensor.movement_backyard", "glance");
    TEST_ASSERT_EQUAL(type_string, tile.type);
    TEST_ASSERT_TRUE(tile.read_only);
    TEST_ASSERT_EQUAL_STRING("Detected", tile.state);
    TEST_ASSERT_EQUAL_STRING("motion", tile.icon_name);

    classify("select.speed");
    TEST_ASSERT_EQUAL(type_selection, tile.type);
    TEST_ASSERT_EQUAL(3, tile.option_count);
    TEST_ASSERT_EQUAL_STRING("light_speed", tile.state);
    TEST_ASSERT_EQUAL_STRING("ludicrous_speed", tile.options[2]);

    classify("media_player.living_room", "media-control");
    TEST_ASSERT_EQUAL(type_player, tile.type);
    TEST_ASSERT_EQUAL_STRING("PLAY", tile.state);

    /* An entity the dashboard names and the server does not have. */
    classify("light.not_there");
    TEST_ASSERT_EQUAL(type_string, tile.type);
    TEST_ASSERT_TRUE(tile.read_only);
    TEST_ASSERT_EQUAL_STRING("unavailable", tile.state);
    TEST_ASSERT_EQUAL_STRING("light.not_there", tile.label);
}

static void test_changes(void)
{
    char         panel[STR_STATE_TEXT_LEN];
    JsonDocument change;

    /* The light goes off: the state changes, brightness is listed as gone. */
    classify("light.ceiling_lights");
    deserializeJson(change, "{\"+\":{\"s\":\"off\"},\"-\":{\"a\":[\"brightness\"]}}");
    ha_entity_update(&entity, change["+"], change["-"]);
    ha_entity_panel_state(&entity, panel, sizeof(panel));
    TEST_ASSERT_EQUAL_STRING("29,68,0", panel);
    TEST_ASSERT_TRUE(entity.brightness < 0.0f);

    /* A moving cover reports its position only, as the demo cover does. */
    classify("cover.living_room_window");
    deserializeJson(change, "{\"+\":{\"lu\":1.0,\"a\":{\"current_position\":40}}}");
    ha_entity_update(&entity, change["+"], change["-"]);
    ha_entity_panel_state(&entity, panel, sizeof(panel));
    TEST_ASSERT_EQUAL_STRING("60", panel);

    /* A set point that loses its value. */
    classify("climate.heatpump");
    deserializeJson(change, "{\"+\":{\"a\":{\"temperature\":null}}}");
    ha_entity_update(&entity, change["+"], change["-"]);
    ha_entity_panel_state(&entity, panel, sizeof(panel));
    TEST_ASSERT_EQUAL_STRING("NULL", panel);
}

/* ----------------------------------------------------------------- commands */

static JsonDocument command(const char *id, const char *text)
{
    static char  buf[384];
    JsonDocument doc;

    memset(&entity, 0, sizeof(entity));
    ha_entity_reset(&entity, id);

    size_t len = ha_command_message(&entity, text, 7, buf, sizeof(buf));

    if (len > 0)
        deserializeJson(doc, buf, len);

    return doc;
}

static void test_commands(void)
{
    JsonDocument m = command("light.hall", "50");

    TEST_ASSERT_EQUAL(7, m["id"].as<int>());
    TEST_ASSERT_EQUAL_STRING("call_service", m["type"]);
    TEST_ASSERT_EQUAL_STRING("light", m["domain"]);
    TEST_ASSERT_EQUAL_STRING("turn_on", m["service"]);
    TEST_ASSERT_EQUAL_STRING("light.hall", m["target"]["entity_id"]);
    TEST_ASSERT_EQUAL(50, m["service_data"]["brightness_pct"].as<int>());

    TEST_ASSERT_EQUAL_STRING("turn_off", command("light.hall", "0")["service"]);
    TEST_ASSERT_EQUAL_STRING("turn_off", command("light.hall", "OFF")["service"]);

    m = command("light.hall", "120,80,60");
    TEST_ASSERT_EQUAL_STRING("turn_on", m["service"]);
    TEST_ASSERT_EQUAL(120, m["service_data"]["hs_color"][0].as<int>());
    TEST_ASSERT_EQUAL(80, m["service_data"]["hs_color"][1].as<int>());
    TEST_ASSERT_EQUAL(60, m["service_data"]["brightness_pct"].as<int>());

    TEST_ASSERT_EQUAL_STRING("open_cover", command("cover.x", "UP")["service"]);
    TEST_ASSERT_EQUAL_STRING("close_cover", command("cover.x", "DOWN")["service"]);
    TEST_ASSERT_EQUAL_STRING("stop_cover", command("cover.x", "STOP")["service"]);

    m = command("cover.x", "30");
    TEST_ASSERT_EQUAL_STRING("set_cover_position", m["service"]);
    TEST_ASSERT_EQUAL(70, m["service_data"]["position"].as<int>());

    m = command("climate.x", "21.5");
    TEST_ASSERT_EQUAL_STRING("set_temperature", m["service"]);
    TEST_ASSERT_EQUAL_FLOAT(21.5f, m["service_data"]["temperature"].as<float>());

    m = command("select.speed", "ludicrous_speed");
    TEST_ASSERT_EQUAL_STRING("select_option", m["service"]);
    TEST_ASSERT_EQUAL_STRING("ludicrous_speed", m["service_data"]["option"]);

    TEST_ASSERT_EQUAL_STRING("media_next_track", command("media_player.x", "NEXT")["service"]);
    TEST_ASSERT_EQUAL_STRING("media_pause", command("media_player.x", "PAUSE")["service"]);
    TEST_ASSERT_EQUAL_STRING("unlock", command("lock.x", "OFF")["service"]);
    TEST_ASSERT_EQUAL_STRING("press", command("button.x", "ON")["service"]);
    TEST_ASSERT_EQUAL_STRING("set_value", command("number.x", "12")["service"]);

    /* Nothing to send: a button cannot be pressed off, a cover has no PLAY. */
    TEST_ASSERT_TRUE(command("button.x", "OFF").isNull());
    TEST_ASSERT_TRUE(command("cover.x", "PLAY").isNull());
}

/* The Home Assistant connector names icons of the panel's own set in the
 * form icon_set_get_by_url() reads; see Item::iconUrl() in connector_ha.cpp. */
static void test_icon_reference_form(void)
{
    char name[32];
    char state[32];

    TEST_ASSERT_TRUE(icon_url_split("ha:/icon/light?state=40&format=png", name, sizeof(name),
                                    state, sizeof(state)));
    TEST_ASSERT_EQUAL_STRING("light", name);
    TEST_ASSERT_EQUAL_STRING("40", state);

    TEST_ASSERT_TRUE(icon_url_split("ha:/icon/temperature?format=png", name, sizeof(name),
                                    state, sizeof(state)));
    TEST_ASSERT_EQUAL_STRING("temperature", name);
    TEST_ASSERT_EQUAL_STRING("", state);
}

void test_ha_model_run(void)
{
    RUN_TEST(test_ws_header_lengths);
    RUN_TEST(test_ws_decode_server_frames);
    RUN_TEST(test_ref_and_view_match);
    RUN_TEST(test_view_summaries);
    RUN_TEST(test_root_view_tiles_and_clock);
    RUN_TEST(test_sections_subview);
    RUN_TEST(test_glance_and_stack_flattened);
    RUN_TEST(test_root_links_to_other_views);
    RUN_TEST(test_lights);
    RUN_TEST(test_covers);
    RUN_TEST(test_climate_and_numbers);
    RUN_TEST(test_switches_and_others);
    RUN_TEST(test_changes);
    RUN_TEST(test_commands);
    RUN_TEST(test_icon_reference_form);
}
