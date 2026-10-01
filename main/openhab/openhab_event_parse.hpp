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

/* Item names are [A-Za-z0-9_]; openHAB sets no limit, the panel does. A name
 * that does not fit cannot be matched to a tile, so it is refused rather than
 * truncated into somebody else's name. */
#define OPENHAB_EVENT_NAME_LEN 64

/* One "data:" line. An item event is two to three hundred bytes; the largest
 * are HSB and DateTime states, which still fit with room to spare. A longer
 * line is dropped whole -- it can only be an event the panel did not ask for. */
#define SSE_LINE_LEN 512

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
};

void sse_reader_reset(struct sse_reader *r);
void sse_reader_feed(struct sse_reader *r, const char *buf, size_t len,
                     sse_data_cb cb, void *ctx);

#endif /* OPENHAB_EVENT_PARSE_HPP */
