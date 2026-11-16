/**
 * @file ha_model.cpp
 *
 * See ha_model.hpp. ArduinoJson and libc only.
 */
#include "ha_model.hpp"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ helpers */

static const char *str_or(JsonVariantConst v, const char *fallback)
{
    const char *s = v.as<const char *>();

    return (s != NULL) ? s : fallback;
}

/* Whether entity `id` is in `domain`: "light.x" is in "light". */
static bool in_domain(const char *id, const char *domain)
{
    size_t n = strlen(domain);

    return strncmp(id, domain, n) == 0 && id[n] == '.';
}

static bool in_any_domain(const char *id, const char *const *domains)
{
    for (; *domains != NULL; domains++)
        if (in_domain(id, *domains))
            return true;

    return false;
}

/* Whether object `obj` has `key`, null value or not: a change names an
 * attribute with null when it lost its value. */
static bool has_key(JsonVariantConst obj, const char *key)
{
    for (JsonPairConst kv : obj.as<JsonObjectConst>())
        if (strcmp(kv.key().c_str(), key) == 0)
            return true;

    return false;
}

static bool array_has(JsonVariantConst array, const char *value)
{
    for (JsonVariantConst v : array.as<JsonArrayConst>())
    {
        const char *s = v.as<const char *>();

        if (s != NULL && strcmp(s, value) == 0)
            return true;
    }

    return false;
}

/* --------------------------------------------------------------- the views */

static void card_filter(JsonObject f, int depth)
{
    f["type"] = true;
    f["entity"] = true;
    f["name"] = true;
    f["icon"] = true;
    f["title"] = true;
    f["entities"] = true;
    f["tap_action"]["action"] = true;
    f["tap_action"]["navigation_path"] = true;

    if (depth > 0)
        card_filter(f["cards"].add<JsonObject>(), depth - 1);
}

void ha_view_filter(JsonDocument &filter)
{
    filter.clear();

    JsonObject view = filter.to<JsonObject>();

    view["title"] = true;
    view["path"] = true;
    view["subview"] = true;
    card_filter(view["cards"].add<JsonObject>(), 2);
    card_filter(view["sections"].add<JsonObject>()["cards"].add<JsonObject>(), 2);
}

void ha_view_summary_filter(JsonDocument &filter)
{
    filter.clear();

    JsonObject view = filter.to<JsonObject>();

    view["title"] = true;
    view["path"] = true;
    view["subview"] = true;
}

void ha_ref_view(const char *ref, char *view, size_t view_size)
{
    const char *p = (strncmp(ref, "ha:", 3) == 0) ? ref + 3 : ref;
    const char *slash = strchr(p, '/');

    strlcpy(view, (slash != NULL) ? slash + 1 : "", view_size);
}

bool ha_view_matches(const char *want, size_t index, const char *path)
{
    if (want[0] == '\0')
        return index == 0;

    if (want[0] == '#')
        return (size_t)atoi(want + 1) == index;

    return path != NULL && strcmp(path, want) == 0;
}

void ha_view_summary(JsonVariantConst view, size_t index, struct ha_view_s *out)
{
    const char *path = view["path"].as<const char *>();

    strlcpy(out->title, str_or(view["title"], ""), sizeof(out->title));

    if (path != NULL && path[0] != '\0')
        strlcpy(out->target, path, sizeof(out->target));
    else
        snprintf(out->target, sizeof(out->target), "#%u", (unsigned)index);

    if (out->title[0] == '\0')
        strlcpy(out->title, out->target, sizeof(out->title));

    out->subview = view["subview"].as<bool>();
}

/* The view a navigation_path leads to: its last segment, as a path or, when
 * it is a number, as an index. "/ohez-panel/media" is "media". */
static void nav_target(const char *path, char *out, size_t out_size)
{
    const char *last = strrchr(path, '/');
    const char *seg  = (last != NULL) ? last + 1 : path;
    bool        digits = (seg[0] != '\0');

    for (const char *c = seg; *c != '\0'; c++)
        if (*c < '0' || *c > '9')
            digits = false;

    if (digits)
        snprintf(out, out_size, "#%s", seg);
    else
        strlcpy(out, seg, out_size);
}

struct view_builder_s
{
    struct ha_page_s *page;
    bool              root;
    const char       *clock_frame;
};

static struct ha_tile_s *add_tile(struct view_builder_s *b, uint8_t kind)
{
    if (b->page->tile_count >= ITEM_COUNT_MAX)
        return NULL;

    struct ha_tile_s *t = &b->page->tiles[b->page->tile_count++];

    memset(t, 0, sizeof(*t));
    t->kind = kind;

    return t;
}

static void add_entity(struct view_builder_s *b, const char *entity, const char *name,
                       const char *icon, const char *card)
{
    if (entity == NULL || entity[0] == '\0')
        return;

    struct ha_tile_s *t = add_tile(b, HA_TILE_ENTITY);

    if (t == NULL)
        return;

    strlcpy(t->entity, entity, sizeof(t->entity));
    strlcpy(t->name, name ? name : "", sizeof(t->name));
    strlcpy(t->icon, icon ? icon : "", sizeof(t->icon));
    strlcpy(t->card, card ? card : "", sizeof(t->card));
}

/* One row of an entities or glance card: a string, or an object with an
 * entity. Rows without one (sections, dividers, buttons) are not tiles. */
static void add_row(struct view_builder_s *b, JsonVariantConst row, const char *card)
{
    if (row.is<const char *>())
    {
        add_entity(b, row.as<const char *>(), NULL, NULL, card);
        return;
    }

    add_entity(b, row["entity"].as<const char *>(), row["name"].as<const char *>(),
               row["icon"].as<const char *>(), card);
}

static void add_clock_rows(struct view_builder_s *b, JsonVariantConst rows)
{
    for (JsonVariantConst row : rows.as<JsonArrayConst>())
    {
        if (b->page->clock_count >= CLOCK_ITEM_COUNT)
            return;

        const char *entity = row.is<const char *>() ? row.as<const char *>()
                                                    : row["entity"].as<const char *>();

        if (entity == NULL || entity[0] == '\0')
            continue;

        struct ha_tile_s *t = &b->page->clock[b->page->clock_count++];

        memset(t, 0, sizeof(*t));
        t->kind = HA_TILE_ENTITY;
        strlcpy(t->entity, entity, sizeof(t->entity));

        if (row.is<JsonObjectConst>())
            strlcpy(t->name, str_or(row["name"], ""), sizeof(t->name));
    }
}

static void add_card(struct view_builder_s *b, JsonVariantConst card)
{
    const char *type = str_or(card["type"], "");

    if (   strcmp(type, "vertical-stack") == 0 || strcmp(type, "horizontal-stack") == 0
        || strcmp(type, "grid") == 0)
    {
        for (JsonVariantConst c : card["cards"].as<JsonArrayConst>())
            add_card(b, c);
        return;
    }

    if (strcmp(type, "entities") == 0 || strcmp(type, "glance") == 0)
    {
        const char *title = str_or(card["title"], "");

        if (b->root && b->clock_frame[0] != '\0' && strcmp(title, b->clock_frame) == 0)
        {
            add_clock_rows(b, card["entities"]);
            return;
        }

        for (JsonVariantConst row : card["entities"].as<JsonArrayConst>())
            add_row(b, row, type);
        return;
    }

    const char *entity = card["entity"].as<const char *>();

    if (entity != NULL)
    {
        add_entity(b, entity, card["name"].as<const char *>(), card["icon"].as<const char *>(),
                   type);
        return;
    }

    const char *nav = card["tap_action"]["navigation_path"].as<const char *>();

    if (nav != NULL && strcmp(str_or(card["tap_action"]["action"], ""), "navigate") == 0)
    {
        struct ha_tile_s *t = add_tile(b, HA_TILE_LINK);

        if (t == NULL)
            return;

        nav_target(nav, t->view, sizeof(t->view));
        strlcpy(t->name, str_or(card["name"], str_or(card["title"], t->view)), sizeof(t->name));
        strlcpy(t->icon, str_or(card["icon"], ""), sizeof(t->icon));
    }
}

static bool links_to(const struct ha_page_s *page, const char *target)
{
    for (size_t i = 0; i < page->tile_count; i++)
        if (page->tiles[i].kind == HA_TILE_LINK && strcmp(page->tiles[i].view, target) == 0)
            return true;

    return false;
}

void ha_page_add_view_links(struct ha_page_s *page, const struct ha_view_s *views,
                            size_t view_count)
{
    struct view_builder_s b = {page, true, ""};

    /* In the room the cards left. A subview is reached from a button only, as
     * in Home Assistant, and a view a card already links to is not linked
     * twice. */
    for (size_t i = 1; i < view_count; i++)
    {
        if (views[i].subview || links_to(page, views[i].target))
            continue;

        struct ha_tile_s *t = add_tile(&b, HA_TILE_LINK);

        if (t == NULL)
            break;

        strlcpy(t->view, views[i].target, sizeof(t->view));
        strlcpy(t->name, views[i].title, sizeof(t->name));
    }
}

void ha_view_build(JsonVariantConst view, bool root, const char *clock_frame,
                   const struct ha_view_s *views, size_t view_count,
                   struct ha_page_s *page)
{
    struct view_builder_s b = {page, root, clock_frame ? clock_frame : ""};

    page->root = root;
    page->tile_count = 0;
    page->clock_count = 0;
    strlcpy(page->title, str_or(view["title"], ""), sizeof(page->title));

    /* Below the root, the first tile is the way back, as openHAB's parent
     * link is. */
    if (root == false)
    {
        struct ha_tile_s *t = add_tile(&b, HA_TILE_BACK);

        if (t != NULL)
            strlcpy(t->name, (view_count > 0) ? views[0].title : "", sizeof(t->name));
    }

    for (JsonVariantConst card : view["cards"].as<JsonArrayConst>())
        add_card(&b, card);

    for (JsonVariantConst section : view["sections"].as<JsonArrayConst>())
        for (JsonVariantConst card : section["cards"].as<JsonArrayConst>())
            add_card(&b, card);

    if (root)
        ha_page_add_view_links(page, views, view_count);
}

/* ---------------------------------------------------------------- entities */

/* Domains whose state is a moment rather than a condition: a tap runs them,
 * and the tile never shows them as on. */
static const char *const stateless_domains[] = {"scene", "script", "button", "input_button",
                                                NULL};

static const char *const toggle_domains[] = {"switch", "input_boolean", "fan", "siren",
                                             "humidifier", "automation", "remote", "lock",
                                             "valve", NULL};

void ha_entity_reset(struct ha_entity_s *e, const char *id)
{
    memset(e, 0, sizeof(*e));
    strlcpy(e->id, id, sizeof(e->id));
    e->brightness = -1.0f;
    e->position = -1.0f;
    e->temperature = NAN;
    e->hue = -1.0f;
    e->saturation = -1.0f;
}

void ha_entity_update(struct ha_entity_s *e, JsonVariantConst state, JsonVariantConst removed)
{
    const char *s = state["s"].as<const char *>();

    if (s != NULL)
    {
        strlcpy(e->state, s, sizeof(e->state));
        e->value = strtof(s, NULL);
    }

    JsonVariantConst a = state["a"];

    if (has_key(a, "brightness"))
        e->brightness = a["brightness"].isNull() ? -1.0f : a["brightness"].as<float>();

    if (has_key(a, "current_position"))
        e->position = a["current_position"].isNull() ? -1.0f : a["current_position"].as<float>();

    if (has_key(a, "temperature"))
        e->temperature = a["temperature"].isNull() ? NAN : a["temperature"].as<float>();

    if (has_key(a, "hs_color"))
    {
        JsonVariantConst hs = a["hs_color"];

        e->hue        = hs.isNull() ? -1.0f : hs[0].as<float>();
        e->saturation = hs.isNull() ? -1.0f : hs[1].as<float>();
    }

    for (JsonVariantConst name : removed["a"].as<JsonArrayConst>())
    {
        const char *n = name.as<const char *>();

        if (n == NULL)
            continue;
        if (strcmp(n, "brightness") == 0)
            e->brightness = -1.0f;
        else if (strcmp(n, "current_position") == 0)
            e->position = -1.0f;
        else if (strcmp(n, "temperature") == 0)
            e->temperature = NAN;
        else if (strcmp(n, "hs_color") == 0)
            e->hue = e->saturation = -1.0f;
    }
}

/* How many decimals a number's text has, at most two. "15.6" has one. */
static int decimals_of(const char *text)
{
    const char *dot = strchr(text, '.');

    if (dot == NULL)
        return 0;

    int n = (int)strlen(dot + 1);

    return (n > 2) ? 2 : n;
}

/* As many decimals as `step` needs, at most two. */
static int decimals_of_step(float step)
{
    int d = 0;

    for (float s = fabsf(step); d < 2 && fabsf(s - roundf(s)) > 0.001f; d++)
        s *= 10.0f;

    return d;
}

/* An openHAB number pattern: "%.1f °C". A '%' in the unit is written "%%",
 * as openHAB writes it. */
static void set_pattern(struct ha_tile_s *t, int decimals, const char *unit)
{
    char escaped[STR_PATTERN_LEN];
    size_t n = 0;

    for (const char *c = (unit != NULL) ? unit : ""; *c != '\0' && n + 2 < sizeof(escaped); c++)
    {
        if (*c == '%')
            escaped[n++] = '%';

        escaped[n++] = *c;
    }

    escaped[n] = '\0';

    /* Wider than the field, then cut to it: a long unit loses its tail. */
    char pattern[STR_PATTERN_LEN * 2];

    if (n > 0)
        snprintf(pattern, sizeof(pattern), "%%.%df %s", decimals, escaped);
    else
        snprintf(pattern, sizeof(pattern), "%%.%df", decimals);

    strlcpy(t->pattern, pattern, sizeof(t->pattern));
}

/* A name of the panel's icon set (tools/icon_set.list) for a card's
 * "mdi:..." icon. Only the common ones: a miss falls back to the entity's
 * domain. */
static const char *icon_from_mdi(const char *mdi)
{
    static const struct
    {
        const char *mdi;
        const char *icon;
    } table[] = {
        {"mdi:lightbulb", "light"},          {"mdi:lamp", "light"},
        {"mdi:ceiling-light", "light"},      {"mdi:television", "screen"},
        {"mdi:sofa", "sofa"},                {"mdi:bed", "bedroom"},
        {"mdi:home", "house"},               {"mdi:thermometer", "temperature"},
        {"mdi:fan", "fan"},                  {"mdi:garage", "garage"},
        {"mdi:music", "mediacontrol"},       {"mdi:speaker", "soundvolume"},
        {"mdi:water", "water"},              {"mdi:fire", "fire"},
        {"mdi:door", "door"},                {"mdi:window-closed", "window"},
        {"mdi:lock", "lock"},                {"mdi:flash", "energy"},
        {"mdi:solar-power", "solarplant"},   {"mdi:washing-machine", "washingmachine"},
        {"mdi:silverware-fork-knife", "kitchen"}, {"mdi:shower", "bath"},
        {"mdi:toilet", "toilet"},            {"mdi:desk", "office"},
        {"mdi:flower", "garden"},            {"mdi:weather-sunny", "sun"},
        {"mdi:cog", "settings"},             {"mdi:camera", "camera"},
        {"mdi:radiator", "radiator"},        {"mdi:blinds", "blinds"},
        {"mdi:window-shutter", "rollershutter"},
    };

    if (mdi == NULL || mdi[0] == '\0')
        return NULL;

    for (size_t i = 0; i < sizeof(table) / sizeof(table[0]); i++)
        if (strcmp(table[i].mdi, mdi) == 0)
            return table[i].icon;

    return NULL;
}

static const char *icon_from_entity(const char *id, const char *device_class)
{
    const char *dc = (device_class != NULL) ? device_class : "";

    static const struct
    {
        const char *domain;
        const char *icon;
    } domains[] = {
        {"light", "light"},           {"switch", "switch"},       {"input_boolean", "switch"},
        {"fan", "fan"},               {"climate", "heating"},     {"water_heater", "water"},
        {"media_player", "player"},   {"lock", "lock"},           {"scene", "settings"},
        {"script", "settings"},       {"button", "switch"},       {"select", "settings"},
        {"input_select", "settings"}, {"number", "slider"},       {"input_number", "slider"},
        {"person", "presence"},       {"device_tracker", "presence"},
        {"weather", "sun_clouds"},    {"camera", "camera"},       {"siren", "siren"},
        {"alarm_control_panel", "shield"}, {"valve", "water"},    {"sun", "sun"},
    };

    if (in_domain(id, "cover"))
    {
        if (strcmp(dc, "garage") == 0)
            return "garagedoor";
        if (strcmp(dc, "blind") == 0 || strcmp(dc, "shade") == 0 || strcmp(dc, "curtain") == 0)
            return "blinds";
        return "rollershutter";
    }

    if (in_domain(id, "sensor"))
    {
        static const char *const pairs[][2] = {
            {"temperature", "temperature"}, {"humidity", "humidity"},
            {"battery", "battery"},         {"power", "energy"},
            {"energy", "energy"},           {"pressure", "pressure"},
            {"illuminance", "sun"},         {"carbon_dioxide", "carbondioxide"},
            {"gas", "gas"},                 {"water", "water"},
            {"monetary", "price"},          {"timestamp", "time"},
        };

        for (size_t i = 0; i < sizeof(pairs) / sizeof(pairs[0]); i++)
            if (strcmp(dc, pairs[i][0]) == 0)
                return pairs[i][1];

        return "text";
    }

    if (in_domain(id, "binary_sensor"))
    {
        if (strcmp(dc, "door") == 0 || strcmp(dc, "garage_door") == 0)
            return "door";
        if (strcmp(dc, "window") == 0)
            return "window";
        if (strcmp(dc, "motion") == 0 || strcmp(dc, "occupancy") == 0 || strcmp(dc, "presence") == 0)
            return "motion";
        if (strcmp(dc, "smoke") == 0)
            return "smoke";
        if (strcmp(dc, "moisture") == 0)
            return "water";
        if (strcmp(dc, "battery") == 0)
            return "lowbattery";
        return "contact";
    }

    for (size_t i = 0; i < sizeof(domains) / sizeof(domains[0]); i++)
        if (in_domain(id, domains[i].domain))
            return domains[i].icon;

    return "none";
}

void ha_tile_apply_state(struct ha_tile_s *t, JsonVariantConst st, struct ha_entity_s *e)
{
    ha_entity_reset(e, t->entity);

    t->read_only = false;
    t->min = 0.0f;
    t->max = 100.0f;
    t->step = 1.0f;
    t->option_count = 0;
    t->pattern[0] = '\0';

    JsonVariantConst a = st["a"];
    const char *dc = a["device_class"].as<const char *>();

    strlcpy(t->label, t->name[0] ? t->name : str_or(a["friendly_name"], t->entity),
            sizeof(t->label));

    const char *mdi = icon_from_mdi(t->icon);

    strlcpy(t->icon_name, mdi ? mdi : icon_from_entity(t->entity, dc), sizeof(t->icon_name));

    if (st.isNull())
    {
        /* An entity the dashboard names and the server does not have. */
        t->type = type_string;
        t->read_only = true;
        e->type = t->type;
        strlcpy(e->state, "unavailable", sizeof(e->state));
        ha_entity_panel_state(e, t->state, sizeof(t->state));
        return;
    }

    ha_entity_update(e, st, JsonVariantConst());

    const char *id = t->entity;

    if (in_domain(id, "light"))
    {
        JsonVariantConst modes = a["supported_color_modes"];
        bool color = array_has(modes, "hs") || array_has(modes, "xy") || array_has(modes, "rgb")
                  || array_has(modes, "rgbw") || array_has(modes, "rgbww");
        bool dim = color || array_has(modes, "brightness") || array_has(modes, "color_temp")
                || array_has(modes, "white");

        t->type = color ? type_colorpicker : (dim ? type_slider : type_switch);

        if (dim && !color)
            set_pattern(t, 0, "%");
    }
    else if (in_any_domain(id, toggle_domains) || in_any_domain(id, stateless_domains))
    {
        t->type = type_switch;
    }
    else if (in_domain(id, "cover"))
    {
        t->type = type_rollershutter;
        set_pattern(t, 0, "%");
    }
    else if (in_domain(id, "climate") || in_domain(id, "water_heater"))
    {
        t->type = type_setpoint;
        t->min  = a["min_temp"] | 7.0f;
        t->max  = a["max_temp"] | 35.0f;
        t->step = a["target_temp_step"] | 0.5f;
        set_pattern(t, decimals_of_step(t->step), str_or(a["temperature_unit"], "°C"));
    }
    else if (in_domain(id, "number") || in_domain(id, "input_number"))
    {
        t->type = (strcmp(str_or(a["mode"], ""), "box") == 0) ? type_setpoint : type_slider;
        t->min  = a["min"] | 0.0f;
        t->max  = a["max"] | 100.0f;
        t->step = a["step"] | 1.0f;
        set_pattern(t, decimals_of_step(t->step), a["unit_of_measurement"].as<const char *>());
    }
    else if (in_domain(id, "select") || in_domain(id, "input_select"))
    {
        t->type = type_selection;

        for (JsonVariantConst o : a["options"].as<JsonArrayConst>())
        {
            if (t->option_count >= ITEM_SELECTION_COUNT_MAX)
                break;

            strlcpy(t->options[t->option_count++], str_or(o, ""), sizeof(t->options[0]));
        }
    }
    else if (in_domain(id, "media_player"))
    {
        t->type = type_player;
    }
    else if (in_domain(id, "sensor") && e->state[0] != '\0')
    {
        char *end;

        strtod(e->state, &end);

        if (end != e->state && *end == '\0')
        {
            t->type = type_number;
            set_pattern(t, decimals_of(e->state), a["unit_of_measurement"].as<const char *>());
        }
        else
        {
            t->type = type_string;
        }

        t->read_only = true;
    }
    else
    {
        if (in_domain(id, "binary_sensor"))
        {
            const char *c = (dc != NULL) ? dc : "";

            if (   strcmp(c, "door") == 0 || strcmp(c, "window") == 0 || strcmp(c, "opening") == 0
                || strcmp(c, "garage_door") == 0)
                e->binary_words = 1;
            else if (strcmp(c, "motion") == 0 || strcmp(c, "occupancy") == 0
                     || strcmp(c, "presence") == 0)
                e->binary_words = 2;
        }

        t->type = type_string;
        t->read_only = true;
    }

    e->type = t->type;
    ha_entity_panel_state(e, t->state, sizeof(t->state));
}

static int percent_of_brightness(float brightness)
{
    int p = (int)lroundf(brightness * 100.0f / 255.0f);

    return (p < 1) ? 1 : p;
}

void ha_entity_panel_state(const struct ha_entity_s *e, char *out, size_t out_size)
{
    const char *s = e->state;
    bool        on = (strcmp(s, "on") == 0);

    switch (e->type)
    {
    case type_switch:
        if (in_any_domain(e->id, stateless_domains))
            strlcpy(out, "OFF", out_size);
        else if (in_domain(e->id, "lock"))
            strlcpy(out, (strcmp(s, "locked") == 0) ? "ON" : "OFF", out_size);
        else if (in_domain(e->id, "valve"))
            strlcpy(out, (strcmp(s, "open") == 0) ? "ON" : "OFF", out_size);
        else
            strlcpy(out, on ? "ON" : "OFF", out_size);
        break;

    case type_slider:
        if (in_domain(e->id, "light"))
            snprintf(out, out_size, "%d",
                     on ? ((e->brightness >= 0.0f) ? percent_of_brightness(e->brightness) : 100) : 0);
        else
            snprintf(out, out_size, "%g", (double)e->value);
        break;

    case type_colorpicker:
        snprintf(out, out_size, "%d,%d,%d", (int)lroundf((e->hue < 0.0f) ? 0.0f : e->hue),
                 (int)lroundf((e->saturation < 0.0f) ? 0.0f : e->saturation),
                 on ? ((e->brightness >= 0.0f) ? percent_of_brightness(e->brightness) : 100) : 0);
        break;

    case type_rollershutter:
        /* openHAB's direction: 0 is open, 100 is closed. */
        if (e->position >= 0.0f)
            snprintf(out, out_size, "%d", (int)lroundf(100.0f - e->position));
        else
            strlcpy(out, (strcmp(s, "closed") == 0) ? "100" : "0", out_size);
        break;

    case type_setpoint:
        if (in_domain(e->id, "climate") || in_domain(e->id, "water_heater"))
        {
            if (isnan(e->temperature))
                strlcpy(out, "NULL", out_size);
            else
                snprintf(out, out_size, "%g", (double)e->temperature);
        }
        else
        {
            snprintf(out, out_size, "%g", (double)e->value);
        }
        break;

    case type_player:
        strlcpy(out, (strcmp(s, "playing") == 0) ? "PLAY" : "PAUSE", out_size);
        break;

    case type_string:
        if (in_domain(e->id, "binary_sensor") && (on || strcmp(s, "off") == 0))
        {
            static const char *const words[3][2] = {
                {"On", "Off"}, {"Open", "Closed"}, {"Detected", "Clear"}};

            strlcpy(out, words[e->binary_words % 3][on ? 0 : 1], out_size);
        }
        else
        {
            strlcpy(out, s, out_size);
        }
        break;

    default:
        strlcpy(out, s, out_size);
        break;
    }
}

/* ---------------------------------------------------------------- commands */

static bool is_number(const char *text, float *value)
{
    char *end;

    *value = strtof(text, &end);

    return end != text && *end == '\0';
}

size_t ha_command_message(const struct ha_entity_s *e, const char *command, uint32_t id,
                          char *out, size_t out_size)
{
    JsonDocument doc;
    char         domain[24];
    const char  *service = NULL;
    const char  *dot = strchr(e->id, '.');
    float        number;

    if (dot == NULL || (size_t)(dot - e->id) >= sizeof(domain))
        return 0;

    memcpy(domain, e->id, (size_t)(dot - e->id));
    domain[dot - e->id] = '\0';

    JsonObject data = doc["service_data"].to<JsonObject>();
    bool       on  = strcmp(command, "ON") == 0;
    bool       off = strcmp(command, "OFF") == 0;
    unsigned   h, s, v;

    if (on || off)
    {
        if (strcmp(domain, "lock") == 0)
            service = on ? "lock" : "unlock";
        else if (strcmp(domain, "valve") == 0)
            service = on ? "open_valve" : "close_valve";
        else if (strcmp(domain, "button") == 0 || strcmp(domain, "input_button") == 0)
            service = on ? "press" : NULL;
        else if (strcmp(domain, "scene") == 0 || strcmp(domain, "script") == 0)
            service = on ? "turn_on" : NULL;
        else if (strcmp(domain, "cover") == 0)
            service = on ? "open_cover" : "close_cover";
        else
            service = on ? "turn_on" : "turn_off";
    }
    else if (strcmp(domain, "cover") == 0 && strcmp(command, "UP") == 0)
        service = "open_cover";
    else if (strcmp(domain, "cover") == 0 && strcmp(command, "DOWN") == 0)
        service = "close_cover";
    else if (strcmp(domain, "cover") == 0 && strcmp(command, "STOP") == 0)
        service = "stop_cover";
    else if (strcmp(domain, "media_player") == 0 && strcmp(command, "PLAY") == 0)
        service = "media_play";
    else if (strcmp(domain, "media_player") == 0 && strcmp(command, "PAUSE") == 0)
        service = "media_pause";
    else if (strcmp(domain, "media_player") == 0 && strcmp(command, "NEXT") == 0)
        service = "media_next_track";
    else if (strcmp(domain, "media_player") == 0 && strcmp(command, "PREVIOUS") == 0)
        service = "media_previous_track";
    else if (strcmp(domain, "light") == 0 && sscanf(command, "%u,%u,%u", &h, &s, &v) == 3)
    {
        if (v == 0)
        {
            service = "turn_off";
        }
        else
        {
            service = "turn_on";
            JsonArray hs = data["hs_color"].to<JsonArray>();
            hs.add(h);
            hs.add(s);
            data["brightness_pct"] = v;
        }
    }
    else if (is_number(command, &number))
    {
        if (strcmp(domain, "light") == 0)
        {
            service = (number > 0.0f) ? "turn_on" : "turn_off";

            if (number > 0.0f)
                data["brightness_pct"] = (int)lroundf(number);
        }
        else if (strcmp(domain, "cover") == 0)
        {
            service = "set_cover_position";
            data["position"] = (int)lroundf(100.0f - number);
        }
        else if (strcmp(domain, "climate") == 0 || strcmp(domain, "water_heater") == 0)
        {
            service = "set_temperature";
            data["temperature"] = number;
        }
        else if (strcmp(domain, "number") == 0 || strcmp(domain, "input_number") == 0)
        {
            service = "set_value";
            data["value"] = number;
        }
        else if (strcmp(domain, "fan") == 0)
        {
            service = "set_percentage";
            data["percentage"] = (int)lroundf(number);
        }
        else if (strcmp(domain, "media_player") == 0)
        {
            service = "volume_set";
            data["volume_level"] = number / 100.0f;
        }
    }

    if (service == NULL && (strcmp(domain, "select") == 0 || strcmp(domain, "input_select") == 0))
    {
        service = "select_option";
        data["option"] = command;
    }

    if (service == NULL)
        return 0;

    doc["id"] = id;
    doc["type"] = "call_service";
    doc["domain"] = domain;
    doc["service"] = service;
    doc["target"]["entity_id"] = e->id;

    size_t len = measureJson(doc);

    if (len + 1 > out_size)
        return 0;

    serializeJson(doc, out, out_size);

    return len;
}
