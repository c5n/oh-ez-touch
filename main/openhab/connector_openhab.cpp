/**
 * @file connector_openhab.cpp
 *
 * connector/connector.hpp for openHAB: a forwarder to the client task
 * (openhab_client.cpp), the event stream (openhab_events.cpp), the HTTP
 * session (openhab_http.cpp), the settings' sitemap list and server scan
 * (openhab_sitemaps.cpp, openhab_discover.cpp) and the sitemap parser
 * (openhab_connector.cpp). It adds no behaviour of its own: an openHAB build
 * does exactly what it did before the connector boundary existed.
 */
#include "connector/connector.hpp"

#include "openhab_client.hpp"
#include "openhab_connector.hpp"
#include "openhab_discover.hpp"
#include "openhab_events.hpp"
#include "openhab_http.hpp"
#include "openhab_sitemaps.hpp"

#include "esp_log.h"

#include <stdio.h>

static const char *TAG = "connector";

struct connector_server_s connector_server(const Config *config)
{
    struct connector_server_s server;

    server.host = config->item.openhab.hostname;
    server.port = config->item.openhab.port;
    server.root = config->item.openhab.sitemap;

    return server;
}

bool connector_root_ref(char *out, size_t out_size, const char *website, const char *root)
{
    int len = snprintf(out, out_size, "%s/rest/sitemaps/%s/%s?type=json", website, root, root);

    return (len > 0 && (size_t)len < out_size);
}

const char *connector_name(void)
{
    return "openHAB";
}

void connector_configure(const Config *config)
{
    /* Nothing to copy: the openHAB client is handed its URLs by the UI, and
     * the UI reads the doorbell settings itself. */
    (void)config;
}

bool connector_setup(void)
{
    bool ok = true;

    if (openhab_client_setup() == false)
    {
        ESP_LOGE(TAG, "no openHAB client task; the panel will not reach openHAB");
        ok = false;
    }

    /* Idle until the UI has a page to listen for. Without it the panel polls,
     * as it always did. */
    if (openhab_events_setup() == false)
    {
        ESP_LOGE(TAG, "no openHAB event task; item states are polled only");
        ok = false;
    }

    return ok;
}

void connector_link_reset(void)
{
    openhab_http_reset();
    openhab_events_reset();
}

void connector_loop(void)
{
    /* The list of sitemaps is fetched because somebody opened the openHAB
     * settings, and on a panel that cannot reach its server that has to end
     * in a failure the screen can report rather than in a request nothing
     * ever times out. The scan that finds the server runs for the same
     * reason: the settings screen is reachable without a network worth the
     * name. */
    openhab_sitemaps_loop();
    openhab_discover_loop();
}

uint32_t connector_request_page(const char *ref)
{
    return openhab_client_request_page(ref);
}

bool connector_request_icon(const char *ref, uint8_t slot, uint32_t generation)
{
    return openhab_client_request_icon(ref, slot, generation);
}

bool connector_request_state(const char *ref, uint8_t slot, uint32_t generation)
{
    return openhab_client_request_state(ref, slot, generation);
}

bool connector_command(const char *ref, const char *body)
{
    return openhab_client_command(ref, body);
}

bool connector_request_image(const char *ref, uint16_t max_w, uint16_t max_h, uint8_t tag)
{
    return openhab_client_request_image(ref, max_w, max_h, tag);
}

bool connector_poll(struct connector_result_s *out)
{
    return openhab_client_poll(out);
}

void connector_result_release(struct connector_result_s *res)
{
    openhab_client_result_release(res);
}

int connector_page_parse(Sitemap *sitemap, const char *payload, size_t payload_len,
                         char *scratch, size_t scratch_size)
{
    return sitemap->parse(payload, payload_len, scratch, scratch_size);
}

void connector_events_subscribe(const struct connector_subscription_s *sub)
{
    openhab_events_subscribe(sub);
}

bool connector_events_streaming(void)
{
    return openhab_events_streaming();
}

bool connector_events_take_resync(void)
{
    return openhab_events_take_resync();
}

bool connector_events_take_reload(void)
{
    return openhab_events_take_reload();
}

bool connector_events_poll(struct connector_event_s *out)
{
    return openhab_events_poll(out);
}
