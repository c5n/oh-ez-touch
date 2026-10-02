/**
 * @file openhab_event_parse.hpp
 *
 * What openHAB's event stream says, without the stream.
 *
 * GET /rest/events?topics=... is a server-sent event stream: lines of text,
 * each "data:" line one JSON event, and a blank line between events. The task
 * that reads it lives in openhab_events.cpp and owns a socket; this is the
 * half that only decides what the bytes mean, split out for the same reason
 * openhab_connector.cpp was -- so that the host tests can reach it.
 *
 * Two layers, both pure:
 *
 *  - sse_reader splits the stream into lines and hands every "data:" line on.
 *    The stream arrives in reads of whatever size the socket chose, so a line
 *    may span any number of them.
 *  - openhab_event_parse() reads one such line. An item event is
 *
 *      {"topic":"openhab/items/X/statechanged",
 *       "payload":"{\"type\":\"OnOff\",\"value\":\"ON\",...}",
 *       "type":"ItemStateChangedEvent"}
 *
 *    with the payload a JSON object encoded as a string, so it is parsed twice.
 *    Its "value" is the same raw state "GET /rest/items/X/state" answers --
 *    "21.5 °C", "ON", "NULL" -- which is what lets Item::applyState() take an
 *    event exactly as it takes a poll.
 */
#ifndef OPENHAB_EVENT_PARSE_HPP
#define OPENHAB_EVENT_PARSE_HPP

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Item names are [A-Za-z0-9_]; openHAB sets no limit, the panel does. A name
 * that does not fit cannot be matched to a tile, so it is refused rather than
 * truncated into somebody else's name. */
#define OPENHAB_EVENT_NAME_LEN 64

/* One "data:" line. An item event is two to three hundred bytes; the largest
 * are HSB and DateTime states, which still fit with room to spare. A sitemap
 * widget event carries the whole item with it -- its state description, its
 * command options -- and runs from 500 bytes for a plain Number to 900 for one
 * with three options on openHAB 5.2.1. A longer line is dropped whole, and the
 * reader says so: see sse_reader_take_overflow(). */
#define SSE_LINE_LEN 2048

/* The width of a state, and of the two halves of a widget's label. */
#define OPENHAB_EVENT_TEXT_LEN 32

enum openhab_event_e
{
    OPENHAB_EVENT_INVALID, /* not JSON, or not shaped like an event      */
    OPENHAB_EVENT_ALIVE,   /* the server's keepalive, every ten seconds  */
    OPENHAB_EVENT_OTHER,   /* an event, but not an item state change     */
    OPENHAB_EVENT_STATE,   /* name and value are set                     */
};

/**
 * Read one event.
 *
 * The item name is the third segment of the topic, so a group's
 * "openhab/items/<group>/<member>/statechanged" names the group -- which is
 * the item whose state changed. The root segment is not checked: openHAB 2
 * calls it "smarthome".
 *
 * @param value receives the state, truncated to `value_size` like a polled
 *   state is.
 */
enum openhab_event_e openhab_event_parse(const char *json, size_t len,
                                         char *name, size_t name_size,
                                         char *value, size_t value_size);

/* What GET /rest/sitemaps/events/<subscription> says about one widget of the
 * page it was opened for, as openHAB 5.2.1 sends it:
 *
 *   {"widgetId":"1_0","label":"Temperature [30,0 °C]","labelSource":"ITEM_LABEL",
 *    "icon":"temperature","reloadIcon":true,"valuecolor":"orange",
 *    "iconcolor":"red","visibility":true,
 *    "item":{"state":"30 °C","stateDescription":{...},...,"name":"EV_Temp",...},
 *    "descriptionChanged":false,"sitemapName":"ohezevents","pageId":"ohezevents"}
 *
 * Unlike an item event it is the widget as the page would show it now: the
 * label with openHAB's formatted value in it, and the colours and visibility
 * its rules give the new state. A colour is absent, not empty, when no rule
 * applies any more, so absent means "the theme's".
 *
 * Widgets and pages are known by a hash of their id -- see openhab_id_hash()
 * -- so an Item can carry one in four bytes. */
struct openhab_widget_event_s
{
    uint32_t widget;
    uint32_t page;
    /* The item's raw state, the one /state answers; "" for a widget with no
     * item, like a Frame. */
    char     state[OPENHAB_EVENT_TEXT_LEN];
    /* The label before its "[...]", and what is inside it ("" for none). */
    char     caption[OPENHAB_EVENT_TEXT_LEN];
    char     display[OPENHAB_EVENT_TEXT_LEN];
    uint32_t label_color;  /* 0xRRGGBB or OPENHAB_COLOR_NONE */
    uint32_t value_color;
    uint32_t icon_color;
    bool     has_state;
    bool     visible;
};

enum openhab_sitemap_event_e
{
    OPENHAB_SITEMAP_EVENT_INVALID, /* not JSON, or not shaped like an event       */
    OPENHAB_SITEMAP_EVENT_ALIVE,   /* {"TYPE":"ALIVE"}, about once a minute        */
    OPENHAB_SITEMAP_EVENT_WIDGET,  /* `out` is set                                 */
    /* The page has to be fetched again: SITEMAP_CHANGED (the file was edited),
     * or a widget whose "descriptionChanged" says its state description did. */
    OPENHAB_SITEMAP_EVENT_RELOAD,
    OPENHAB_SITEMAP_EVENT_OTHER,   /* a "TYPE" this panel does not know            */
};

/**
 * Read one sitemap event. The item object, which is most of the line, is
 * filtered down to its state.
 */
enum openhab_sitemap_event_e openhab_sitemap_event_parse(const char *json, size_t len,
                                                         struct openhab_widget_event_s *out);

/**
 * The subscription id out of the answer to POST /rest/sitemaps/events/subscribe:
 *
 *   {"status":"CREATED","context":{"headers":{"Location":
 *     ["http://host:8080/rest/sitemaps/events/49bea9a9-...-1947db28adb2"]}},...}
 *
 * The last segment of the Location, and only that: the host in it is the one
 * the request named, which is not necessarily one the panel can reach.
 *
 * @return false for a body without one, or an id that does not fit.
 */
bool openhab_sitemap_subscription_id(const char *json, size_t len, char *id, size_t id_size);

/* FNV-1a over a widget or page id: "1_0", "1_50", or on a group's generated
 * page the member's item name. 0 is kept for "none", so an id that hashes to
 * it is moved to 1 -- one more collision, out of four billion. */
static inline uint32_t openhab_id_hash(const char *id)
{
    uint32_t hash = 2166136261u;

    if (id == NULL || id[0] == '\0')
        return 0;

    for (; *id != '\0'; id++)
    {
        hash ^= (uint8_t)*id;
        hash *= 16777619u;
    }

    return (hash == 0) ? 1 : hash;
}

/**
 * The line splitter.
 *
 * Only "data:" lines are handed on. "event:", "id:", comments and the blank
 * line between events are dropped: the event name is either "message" or
 * "alive" and the data line says which by itself, and an event of more than
 * one data line is never sent by openHAB.
 */
typedef void (*sse_data_cb)(const char *data, size_t len, void *ctx);

struct sse_reader
{
    char   line[SSE_LINE_LEN];
    size_t len;
    bool   overflow; /* the current line did not fit; drop it at its end */
    bool   dropped;  /* a line was dropped since the last take             */
};

void sse_reader_reset(struct sse_reader *r);

/* True once after a "data:" line was dropped for being too long: an event
 * went unseen, and the caller decides what that costs. */
bool sse_reader_take_overflow(struct sse_reader *r);
void sse_reader_feed(struct sse_reader *r, const char *buf, size_t len,
                     sse_data_cb cb, void *ctx);

#endif /* OPENHAB_EVENT_PARSE_HPP */
