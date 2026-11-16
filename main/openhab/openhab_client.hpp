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

#include "connector/connector_types.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* An icon is read into the same buffer as a page and is decoded from there
 * (see icons/icon_png.h), so it may fill all of it: there is no copy of the
 * size of the body any more. What a large PNG costs instead is scratch -- the
 * decode takes its first segments from what the PNG leaves free in the
 * buffer, and asks the heap for the rest. */
#define OPENHAB_CLIENT_ICON_BUFFER_SIZE CONNECTOR_PAGE_BUFFER_SIZE

/* And the list of sitemaps. A real openHAB 5.2.1 sends about 210 bytes per
 * sitemap there -- a link, a homepage object with a link and two flags, the
 * name and the label -- so this holds some forty of them, which is a long way
 * past the twelve the panel will offer. It is only ever allocated while
 * somebody is looking at the openHAB settings, on the panel or in a browser. */
#define OPENHAB_CLIENT_SITEMAPS_BUFFER_SIZE 8192

/* Deep enough for everything one page can have outstanding at once -- six
 * icons and six states -- with room for a tap arriving in the middle of it. A
 * submit that does not fit is reported to the caller rather than waited on. */
#define OPENHAB_CLIENT_REQUEST_QUEUE_DEPTH 12

/* Shallow on purpose. The UI takes one result per iteration of a loop that
 * runs every few milliseconds, so this is empty in steady state; what the
 * depth really bounds is how many finished payloads can be held in the queue
 * at once if the UI is busy, and each of those is up to a page in size. The
 * worker blocks rather than dropping when it is full, which is the whole of
 * the back-pressure. */
#define OPENHAB_CLIENT_RESULT_QUEUE_DEPTH 4

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
 * Fetch the list of sitemaps a server offers -- GET /rest/sitemaps.
 *
 * Carries CONNECTOR_GENERATION_ALWAYS, like a command and unlike
 * everything else here: this belongs to the settings screen and the web form,
 * not to the tile page, so navigating to another page while the answer is in
 * flight must not throw it away.
 *
 * The result is routed to openhab_sitemaps.cpp, which owns the cache both
 * front ends read.
 */
bool openhab_client_request_sitemaps(const char *url);

/**
 * GET the picture an Image item holds, from its "<link>/state", and decode it
 * on the client task to at most `max_w` x `max_h` (see image_decode.h).
 *
 * Never goes stale with the page: the doorbell's picture is opened from
 * whatever page is on screen. `tag` comes back as the result's slot, so the
 * screen that asked can tell its own answer from an older one's.
 */
bool openhab_client_request_image(const char *url, uint16_t max_w, uint16_t max_h, uint8_t tag);

/**
 * Take at most one finished request.
 *
 * @return false when there is none. Never blocks -- the only caller is the
 *   task that draws.
 */
bool openhab_client_poll(struct connector_result_s *out);

/**
 * Free a result's payload and blank it. Idempotent, so a path that is not sure
 * whether it has already released can call it again.
 *
 * A payload that rode inside the result is not freed, only forgotten -- see
 * `payload_inline`. Every caller releases the same way either way.
 */
void openhab_client_result_release(struct connector_result_s *res);

#endif /* OPENHAB_CLIENT_HPP */
