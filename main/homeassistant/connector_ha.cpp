/**
 * @file connector_ha.cpp
 *
 * connector/connector.hpp for Home Assistant -- a placeholder so far.
 *
 * What it gives: a Home Assistant build that compiles, boots, shows the Home
 * Assistant settings and reports itself as "<board>-HA". What it does not
 * give yet: anything on the network. Every page request ends in a failed
 * result, so the tile page shows its usual "access failed" box and retries
 * on its usual interval; it never restarts the panel.
 *
 * The plan for the real connector -- a Lovelace dashboard read over the
 * WebSocket API, states and commands for the entities on it -- is in
 * doc/todo.md.
 *
 * No task and no queue of its own: every call below comes from the UI task,
 * so the few failed results wait in a small ring that connector_poll()
 * drains on the same task.
 */
#include "connector/connector.hpp"

#include "esp_log.h"

#include <stdio.h>
#include <string.h>

static const char *TAG = "connector";

/* A page request and the image screen's request can be outstanding at the
 * same time; nothing else produces a result here. */
#define HA_RESULT_RING 4

static struct connector_result_s ring[HA_RESULT_RING];
static size_t ring_head;
static size_t ring_count;

static uint32_t generation;

static bool ring_push(enum connector_request_e type, uint32_t gen, uint8_t slot)
{
    if (ring_count >= HA_RESULT_RING)
        return false;

    struct connector_result_s *res = &ring[(ring_head + ring_count) % HA_RESULT_RING];

    memset(res, 0, sizeof(*res));
    res->type       = type;
    res->generation = gen;
    res->slot       = slot;
    res->ok         = false;

    ring_count++;

    return true;
}

struct connector_server_s connector_server(const Config *config)
{
    struct connector_server_s server;

    server.host = config->item.homeassistant.hostname;
    server.port = config->item.homeassistant.port;
    server.root = config->item.homeassistant.dashboard;

    return server;
}

bool connector_root_ref(char *out, size_t out_size, const char *website, const char *root)
{
    /* Not a URL the placeholder fetches: it names the dashboard in the log
     * line and the box that report the failed page. */
    int len = snprintf(out, out_size, "%s/lovelace/%s", website,
                       (root[0] != '\0') ? root : "default");

    return (len > 0 && (size_t)len < out_size);
}

const char *connector_name(void)
{
    return "Home Assistant";
}

bool connector_setup(void)
{
    ESP_LOGW(TAG, "the Home Assistant connector is a placeholder; no page will load");

    return true;
}

void connector_link_reset(void)
{
}

void connector_loop(void)
{
}

uint32_t connector_request_page(const char *ref)
{
    (void)ref;

    /* A new generation even though nothing is fetched: the UI stamps the
     * page's requests with it and discards results of older ones. 0 means
     * "not queued" to the caller, so it is skipped. */
    if (++generation == CONNECTOR_GENERATION_ALWAYS)
        ++generation;

    if (ring_push(CONNECTOR_REQ_PAGE, generation, CONNECTOR_SLOT_NONE) == false)
        return 0;

    return generation;
}

bool connector_request_icon(const char *ref, uint8_t slot, uint32_t gen)
{
    (void)ref;
    (void)slot;
    (void)gen;

    return false;
}

bool connector_request_state(const char *ref, uint8_t slot, uint32_t gen)
{
    (void)ref;
    (void)slot;
    (void)gen;

    return false;
}

bool connector_command(const char *ref, const char *body)
{
    (void)ref;
    (void)body;

    return false;
}

bool connector_request_image(const char *ref, uint16_t max_w, uint16_t max_h, uint8_t tag)
{
    (void)ref;
    (void)max_w;
    (void)max_h;

    return ring_push(CONNECTOR_REQ_IMAGE, CONNECTOR_GENERATION_ALWAYS, tag);
}

bool connector_poll(struct connector_result_s *out)
{
    if (ring_count == 0)
        return false;

    *out = ring[ring_head];
    ring_head = (ring_head + 1) % HA_RESULT_RING;
    ring_count--;

    return true;
}

void connector_result_release(struct connector_result_s *res)
{
    /* No result here carries a payload. */
    res->payload     = NULL;
    res->payload_len = 0;
}

int connector_page_parse(Sitemap *sitemap, const char *payload, size_t payload_len,
                         char *scratch, size_t scratch_size)
{
    (void)sitemap;
    (void)payload;
    (void)payload_len;
    (void)scratch;
    (void)scratch_size;

    return -1;
}

void connector_events_subscribe(const struct connector_subscription_s *sub)
{
    (void)sub;
}

bool connector_events_streaming(void)
{
    return false;
}

bool connector_events_take_resync(void)
{
    return false;
}

bool connector_events_take_reload(void)
{
    return false;
}

bool connector_events_poll(struct connector_event_s *out)
{
    (void)out;

    return false;
}

/* The four page model members the active connector defines (see
 * model/page_model.hpp). The placeholder never fills an Item, so there is
 * nothing to address. */

int Sitemap::parse(const char *payload, size_t payload_len, char *scratch, size_t scratch_size)
{
    (void)payload;
    (void)payload_len;
    (void)scratch;
    (void)scratch_size;

    return -1;
}

bool Item::stateUrl(char *out, size_t out_size) const
{
    (void)out;
    (void)out_size;

    return false;
}

bool Item::iconUrl(const char *website, char *out, size_t out_size) const
{
    (void)website;
    (void)out;
    (void)out_size;

    return false;
}

bool Item::name(char *out, size_t out_size) const
{
    (void)out;
    (void)out_size;

    return false;
}
