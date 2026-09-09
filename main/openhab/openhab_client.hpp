/**
 * @file openhab_client.hpp
 *
 * The openHAB requests, on a task of their own.
 *
 * Everything this firmware asks openHAB for is a short round trip that can
 * nevertheless take the full OPENHAB_HTTP_TIMEOUT_MS: a sitemap page, a widget
 * icon, an item's state, or a command with nothing wanted back. Until this
 * existed they were all made from the task that also drives LVGL, so a page
 * switch -- one page GET followed by up to six icon GETs -- could hold the
 * screen for half a minute against a server that was merely slow, with
 * lv_timer_handler() never pumped and the panel dead to the touch.
 *
 * So the waiting moves here. The caller submits a URL and carries on drawing;
 * the answer arrives on a queue it drains at the top of its loop.
 *
 * What the worker may touch is deliberately almost nothing. It calls no lv_*,
 * reads no Item or Sitemap, and does not look at Config. URLs go in as bytes
 * and bodies come back as bytes; every decision about what a body means, and
 * every LVGL call, stays on the task that owns the screen. That is what keeps
 * main.cpp's invariant true -- lv_conf.h still sets LV_USE_OS to LV_OS_NONE,
 * LVGL still has no locking, and it still does not need any.
 *
 * One worker, not a pool. Two would fetch a page's six icons three times
 * faster and would also be free to deliver two taps on the same item out of
 * order, which is a worse bargain than it sounds.
 */
#ifndef OPENHAB_CLIENT_HPP
#define OPENHAB_CLIENT_HPP

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Every URL this client carries, at one width. Matches STR_URL_LEN in
 * openhab_connector.hpp, which is where the URLs are built; the two are
 * checked against each other in openhab_client.cpp rather than by including a
 * header full of ArduinoJson here. */
#define OPENHAB_CLIENT_URL_LEN 256

/* A command body is an item state, which is the widest thing ever posted. */
#define OPENHAB_CLIENT_BODY_LEN 32

/* The largest sitemap page and the largest icon that will be read. Both were
 * previously sized at their point of use -- SITEMAP_PAGE_BUFFER_SIZE in the
 * connector, ICON_PNG_BUFFER_SIZE in the UI -- and both are the worker's
 * business now, because it is the worker that reads into them. */
#define OPENHAB_CLIENT_PAGE_BUFFER_SIZE 12288
#define OPENHAB_CLIENT_ICON_BUFFER_SIZE 5000

/* Deep enough for everything one page can have outstanding at once -- six
 * icons and six states -- with room for a tap and a sensor reading arriving in
 * the middle of it. A submit that does not fit is reported to the caller
 * rather than waited on. */
#define OPENHAB_CLIENT_REQUEST_QUEUE_DEPTH 12

/* Shallow on purpose. The UI takes one result per iteration of a loop that
 * runs every few milliseconds, so this is empty in steady state; what the
 * depth really bounds is how many finished payloads can be held in the queue
 * at once if the UI is busy, and each of those is up to a page in size. The
 * worker blocks rather than dropping when it is full, which is the whole of
 * the back-pressure. */
#define OPENHAB_CLIENT_RESULT_QUEUE_DEPTH 4

/* For a request that belongs to no particular widget. */
#define OPENHAB_CLIENT_SLOT_NONE 0xFFu

/* The generation that never goes stale. Commands carry it: a command the user
 * asked for is still worth delivering after they have navigated away from the
 * tile they tapped. */
#define OPENHAB_CLIENT_GENERATION_ALWAYS 0u

enum openhab_request_e
{
    OPENHAB_REQ_PAGE,     /* GET a sitemap page, as JSON */
    OPENHAB_REQ_ICON,     /* GET a widget icon, as PNG */
    OPENHAB_REQ_STATE,    /* GET one item's state, as text */
    OPENHAB_REQ_COMMAND,  /* POST text/plain, nothing wanted back */
};

/**
 * A finished request.
 *
 * `payload` is on the heap and belongs to whoever took the result off the
 * queue -- including code that decides to drop it. Release it with
 * openhab_client_result_release() on every path.
 *
 * `ok == true` with `payload == NULL` means "nothing to apply, and that is not
 * an error". A command reports success that way, and so does offline mode for
 * a state poll: the fixtures carry a fixed state per item, so leaving the item
 * alone is what makes a switch toggled locally look like it worked.
 */
struct openhab_result_s
{
    enum openhab_request_e type;
    uint32_t generation;
    uint8_t  slot;        /* the widget it was issued for, or _SLOT_NONE */
    bool     ok;
    int      status;      /* HTTP status where there was one, else 0 */
    char    *payload;     /* NUL-terminated at [payload_len], or NULL */
    size_t   payload_len;
};

/**
 * Create the queues and start the worker.
 *
 * @return false if either queue or the task could not be created, in which
 *   case every submit below also returns false rather than silently doing
 *   nothing. The caller counts those failures, and enough of them restart the
 *   panel -- which is the only way a client that never started becomes
 *   visible from the outside.
 */
bool openhab_client_setup(void);

/**
 * Fetch a sitemap page, and invalidate everything issued for the previous one.
 *
 * Invalidating is part of submitting rather than a call of its own, because
 * the generation *is* the identity of the page being fetched or last accepted.
 * Keeping the two together is what stops a slow first attempt landing after
 * its own retry and re-parsing the older page.
 *
 * @return the new generation, to be stamped on the icon and state requests
 *   that the page's widgets will need, or 0 if the request could not be
 *   queued.
 */
uint32_t openhab_client_request_page(const char *url);

/**
 * Fetch a widget icon or an item state for widget `slot`.
 *
 * `generation` is the value the page fetch returned. A request whose
 * generation has been superseded by the time the worker reaches it is dropped
 * without being made, and one superseded while it was being made is dropped
 * without a result -- so a page change costs nothing for the requests it
 * orphans.
 */
bool openhab_client_request_icon(const char *url, uint8_t slot, uint32_t generation);
bool openhab_client_request_state(const char *url, uint8_t slot, uint32_t generation);

/**
 * POST `body` to `url` as text/plain, which is how openHAB's REST API takes a
 * command or a state update. Fire and forget: a result is produced, but it
 * carries no payload and there is nothing in it to apply.
 */
bool openhab_client_command(const char *url, const char *body);

/**
 * Take at most one finished request.
 *
 * @return false when there is none. Never blocks -- the only caller is the
 *   task that draws.
 */
bool openhab_client_poll(struct openhab_result_s *out);

/**
 * Free a result's payload and blank it. Idempotent, so a path that is not sure
 * whether it has already released can call it again.
 */
void openhab_client_result_release(struct openhab_result_s *res);

#endif /* OPENHAB_CLIENT_HPP */
