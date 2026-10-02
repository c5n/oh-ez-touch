/**
 * @file openhab_event_parse.cpp
 *
 * See openhab_event_parse.hpp.
 */

#include "openhab_event_parse.hpp"

#include "openhab_color.h"

#include <ArduinoJson.h>

#include <ctype.h>
#include <string.h>

/* The third segment of "<root>/items/<name>/...". */
static bool topic_item_name(const char *topic, char *name, size_t name_size)
{
    const char *items = strchr(topic, '/');

    if (items == NULL || strncmp(items, "/items/", 7) != 0)
        return false;

    const char *start = items + 7;
    const char *end = strchr(start, '/');

    if (end == NULL || end == start)
        return false;

    size_t len = (size_t)(end - start);

    if (len >= name_size)
        return false;

    memcpy(name, start, len);
    name[len] = '\0';

    return true;
}

enum openhab_event_e openhab_event_parse(const char *json, size_t len,
                                         char *name, size_t name_size,
                                         char *value, size_t value_size)
{
    /* Only ever called from the one task that reads the stream, and from the
     * host tests, which are one thread too; so the filters are built once and
     * kept. The documents are not: an event is a few hundred bytes, and a
     * handful a minute is not the churn the page parser's arena exists for. */
    static JsonDocument event_filter;
    static JsonDocument payload_filter;
    static bool filters_ready = false;

    if (filters_ready == false)
    {
        filters_ready = true;

        event_filter["topic"] = true;
        event_filter["payload"] = true;
        event_filter["type"] = true;
        payload_filter["value"] = true;
    }

    JsonDocument event;

    if (deserializeJson(event, json, len, DeserializationOption::Filter(event_filter))
        != DeserializationError::Ok)
        return OPENHAB_EVENT_INVALID;

    const char *type = event["type"].as<const char *>();

    if (type == NULL)
        return OPENHAB_EVENT_INVALID;

    if (strcmp(type, "ALIVE") == 0)
        return OPENHAB_EVENT_ALIVE;

    /* The server only sends the topics it was asked for, but a topic filter
     * with a wildcard is wider than it looks -- '*' also crosses '/' -- so the
     * type is what decides, not the topic. */
    if (   strcmp(type, "ItemStateChangedEvent") != 0
        && strcmp(type, "GroupItemStateChangedEvent") != 0)
        return OPENHAB_EVENT_OTHER;

    const char *topic = event["topic"].as<const char *>();
    const char *payload = event["payload"].as<const char *>();

    if (topic == NULL || payload == NULL)
        return OPENHAB_EVENT_INVALID;

    if (topic_item_name(topic, name, name_size) == false)
        return OPENHAB_EVENT_INVALID;

    JsonDocument state;

    if (deserializeJson(state, payload, DeserializationOption::Filter(payload_filter))
        != DeserializationError::Ok)
        return OPENHAB_EVENT_INVALID;

    const char *state_value = state["value"].as<const char *>();

    if (state_value == NULL)
        return OPENHAB_EVENT_INVALID;

    strlcpy(value, state_value, value_size);

    return OPENHAB_EVENT_STATE;
}

/* "Temperature [30,0 °C]" into "Temperature" and "30,0 °C". The caption the
 * way label_trim() in openhab_connector.cpp makes it, so the two compare; the
 * value up to the last ']', which a value may itself contain. */
static void label_split(const char *label, char *caption, size_t caption_size,
                        char *display, size_t display_size)
{
    const char *open = strchr(label, '[');
    size_t      len = (open != NULL) ? (size_t)(open - label) : strlen(label);

    while (len > 0 && isspace((unsigned char)label[len - 1]))
        len--;

    if (len >= caption_size)
        len = caption_size - 1;

    memcpy(caption, label, len);
    caption[len] = '\0';
    display[0] = '\0';

    if (open == NULL)
        return;

    const char *close = strrchr(open, ']');

    if (close == NULL)
        return;

    len = (size_t)(close - open - 1);

    if (len >= display_size)
        len = display_size - 1;

    memcpy(display, open + 1, len);
    display[len] = '\0';
}

enum openhab_sitemap_event_e openhab_sitemap_event_parse(const char *json, size_t len,
                                                         struct openhab_widget_event_s *out)
{
    /* Built once, as the item filter above is. */
    static JsonDocument filter;
    static bool filter_ready = false;

    if (filter_ready == false)
    {
        filter_ready = true;

        filter["TYPE"] = true;
        filter["widgetId"] = true;
        filter["pageId"] = true;
        filter["label"] = true;
        filter["labelcolor"] = true;
        filter["valuecolor"] = true;
        filter["iconcolor"] = true;
        filter["visibility"] = true;
        filter["descriptionChanged"] = true;
        filter["item"]["state"] = true;
    }

    JsonDocument event;

    if (deserializeJson(event, json, len, DeserializationOption::Filter(filter))
        != DeserializationError::Ok
        || event.is<JsonObject>() == false)
        return OPENHAB_SITEMAP_EVENT_INVALID;

    const char *type = event["TYPE"].as<const char *>();

    if (type != NULL)
    {
        if (strcmp(type, "ALIVE") == 0)
            return OPENHAB_SITEMAP_EVENT_ALIVE;

        if (strcmp(type, "SITEMAP_CHANGED") == 0)
            return OPENHAB_SITEMAP_EVENT_RELOAD;

        return OPENHAB_SITEMAP_EVENT_OTHER;
    }

    const char *widget_id = event["widgetId"].as<const char *>();

    if (widget_id == NULL || widget_id[0] == '\0')
        return OPENHAB_SITEMAP_EVENT_INVALID;

    if (event["descriptionChanged"].as<bool>() == true)
        return OPENHAB_SITEMAP_EVENT_RELOAD;

    out->widget = openhab_id_hash(widget_id);
    out->page = openhab_id_hash(event["pageId"].as<const char *>());

    const char *state = event["item"]["state"].as<const char *>();

    out->has_state = (state != NULL);
    strlcpy(out->state, (state != NULL) ? state : "", sizeof(out->state));

    const char *label = event["label"].as<const char *>();

    label_split((label != NULL) ? label : "", out->caption, sizeof(out->caption),
                out->display, sizeof(out->display));

    out->label_color = openhab_color_parse(event["labelcolor"].as<const char *>());
    out->value_color = openhab_color_parse(event["valuecolor"].as<const char *>());
    out->icon_color = openhab_color_parse(event["iconcolor"].as<const char *>());

    /* Only an explicit false hides, as on the page. */
    out->visible = !(event["visibility"].is<bool>() && event["visibility"].as<bool>() == false);

    return OPENHAB_SITEMAP_EVENT_WIDGET;
}

bool openhab_sitemap_subscription_id(const char *json, size_t len, char *id, size_t id_size)
{
    static JsonDocument filter;
    static bool filter_ready = false;

    if (filter_ready == false)
    {
        filter_ready = true;
        filter["context"]["headers"]["Location"] = true;
    }

    JsonDocument doc;

    if (deserializeJson(doc, json, len, DeserializationOption::Filter(filter))
        != DeserializationError::Ok)
        return false;

    const char *location = doc["context"]["headers"]["Location"][0].as<const char *>();

    if (location == NULL)
        return false;

    const char *slash = strrchr(location, '/');
    const char *start = (slash != NULL) ? slash + 1 : location;

    /* Goes into a URL path as it is, so only what an id is made of: the UUID
     * openHAB hands out is hex and dashes. */
    if (start[0] == '\0' || strspn(start, "0123456789abcdefABCDEF-") != strlen(start))
        return false;

    return strlcpy(id, start, id_size) < id_size;
}

void sse_reader_reset(struct sse_reader *r)
{
    r->len = 0;
    r->overflow = false;
    r->dropped = false;
}

bool sse_reader_take_overflow(struct sse_reader *r)
{
    bool dropped = r->dropped;

    r->dropped = false;

    return dropped;
}

static void sse_line_end(struct sse_reader *r, sse_data_cb cb, void *ctx)
{
    if (r->overflow == true && r->len >= 5 && memcmp(r->line, "data:", 5) == 0)
        r->dropped = true;

    if (r->overflow == false && r->len >= 5 && memcmp(r->line, "data:", 5) == 0)
    {
        const char *data = r->line + 5;
        size_t data_len = r->len - 5;

        /* "data: x" and "data:x" are the same line; the one space is part of
         * the framing, not of the data. */
        if (data_len > 0 && data[0] == ' ')
        {
            data++;
            data_len--;
        }

        r->line[r->len] = '\0';
        cb(data, data_len, ctx);
    }

    /* The next line, not the reader: a drop stays reported until taken. */
    r->len = 0;
    r->overflow = false;
}

void sse_reader_feed(struct sse_reader *r, const char *buf, size_t len,
                     sse_data_cb cb, void *ctx)
{
    for (size_t i = 0; i < len; ++i)
    {
        char c = buf[i];

        /* A line ends at LF, CR or CRLF. CRLF ends it at the CR and leaves an
         * empty line at the LF, which is dropped like the blank line between
         * events -- so the three need no state between them. */
        if (c == '\n' || c == '\r')
        {
            sse_line_end(r, cb, ctx);
            continue;
        }

        if (r->overflow == true)
            continue;

        /* One byte kept back for the terminator sse_line_end() writes. */
        if (r->len >= sizeof(r->line) - 1)
        {
            r->overflow = true;
            continue;
        }

        r->line[r->len++] = c;
    }
}
