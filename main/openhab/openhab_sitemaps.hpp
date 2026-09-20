/**
 * @file openhab_sitemaps.hpp
 *
 * Which sitemaps the server offers, so that the one this panel shows can be
 * picked from a list instead of typed.
 *
 * The sitemap name has always been a text field in config.json and in both
 * settings front ends, which means it is a name somebody has to know, spell
 * and keep in step with the server. openHAB will say what it serves -- GET
 * /rest/sitemaps is a list of every sitemap with its name and label -- so this
 * asks, and keeps the answer.
 *
 * One cache for both front ends, because they want the same list and the fetch
 * is the same fetch: the settings screen asks when the openHAB page opens, the
 * web form asks when the page is loaded, and whichever comes second usually
 * finds the answer already here.
 *
 * Which task does what, since three of them are involved:
 *
 *   - openhab_sitemaps_request() only records a want. It is called from the
 *     settings screen on the UI task and from the web handler on the server's
 *     task, and neither is a place to start a fetch -- the same reason
 *     openhab_ui_request_connect() is a request rather than a call.
 *   - openhab_sitemaps_loop() makes the request and ages it out. It runs on the
 *     task that owns the loop, which is also the one that applies the answer.
 *   - the readers below run on either task, unlocked, the way Config's readers
 *     do. The worst a browser can see is a list caught between two refreshes of
 *     the same server's sitemaps; SitemapList publishes its count last so that
 *     it can never be a count without names behind it.
 */
#ifndef OPENHAB_SITEMAPS_HPP
#define OPENHAB_SITEMAPS_HPP

#include "openhab_client.hpp"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum openhab_sitemaps_state_e
{
    OPENHAB_SITEMAPS_IDLE = 0, /* never asked, or asked about another server */
    OPENHAB_SITEMAPS_FETCHING,
    OPENHAB_SITEMAPS_READY,    /* the list below is this server's answer */
    OPENHAB_SITEMAPS_FAILED    /* no answer, or one that was not a list */
};

/**
 * Ask what `host`:`port` serves, from any task.
 *
 * Records the want; openhab_sitemaps_loop() makes the request. A want that
 * arrives while a fetch is in flight is kept and served after it, so that
 * changing the host in the settings screen while the old host is still being
 * waited on ends with the new host's sitemaps rather than the old host's.
 *
 * Every call refetches, deliberately: this is called when somebody opens the
 * page that shows the list, and a panel that has been sitting on a stale answer
 * since it booted is exactly what the list is meant to replace.
 */
void openhab_sitemaps_request(const char *host, uint16_t port);

/**
 * Submit a recorded want, and fail a fetch that has gone unanswered.
 *
 * Called unconditionally from the main loop rather than from openhab_ui_loop(),
 * which only runs while the station is online: a panel that cannot reach its
 * server should report an empty list rather than an endless "Loading".
 */
void openhab_sitemaps_loop(void);

/**
 * Fold a finished OPENHAB_REQ_SITEMAPS result into the cache.
 *
 * Called from the one place that drains the client's result queue -- there is
 * a single queue and openhab_ui.cpp owns it -- and only for results of that
 * type. The payload is not released here; the caller does that as it does for
 * every other result.
 */
void openhab_sitemaps_apply(const struct openhab_result_s *res);

enum openhab_sitemaps_state_e openhab_sitemaps_state(void);

/* How many sitemaps are on offer, and how many the server listed. They differ
 * when the server has more than the panel will hold; both front ends say so
 * rather than presenting the first twelve as all of them. */
size_t openhab_sitemaps_count(void);
size_t openhab_sitemaps_total(void);

/* "" for an index that is not there, so a caller can print it without testing
 * it. The label is the sitemap's own where it has one, its name where it has
 * not. */
const char *openhab_sitemaps_name(size_t index);
const char *openhab_sitemaps_label(size_t index);

/**
 * Bumped whenever the state or the list changes, and never zero.
 *
 * What the settings screen watches: it rebuilds its list of sitemaps when this
 * moves, which is cheaper and steadier than comparing the list itself every
 * few milliseconds.
 */
uint32_t openhab_sitemaps_revision(void);

#endif /* OPENHAB_SITEMAPS_HPP */
