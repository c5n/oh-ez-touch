/**
 * @file openhab_events.hpp
 *
 * Item states pushed by openHAB, on a task of their own.
 *
 * Polling asked openHAB for every visible item's state every five seconds:
 * nine requests per five seconds that almost always answered "no change", and
 * a change made anywhere else took up to five seconds to reach the glass. The
 * server can say when a state changes instead -- GET /rest/events?topics=...
 * is a server-sent event stream that stays open and carries one line per
 * change -- and this is the task that keeps it open.
 *
 * Its own task and its own esp_http_client handle, because a stream is a
 * request that never finishes: on the openHAB client task it would hold up
 * every page, icon and command behind it, and that task's handle is one task
 * only (openhab_http.hpp).
 *
 * The same ground rules as the client task. It calls no lv_*, reads no Item and
 * no Config: item names go in, (name, value) pairs come out on a queue the UI
 * drains. Matching a name to a tile, and deciding what the value means, stays
 * on the task that owns the screen.
 *
 * Two streams, one at a time. Where the server has it, the task follows the
 * page on screen through GET /rest/sitemaps/events/<subscription>: one event
 * per widget whose state, formatted value, colours or visibility changed, as
 * openHAB's rules evaluated them. Where it has not -- the subscribe POST is
 * refused -- it falls back to the item state changes of /rest/events, which
 * carry the raw state and nothing else.
 *
 * Polling does not go away. It is what keeps the tiles right while the stream
 * is down -- an openHAB restarting, a link coming back, a server too old to
 * have the endpoint -- and openhab_ui.cpp slows it to a safety net while the
 * stream is up. Nothing is replayed after a reconnect, so every connect asks
 * the UI for one immediate poll of everything: see openhab_events_take_resync().
 */
#ifndef OPENHAB_EVENTS_HPP
#define OPENHAB_EVENTS_HPP

#include "openhab_event_parse.hpp"

#include <stdbool.h>
#include <stddef.h>

/* The tiles and the clock items, with room to spare. */
#define OPENHAB_EVENTS_ITEM_MAX 12

/* The width of an item state; checked against STR_STATE_TEXT_LEN where both
 * are in scope, in openhab_ui.cpp. */
#define OPENHAB_EVENTS_VALUE_LEN 32

/* "http://host:port", which is what openhab_ui.cpp calls the website. */
#define OPENHAB_EVENTS_WEBSITE_LEN 128

/* Deep enough for a scene switching every tile at once, plus the clock items.
 * An event that does not fit is dropped and a resync asked for instead, so
 * the depth bounds latency under a burst rather than correctness. */
#define OPENHAB_EVENTS_QUEUE_DEPTH 8

/* A sitemap name, at the width Config stores it, and a page id. */
#define OPENHAB_EVENTS_SITEMAP_LEN 32
#define OPENHAB_EVENTS_PAGE_LEN 64

/* What to listen for. Built by the UI, copied whole by subscribe(). */
struct openhab_events_subscription_s
{
    char   website[OPENHAB_EVENTS_WEBSITE_LEN];
    /* The page on screen, for the sitemap stream. Either one "" falls back to
     * the items below at once. */
    char   sitemap[OPENHAB_EVENTS_SITEMAP_LEN];
    char   page[OPENHAB_EVENTS_PAGE_LEN];
    /* The items on screen, for the item stream. */
    size_t count;
    struct
    {
        char name[OPENHAB_EVENT_NAME_LEN];
        /* A group's own state changes arrive on
         * "items/<group>/<member>/statechanged", which a topic for
         * "items/<group>/statechanged" does not match. */
        bool group;
    } item[OPENHAB_EVENTS_ITEM_MAX];
};

enum openhab_event_kind_e
{
    OPENHAB_EVENT_KIND_ITEM,   /* `item`: an item stream's state change      */
    OPENHAB_EVENT_KIND_WIDGET, /* `widget`: a sitemap stream's widget change */
};

struct openhab_event_s
{
    enum openhab_event_kind_e kind;

    union
    {
        struct
        {
            char name[OPENHAB_EVENT_NAME_LEN];
            char value[OPENHAB_EVENTS_VALUE_LEN];
        } item;

        struct openhab_widget_event_s widget;
    };
};

/**
 * Create the queue and start the task. Idle until the first subscribe().
 *
 * @return false if either could not be created. Every call below is then a
 *   harmless no-op, and the panel polls as it did before this existed.
 */
bool openhab_events_setup(void);

/**
 * Listen for `sub` from now on, replacing whatever was listened for before.
 *
 * Cheap to call with the same set again -- it is compared and ignored -- so
 * the UI does not have to track when its set changed. A different set closes
 * the stream and opens a new one, since a topic filter is fixed for the life
 * of a stream. A `count` of 0 closes it and leaves it closed.
 */
void openhab_events_subscribe(const struct openhab_events_subscription_s *sub);

/**
 * Close the stream and open it again, for the reason openhab_http_reset()
 * exists: a connection that spanned a reassociation is dead and does not know
 * it. May be called from any task.
 */
void openhab_events_reset(void);

/** True while a stream is open and has answered 200. */
bool openhab_events_streaming(void);

/**
 * True once after every connect, and after an event was dropped for want of
 * queue room. Either way some changes may have gone unseen, and the answer is
 * to poll everything once.
 */
bool openhab_events_take_resync(void);

/**
 * True once after the page on screen has to be fetched again: the sitemap
 * stream said the sitemap file or a state description changed, it reconnected
 * after having been up -- so colours and visibility may have changed unseen,
 * which a resync's polls cannot bring back -- or it dropped an event too long
 * to read.
 */
bool openhab_events_take_reload(void);

/** Take at most one event. Never blocks. */
bool openhab_events_poll(struct openhab_event_s *out);

#endif /* OPENHAB_EVENTS_HPP */
