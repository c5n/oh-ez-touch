/**
 * @file openhab_sitemaps.cpp
 *
 * See openhab_sitemaps.hpp.
 */

#include "sdkconfig.h"

#include "openhab_sitemaps.hpp"

#include "config/config_fields.hpp"
#include "openhab_client.hpp"
#include "openhab_connector.hpp"
#include "openhab_http.hpp"
#include "port/port_sys.h"

#include <stdio.h>
#include <string.h>

/* A choice from this list is written straight into Config, so the two widths
 * have to agree. They are declared apart because openhab_connector.hpp is
 * reached from the host tests, where Config's FreeRTOS mutex is not. */
static_assert(sizeof(((config_item_t *)0)->openhab.sitemap) == STR_SITEMAP_NAME_LEN,
              "SitemapList holds names at a different width than Config stores them");

/* "http://" + a 32 byte hostname + ':' + five digits + "/rest/sitemaps". The
 * slack is what keeps a hostname that grew from being truncated into a URL
 * that would only ever 404. */
#define SITEMAPS_URL_LEN 96

/* How long an unanswered fetch stays "loading" before the screen says it
 * failed.
 *
 * Twice the HTTP timeout and a little: a request waits behind at most one
 * other in the worker's queue -- a page or an icon, each bounded by that
 * timeout -- before its own five seconds start. Shorter than this and a panel
 * that was merely fetching a page first would report a failure that never
 * happened. The backstop matters because a request that was never queued
 * produces no result at all. */
#define SITEMAPS_ANSWER_TIMEOUT_MS ((OPENHAB_HTTP_TIMEOUT_MS * 2) + 2000)

/* The one cache. About 800 bytes, and the reason there is only one: both front
 * ends want the same answer to the same question. */
static SitemapList list;

static enum openhab_sitemaps_state_e state = OPENHAB_SITEMAPS_IDLE;

/* Never zero, so that a front end can hold "the revision I last drew" in a
 * plain integer initialised to zero and be told about the first list it ever
 * sees. */
static uint32_t revision = 1;

/* What has been asked for but not yet submitted. Written from any task,
 * read and cleared by openhab_sitemaps_loop(). One want, not a queue: the
 * newest request is the only one worth making, because the list it asks for is
 * the list that is about to be looked at. */
static char     pending_host[32];
static uint16_t pending_port;
static bool     pending;

/* The URL of the fetch in flight, and when it stops being worth waiting for. */
static char     current_url[SITEMAPS_URL_LEN];
static uint64_t answer_deadline;

static void publish(enum openhab_sitemaps_state_e new_state)
{
    state = new_state;
    revision++;

    /* Zero is the value a front end starts at, so skip it on the wrap. */
    if (revision == 0)
        revision = 1;
}

void openhab_sitemaps_request(const char *host, uint16_t port)
{
    if (host == NULL)
        return;

    strlcpy(pending_host, host, sizeof(pending_host));
    pending_port = port;

    /* Last, and it matters: the loop may run on another task between these
     * statements, and a want it sees has to be one whose host is already
     * written. */
    pending = true;
}

void openhab_sitemaps_loop(void)
{
    if (state == OPENHAB_SITEMAPS_FETCHING)
    {
        if (port_millis() < answer_deadline)
            return;

        printf("openhab_sitemaps: no answer from %s\r\n", current_url);

        /* Emptied, like every other way this fails. A list kept beside a line
         * that says the server did not answer is a list nobody can tell the
         * age of, and the refresh that would replace it is the one that just
         * did not happen. */
        list.clear();
        publish(OPENHAB_SITEMAPS_FAILED);

        /* Falls through: a want recorded while this one was in flight is the
         * newer question and is asked now. */
    }

    if (pending == false)
        return;

    pending = false;

    char url[SITEMAPS_URL_LEN];
    int  len = snprintf(url, sizeof(url), "http://%s:%u/rest/sitemaps", pending_host,
                        (unsigned)pending_port);

    if (len < 0 || (size_t)len >= sizeof(url))
    {
        printf("openhab_sitemaps: URL truncated to %u bytes: %s\r\n",
               (unsigned)sizeof(url), url);
        list.clear();
        strlcpy(current_url, url, sizeof(current_url));
        publish(OPENHAB_SITEMAPS_FAILED);
        return;
    }

    /* A different server: what is in the list belongs to the previous one, and
     * showing its sitemaps under a new host's name is worse than showing none.
     * The same server keeps its list while the refresh is in flight, which is
     * what makes reopening the settings page a page that is already filled in
     * rather than one that empties itself and fills again. */
    if (strcmp(url, current_url) != 0)
        list.clear();

    strlcpy(current_url, url, sizeof(current_url));

    if (openhab_client_request_sitemaps(current_url) == false)
    {
        publish(OPENHAB_SITEMAPS_FAILED);
        return;
    }

    answer_deadline = port_millis() + SITEMAPS_ANSWER_TIMEOUT_MS;
    publish(OPENHAB_SITEMAPS_FETCHING);
}

void openhab_sitemaps_apply(const struct openhab_result_s *res)
{
    /* Applied whatever the state is, including after this fetch was given up
     * on: a late answer is still this server's answer, and the alternative is
     * throwing away a list the user is waiting for because it arrived a second
     * after the deadline. The one thing it must not do is outlive a *newer*
     * request -- and it cannot, because the loop empties the list and goes back
     * to FETCHING before the next one is submitted. */
    if (res->ok == false || res->payload == NULL)
    {
        printf("openhab_sitemaps: no usable list at: %s\r\n", current_url);
        list.clear();
        publish(OPENHAB_SITEMAPS_FAILED);
        return;
    }

    if (list.parse(res->payload, res->payload_len) != 0)
    {
        publish(OPENHAB_SITEMAPS_FAILED);
        return;
    }

#if CONFIG_OHEZ_DEBUG_OPENHAB_CLIENT
    printf("openhab_sitemaps: %u of %u sitemaps from %s\r\n", (unsigned)list.getCount(),
           (unsigned)list.getTotal(), current_url);
#endif

    publish(OPENHAB_SITEMAPS_READY);
}

enum openhab_sitemaps_state_e openhab_sitemaps_state(void)
{
    /* A want that has been recorded but not yet submitted is already a fetch
     * as far as anyone reading this is concerned. It matters for the web form,
     * whose handler records the want and answers in the same breath, a
     * millisecond before the loop task picks it up: without this it would
     * report the previous server's list as ready under the new server's name,
     * and the browser would stop polling and believe it. */
    if (pending == true)
        return OPENHAB_SITEMAPS_FETCHING;

    return state;
}

size_t openhab_sitemaps_count(void)
{
    return list.getCount();
}

size_t openhab_sitemaps_total(void)
{
    return list.getTotal();
}

const char *openhab_sitemaps_name(size_t index)
{
    return list.getName(index);
}

const char *openhab_sitemaps_label(size_t index)
{
    return list.getLabel(index);
}

uint32_t openhab_sitemaps_revision(void)
{
    return revision;
}
