/**
 * @file openhab_event_parse.cpp
 *
 * See openhab_event_parse.hpp.
 */

#include "openhab_event_parse.hpp"

#include <ArduinoJson.h>

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

void sse_reader_reset(struct sse_reader *r)
{
    r->len = 0;
    r->overflow = false;
}

static void sse_line_end(struct sse_reader *r, sse_data_cb cb, void *ctx)
{
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

    sse_reader_reset(r);
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
