/**
 * @file webui_api_json.cpp
 *
 * See webui_api_json.hpp for why this is a file of its own. Nothing in here
 * touches the transport, LVGL or the network, which is what lets the host
 * tests link it.
 */

#include "webui_api_json.hpp"

#include <ArduinoJson.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

/* Serialize, or measure when there is nowhere to write: the two passes the
 * header promises. serializeJson() truncates rather than overflowing, so a
 * caller that measured first always gets a complete document. */
static size_t emit(const JsonDocument &doc, char *out, size_t out_size)
{
    if (out == NULL || out_size == 0)
        return measureJson(doc);

    return serializeJson(doc, out, out_size);
}

/* ------------------------------------------------------------------ status */

size_t webui_api_status_json(const struct webui_api_status_s *st, char *out,
                             size_t out_size)
{
    JsonDocument doc;

    doc["version"] = st->version;
    doc["target"] = st->target;
    doc["build"] = st->build;
    doc["uptime_s"] = st->uptime_s;
    doc["hostname"] = st->hostname;
    doc["ssid"] = st->ssid;
    doc["wired"] = st->wired;

    if (st->wired == false)
        doc["rssi"] = st->rssi;

    doc["ip"] = st->ip;
    doc["mac"] = st->mac;
    doc["free_heap"] = st->free_heap;

    return emit(doc, out, out_size);
}

/* ------------------------------------------------------------------ config */

static const char *field_kind_name(const struct config_field_s *f)
{
    switch (f->kind)
    {
    case SETTINGS_TEXT:
        return "text";
    case SETTINGS_BOOL:
        return "bool";
    case SETTINGS_ENUM:
        return "enum";
    default:
        return "int";
    }
}

size_t webui_api_config_json(const config_item_t *item, char *out, size_t out_size)
{
    JsonDocument doc;

    JsonArray fields = doc["fields"].to<JsonArray>();

    for (size_t i = 0; i < config_field_count; i++)
    {
        const struct config_field_s *f = &config_fields[i];

        if (f->kind == SETTINGS_SECTION)
        {
            JsonObject section = fields.add<JsonObject>();

            section["section"] = f->label;
            /* The tab the section belongs to, named from the same table the
             * touch screen titles its pages from: a client that groups the
             * form into tabs (devmgr does) cannot then drift from the
             * panel's own grouping. */
            section["tab"] = settings_tab_names[f->tab];
            continue;
        }

        JsonObject row = fields.add<JsonObject>();

        row["name"] = f->name;
        row["label"] = f->label;
        row["kind"] = field_kind_name(f);

        switch (f->kind)
        {
        case SETTINGS_TEXT:
            /* Masked, never the value: the same rule the test interface's
             * `config` and the MQTT publisher enforce. A client that wants
             * the password unchanged sends the mask back and the POST treats
             * it as a no-op. */
            row["value"] = (f->flags & SETTINGS_F_SECRET)
                               ? "***"
                               : config_field_text(f, item);
            break;

        case SETTINGS_BOOL:
            row["value"] = config_field_read(f, item) != 0;
            break;

        case SETTINGS_ENUM:
        {
            int32_t index = config_field_read(f, item);

            if (index < 0 || index >= (int32_t)f->count)
                index = 0;

            row["value"] = f->names[index];

            JsonArray options = row["options"].to<JsonArray>();

            for (uint8_t n = 0; n < f->count; n++)
                options.add(f->names[n]);
            break;
        }

        default:
            row["value"] = config_field_read(f, item);
            row["min"] = f->min;
            row["max"] = f->max;
            break;
        }

        if (f->flags & SETTINGS_F_RESTART)
            row["restart"] = true;
        if (f->flags & SETTINGS_F_SECRET)
            row["secret"] = true;
    }

    return emit(doc, out, out_size);
}

/* --------------------------------------------------------------- config set */

/* A number out of a JSON value, without asking the caller to care which of
 * the numeric spellings ArduinoJson reports. Booleans are deliberately not
 * numbers here: a bool field reads them first. */
static bool json_to_long(JsonVariant v, long *out)
{
    if (v.is<long long>() == true)
    {
        *out = (long)v.as<long long>();
        return true;
    }

    if (v.is<double>() == true)
    {
        *out = (long)v.as<double>();
        return true;
    }

    return false;
}

/* openHAB's ON/OFF, JSON's true/false and a bare 1/0 -- the same vocabulary
 * ohez_mqtt.cpp's value_is_on() accepts on the broker, so a rule written for
 * one front end reads the same on this one. */
static bool json_to_bool(JsonVariant v, bool *out)
{
    if (v.is<bool>() == true)
    {
        *out = v.as<bool>();
        return true;
    }

    long number;

    if (json_to_long(v, &number) == true && (number == 0 || number == 1))
    {
        *out = (number != 0);
        return true;
    }

    if (v.is<const char *>() == true)
    {
        const char *text = v.as<const char *>();

        if (strcasecmp(text, "true") == 0 || strcasecmp(text, "on") == 0)
        {
            *out = true;
            return true;
        }

        if (strcasecmp(text, "false") == 0 || strcasecmp(text, "off") == 0)
        {
            *out = false;
            return true;
        }
    }

    return false;
}

/* One field. Stores into `item` and returns true, or stores nothing and
 * returns false. The masked echo of a secret is neither: it is a no-op, and
 * *noop_out says so. */
static bool apply_one(config_item_t *item, const struct config_field_s *f,
                      JsonVariant value, bool *noop_out)
{
    *noop_out = false;

    switch (f->kind)
    {
    case SETTINGS_TEXT:
        if (value.is<const char *>() == false)
            return false;

        if ((f->flags & SETTINGS_F_SECRET) != 0
            && strcmp(value.as<const char *>(), "***") == 0)
        {
            *noop_out = true;
            return true;
        }

        return config_field_set_text(f, item, value.as<const char *>());

    case SETTINGS_BOOL:
    {
        bool flag;

        if (json_to_bool(value, &flag) == false)
            return false;

        config_field_write(f, item, flag ? 1 : 0);
        return true;
    }

    case SETTINGS_ENUM:
        /* Strict, like the test interface's `set` and unlike
         * config_field_enum_from_name(): an unknown name falling back to the
         * first option is the right thing for a config file and the wrong
         * thing for an API call, where a misspelt theme would silently select
         * another one. */
        if (value.is<const char *>() == false)
            return false;

        for (uint8_t n = 0; n < f->count; n++)
        {
            if (strcasecmp(value.as<const char *>(), f->names[n]) == 0)
            {
                config_field_write(f, item, n);
                return true;
            }
        }

        return false;

    default:
    {
        long number;

        if (json_to_long(value, &number) == true)
        {
            config_field_set_number(f, item, number);
            return true;
        }

        if (value.is<const char *>() == true)
        {
            const char *text = value.as<const char *>();
            char       *end  = NULL;

            number = strtol(text, &end, 10);

            if (end != text && *end == '\0')
            {
                config_field_set_number(f, item, number);
                return true;
            }
        }

        return false;
    }
    }
}

bool webui_api_config_apply(config_item_t *item, const char *body, size_t body_len,
                            char *out, size_t out_size)
{
    JsonDocument request;

    if (deserializeJson(request, body, body_len) != DeserializationError::Ok
        || request.is<JsonObject>() == false)
    {
        JsonDocument error;

        error["error"] = "expected a JSON object";
        emit(error, out, out_size);
        return false;
    }

    /* The rollback copy: any rejection puts the whole struct back, so a
     * half-applied request cannot reach the flash. config_item_t is plain
     * data -- the offsets in the settings table are relative to it precisely
     * so that this is legal. */
    config_item_t before = *item;

    JsonDocument response;

    JsonArray  applied  = response["applied"].to<JsonArray>();
    JsonObject rejected = response["rejected"].to<JsonObject>();

    for (JsonPair pair : request.as<JsonObject>())
    {
        const char *name = pair.key().c_str();

        const struct config_field_s *f = config_field_by_name(name);

        if (f == NULL)
        {
            rejected[name] = "unknown setting";
            continue;
        }

        bool noop;

        if (apply_one(item, f, pair.value(), &noop) == false)
        {
            rejected[name] = "value not accepted";
            continue;
        }

        if (noop == false)
            applied.add(name);
    }

    if (rejected.size() > 0)
    {
        *item = before;

        /* "applied" is noise next to a rollback: nothing was applied, so the
         * response says only what was wrong. */
        response.remove("applied");

        emit(response, out, out_size);
        return false;
    }

    response.remove("rejected");

    emit(response, out, out_size);
    return true;
}

/* ------------------------------------------------------------------- sound */

size_t webui_api_sounds_json(char *out, size_t out_size)
{
    JsonDocument doc;

    JsonArray sounds = doc["sounds"].to<JsonArray>();

    for (int s = 0; s < UI_SOUND_COUNT; s++)
        sounds.add(ui_sound_names[s]);

    return emit(doc, out, out_size);
}

bool webui_api_sound_parse(const char *body, size_t body_len,
                           enum ui_sound_e *sound_out, bool *force_out,
                           char *out, size_t out_size)
{
    JsonDocument request;

    const char *name = NULL;

    *force_out = false;

    if (deserializeJson(request, body, body_len) == DeserializationError::Ok
        && request.is<JsonObject>() == true)
    {
        name = request["name"];
        *force_out = request["force"] | false;
    }

    enum ui_sound_e sound = (name != NULL) ? ui_sound_from_name(name)
                                           : UI_SOUND_COUNT;

    if (sound == UI_SOUND_COUNT)
    {
        /* Say what the vocabulary is rather than only that the name missed:
         * the answer to "what can I ask for" is in the error, the same shape
         * GET /api/sounds returns. */
        JsonDocument error;

        error["error"] = (name != NULL) ? "unknown sound"
                                        : "expected a JSON object with a name";

        JsonArray sounds = error["sounds"].to<JsonArray>();

        for (int s = 0; s < UI_SOUND_COUNT; s++)
            sounds.add(ui_sound_names[s]);

        emit(error, out, out_size);
        return false;
    }

    *sound_out = sound;

    JsonDocument response;

    response["queued"] = ui_sound_names[sound];

    emit(response, out, out_size);
    return true;
}
