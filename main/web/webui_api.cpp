/**
 * @file webui_api.cpp
 *
 * The transport half of the REST API -- see webui_api.hpp for the routes and
 * webui_api_json.cpp for everything they mean.
 *
 * These handlers run on the web server's task, not the one that owns LVGL.
 * That constraint is written down at the top of webui.cpp; the two places it
 * bites here are Config, which every access takes the lock for, and playing a
 * sound, which goes through ui_beep_request() -- a recorded request that
 * openhab_ui_loop() carries out, never ui_beep_play() itself.
 */

#include "webui_api.hpp"

#include "webui_api_json.hpp"
#include "webui_transport.h"

#include "config/config_fields.hpp"
#include "port/port_net.h"
#include "port/port_sys.h"
#include "version.h"

#include <stdio.h>
#include <stdlib.h>

static Config *api_config = NULL;

/* Measure, allocate, fill, send: the two passes every webui_api_json producer
 * supports, so a response is never truncated into invalid JSON. */
static void webui_api_send_json(webui_request_t *req,
                                size_t (*produce)(char *out, size_t out_size))
{
    size_t need = produce(NULL, 0);

    char *body = (char *)malloc(need + 1);

    if (body == NULL)
    {
        webui_send(req, 500, "application/json", "{\"error\":\"out of memory\"}");
        return;
    }

    produce(body, need + 1);

    webui_send(req, 200, "application/json", body);

    free(body);
}

/* ------------------------------------------------------------------ status */

/* Filled per request and read before the handler returns; the hostname/ssid/
 * ip/mac pointers refer to the handler's own port_net_info_t, which outlives
 * the send below. Static only so produce_status() can reach them. */
static struct webui_api_status_s status;

static char status_build[32];

static size_t produce_status(char *out, size_t out_size)
{
    return webui_api_status_json(&status, out, out_size);
}

static void webui_api_handle_status(webui_request_t *req)
{
    port_net_info_t net;

    port_net_info(&net);

    snprintf(status.version, sizeof(status.version), "%u.%02u",
             (unsigned)VERSION_MAJOR, (unsigned)VERSION_MINOR);
    status.target = TARGET_NAME;
    snprintf(status_build, sizeof(status_build), "%s %s", __DATE__, __TIME__);
    status.build = status_build;
    status.uptime_s = (unsigned long long)(port_millis() / 1000);
    status.hostname = net.hostname;
    status.ssid = net.ssid;
    status.bssid = net.bssid;
    status.wired = (net.rssi == PORT_NET_RSSI_WIRED);
    status.rssi = (int)net.rssi;
    status.ip = net.ip;
    status.mac = net.mac;
    status.free_heap = (unsigned long)port_free_heap();

    webui_api_send_json(req, produce_status);
}

/* ------------------------------------------------------------------ config */

static size_t produce_config(char *out, size_t out_size)
{
    return webui_api_config_json(&api_config->item, out, out_size);
}

static void webui_api_handle_config_get(webui_request_t *req)
{
    /* The lock is held across the measure and the fill, so the two passes see
     * the same values -- a save landing between them would otherwise size the
     * buffer for one document and serialize another. */
    api_config->lock();

    webui_api_send_json(req, produce_config);

    api_config->unlock();
}

static void webui_api_handle_config_post(webui_request_t *req)
{
    /* The transport has already buffered the body: it is a form-sized POST,
     * not a stream, so req->args is the JSON and WEBUI_BODY_MAX's 4096 is the
     * ceiling -- checked there, with a 400, before this handler runs. */
    if (req->args == NULL || req->args_len == 0)
    {
        webui_send(req, 400, "application/json",
                   "{\"error\":\"expected a JSON object\"}");
        return;
    }

    /* Bounded by the body plus the fixed overhead, as the apply contract
     * promises -- the response repeats names out of the request. */
    size_t out_size = req->args_len + WEBUI_API_APPLY_RESPONSE_OVERHEAD;

    char *body = (char *)malloc(out_size);

    if (body == NULL)
    {
        webui_send(req, 500, "application/json", "{\"error\":\"out of memory\"}");
        return;
    }

    api_config->lock();

    bool applied = webui_api_config_apply(&api_config->item, req->args,
                                          req->args_len, body, out_size);

    if (applied == true)
    {
        /* The same two steps a save from the form takes, and in the same
         * order: persist, then re-apply what does not need a reboot. */
        api_config->saveConfig();
        settings_apply_live(api_config);
    }

    api_config->unlock();

    webui_send(req, (applied == true) ? 200 : 400, "application/json", body);

    free(body);
}

/* ------------------------------------------------------------------- sound */

static size_t produce_sounds(char *out, size_t out_size)
{
    return webui_api_sounds_json(out, out_size);
}

static void webui_api_handle_sounds(webui_request_t *req)
{
    webui_api_send_json(req, produce_sounds);
}

static void webui_api_handle_sound(webui_request_t *req)
{
    if (req->args == NULL || req->args_len == 0)
    {
        webui_send(req, 400, "application/json",
                   "{\"error\":\"expected a JSON object with a name\"}");
        return;
    }

    /* Smaller than the config bound: the 400 lists the vocabulary, which is
     * fixed-size, and the 200 echoes one canonical name. */
    char body[1024];

    enum ui_sound_e sound;
    bool            force;

    bool queued = webui_api_sound_parse(req->args, req->args_len, &sound, &force,
                                        body, sizeof(body));

    if (queued == true)
    {
        /* A recorded request, carried out by openhab_ui_loop() on the task
         * that owns LVGL -- calling ui_beep_play() from here is the thing
         * webui.cpp's header says a handler must not do. */
        ui_beep_request(sound, force);
    }

    webui_send(req, (queued == true) ? 200 : 400, "application/json", body);
}

/* ------------------------------------------------------------------ routes */

void webui_api_setup(Config *config)
{
    api_config = config;

    webui_transport_route("/api/status", WEBUI_GET, webui_api_handle_status);
    webui_transport_route("/api/config", WEBUI_GET, webui_api_handle_config_get);
    webui_transport_route("/api/config", WEBUI_POST, webui_api_handle_config_post);
    webui_transport_route("/api/sound", WEBUI_POST, webui_api_handle_sound);
    webui_transport_route("/api/sounds", WEBUI_GET, webui_api_handle_sounds);
}
