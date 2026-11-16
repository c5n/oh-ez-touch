/**
 * @file connector.hpp
 *
 * The boundary between the panel and the server it shows.
 *
 * A build contains exactly one connector: openHAB (main/openhab/) or Home
 * Assistant (main/homeassistant/), chosen by CONFIG_OHEZ_CONNECTOR in
 * main/Kconfig.projbuild. Like the port layer, this is a link-time interface:
 * the header is shared, and the active connector's directory implements every
 * function in it. A function the connector does not provide is a link error,
 * not a silent no-op.
 *
 * The UI (ui/openhab_ui.cpp) and main.cpp talk to the server only through
 * this header. What goes in is an opaque reference string -- for openHAB a
 * URL, for Home Assistant whatever its connector stores in an Item's link --
 * and what comes back is a result on a queue. Every decision about what a
 * body means stays with the connector (connector_page_parse()) or with the
 * shared page model (model/page_model.hpp).
 *
 * The types below were the openHAB client's own. They keep their shape, so
 * the openHAB connector forwards to its client, event stream and doorbell
 * without a conversion.
 */
#ifndef CONNECTOR_HPP
#define CONNECTOR_HPP

#include "config/config.hpp"
#include "connector_types.h"
#include "model/page_model.hpp"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ------------------------------------------------------------ the server */

/* Where the configured server is, read from the active connector's section
 * of Config. `root` is the page set: the sitemap for openHAB, the dashboard
 * for Home Assistant. The pointers point into `config`. */
struct connector_server_s
{
    const char *host;
    int         port;
    const char *root;
};

struct connector_server_s connector_server(const Config *config);

/**
 * Write the reference of the root page of page set `root` on the server at
 * `website` ("http://host:port") to `out`. For openHAB this is the sitemap's
 * URL. A connector builds the references of all other pages itself, while it
 * parses a page (an Item's page link).
 *
 * @return false if the reference did not fit; `out` then holds what fitted.
 */
bool connector_root_ref(char *out, size_t out_size, const char *website, const char *root);

/* The connector's name for logs, the status page and the control interface:
 * "openHAB" or "Home Assistant". */
const char *connector_name(void);

/* -------------------------------------------------------------- lifetime */

/**
 * Hand the connector the settings its own task reads: for Home Assistant the
 * server, the access token, the dashboard and the clock frame's card title.
 * The connector copies what it needs; a change takes effect with the next
 * connection. Called from main.cpp at setup, before connector_setup(), and
 * whenever settings are applied. May be called from any task.
 */
void connector_configure(const Config *config);

/**
 * Start the connector's tasks and queues. Called once from main.cpp, after
 * the work buffer exists and before the first request.
 *
 * @return false if a part could not be started. The connector then fails
 *   every request instead of doing nothing silently.
 */
bool connector_setup(void);

/**
 * Drop every held connection: the WLAN link changed, and a TCP connection
 * that spanned a reassociation is dead. May be called from any task.
 */
void connector_link_reset(void);

/**
 * The connector's work outside the tile page that has to run while the
 * station is offline too, such as the settings screen's server scan. Called
 * every iteration of the main loop.
 */
void connector_loop(void);

/* -------------------------------------------------------------- requests */

/**
 * Fetch a page, and invalidate everything issued for the previous one.
 *
 * @return the new generation, to be stamped on the icon and state requests
 *   for the page's widgets, or 0 if the request could not be queued.
 */
uint32_t connector_request_page(const char *ref);

/**
 * Fetch a widget icon or an item state for widget `slot`. A request whose
 * `generation` is no longer current is dropped without a result.
 */
bool connector_request_icon(const char *ref, uint8_t slot, uint32_t generation);
bool connector_request_state(const char *ref, uint8_t slot, uint32_t generation);

/** Send `body` as a command to the item `ref`. Fire and forget. */
bool connector_command(const char *ref, const char *body);

/**
 * Fetch the picture an Image item holds and decode it to at most
 * `max_w` x `max_h`. `tag` comes back as the result's slot.
 */
bool connector_request_image(const char *ref, uint16_t max_w, uint16_t max_h, uint8_t tag);

/** Take at most one finished request. Never blocks. */
bool connector_poll(struct connector_result_s *out);

/** Free a result's payload and blank it. Idempotent. */
void connector_result_release(struct connector_result_s *res);

/**
 * Turn a page result's payload into `sitemap`.
 *
 * `scratch`/`scratch_size` is memory the parse may use for its document; see
 * Sitemap::parse() for the openHAB case. NULL makes the parse use the heap.
 *
 * @return 0, or -1 for a page that did not parse.
 */
int connector_page_parse(Sitemap *sitemap, const char *payload, size_t payload_len,
                         char *scratch, size_t scratch_size);

/* ---------------------------------------------------------------- events */

/**
 * Listen for `sub` from now on. Cheap to call with the same set again. A
 * connector without push ignores it, and the UI keeps polling.
 */
void connector_events_subscribe(const struct connector_subscription_s *sub);

/** True while pushed changes arrive, so the UI may poll less often. */
bool connector_events_streaming(void);

/** True once after changes may have gone unseen: poll every tile once. */
bool connector_events_take_resync(void);

/** True once after the page on screen has to be fetched again. */
bool connector_events_take_reload(void);

/** Take at most one pushed change. Never blocks. */
bool connector_events_poll(struct connector_event_s *out);

#endif /* CONNECTOR_HPP */
