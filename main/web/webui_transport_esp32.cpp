/**
 * @file webui_transport_esp32.cpp
 *
 * The device's HTTP server: esp_http_server.
 *
 * It runs the handlers on a task of its own, which is the one behaviour change
 * this file brings and the reason webui.cpp now takes a lock around Config.
 * Under WebServer::handleClient() a handler ran on the loop task, so it could
 * not race the UI; it can now.
 */

#include "sdkconfig.h"

#if !CONFIG_IDF_TARGET_LINUX

#include "webui_transport.h"

#include <stdlib.h>
#include <string.h>

#include "esp_http_server.h"
#include "esp_log.h"

static const char *TAG = "webui";

/* The settings form is about twenty fields of at most eighty characters, so a
 * kilobyte and a half is the realistic size and four is comfortable headroom.
 * Anything longer is refused rather than truncated: half a form would store
 * half the settings. */
#define WEBUI_BODY_MAX 4096

static httpd_handle_t server;

struct esp32_request_s
{
    httpd_req_t *req;
    char        *body;      /* the POST body, or the query string for a GET */
    bool         chunked;
};

static esp_err_t handle(httpd_req_t *r)
{
    struct esp32_request_s impl = {};
    webui_request_t        req = {};

    impl.req = r;
    req.impl = &impl;

    enum webui_method_e method = (r->method == HTTP_POST) ? WEBUI_POST : WEBUI_GET;

    /* The firmware upload reads its own body: it is a megabyte of multipart,
     * and buffering it to parse it as a form is not possible here. */
    bool stream = webui_transport_route_is_stream(r->uri, method);

    if (method == WEBUI_POST && stream == false)
    {
        size_t len = (size_t)r->content_len;

        if (len > WEBUI_BODY_MAX)
        {
            httpd_resp_send_err(r, HTTPD_400_BAD_REQUEST, "form too large");
            return ESP_OK;
        }

        if (len > 0)
        {
            impl.body = (char *)malloc(len + 1);

            if (impl.body == NULL)
            {
                httpd_resp_send_err(r, HTTPD_500_INTERNAL_SERVER_ERROR, "out of memory");
                return ESP_OK;
            }

            size_t got = 0;

            while (got < len)
            {
                int n = httpd_req_recv(r, impl.body + got, len - got);

                if (n <= 0)
                {
                    free(impl.body);
                    return ESP_FAIL;
                }

                got += (size_t)n;
            }

            impl.body[len] = '\0';
            req.args = impl.body;
            req.args_len = len;
        }
    }
    else
    {
        size_t len = httpd_req_get_url_query_len(r);

        if (len > 0 && len <= WEBUI_BODY_MAX)
        {
            impl.body = (char *)malloc(len + 1);

            if (impl.body != NULL && httpd_req_get_url_query_str(r, impl.body, len + 1) == ESP_OK)
            {
                req.args = impl.body;
                req.args_len = strlen(impl.body);
            }
        }
    }

    webui_transport_dispatch(&req, r->uri, method);

    free(impl.body);

    return ESP_OK;
}

/* The paths are registered with esp_http_server as they are declared, and the
 * dispatch above matches them again against the route table. Two lookups for
 * one request, and worth it: the table is the same on both targets, so a route
 * cannot exist on one and not the other. */
static void register_uri(const char *path, httpd_method_t method)
{
    httpd_uri_t uri = {};

    uri.uri = path;
    uri.method = method;
    uri.handler = handle;

    httpd_register_uri_handler(server, &uri);
}

static uint16_t server_port;

bool webui_transport_start(uint16_t port)
{
    server_port = port;

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

    config.server_port = port;
    /* Room for the routes plus the wildcard below. */
    config.max_uri_handlers = 12;
    /* The firmware upload holds a connection for as long as it takes to send a
     * megabyte over WiFi, and a browser will happily open a second one for the
     * favicon while it does. */
    config.lru_purge_enable = true;
    config.uri_match_fn = httpd_uri_match_wildcard;
    /* The upload reads the body in pieces and must not be timed out between
     * them; the default 5 s is short for a phone on a bad link. */
    config.recv_wait_timeout = 20;
    config.send_wait_timeout = 20;

    if (httpd_start(&server, &config) != ESP_OK)
    {
        ESP_LOGE(TAG, "httpd_start on port %u failed", (unsigned)port);
        return false;
    }

    /* Every path the route table knows, plus a wildcard for everything else --
     * esp_http_server answers 404 itself otherwise, where this project has
     * always redirected to the front page. */
    register_uri("/", HTTP_GET);
    register_uri("/save", HTTP_POST);
    register_uri("/wifi", HTTP_POST);
    register_uri("/restart", HTTP_POST);
    register_uri("/restart", HTTP_GET);
    register_uri("/update", HTTP_GET);
    register_uri("/update", HTTP_POST);
    register_uri("/favicon.ico", HTTP_GET);
    register_uri("/*", HTTP_GET);

    ESP_LOGI(TAG, "listening on port %u", (unsigned)port);

    return true;
}

uint16_t webui_transport_port(void)
{
    return server_port;
}

void webui_transport_loop(void)
{
    /* esp_http_server has its own task. */
}

void webui_begin_chunked(webui_request_t *req, const char *content_type)
{
    struct esp32_request_s *impl = (struct esp32_request_s *)req->impl;

    httpd_resp_set_type(impl->req, content_type);
    impl->chunked = true;
}

void webui_write(webui_request_t *req, const char *data, size_t len)
{
    struct esp32_request_s *impl = (struct esp32_request_s *)req->impl;

    if (len == 0)
        return; /* a zero-length chunk is the terminator */

    httpd_resp_send_chunk(impl->req, data, (ssize_t)len);
}

void webui_end_chunked(webui_request_t *req)
{
    struct esp32_request_s *impl = (struct esp32_request_s *)req->impl;

    httpd_resp_send_chunk(impl->req, NULL, 0);
    impl->chunked = false;
}

void webui_send(webui_request_t *req, int status, const char *content_type, const char *body)
{
    struct esp32_request_s *impl = (struct esp32_request_s *)req->impl;
    char                    status_line[8];

    snprintf(status_line, sizeof(status_line), "%d", status);

    httpd_resp_set_status(impl->req, (status == 200) ? "200 OK" : status_line);
    httpd_resp_set_type(impl->req, content_type);
    httpd_resp_send(impl->req, (body != NULL) ? body : "", HTTPD_RESP_USE_STRLEN);
}

void webui_redirect(webui_request_t *req, int status, const char *location)
{
    struct esp32_request_s *impl = (struct esp32_request_s *)req->impl;

    httpd_resp_set_status(impl->req, (status == 303) ? "303 See Other" : "302 Found");
    httpd_resp_set_hdr(impl->req, "Location", location);
    httpd_resp_send(impl->req, "", 0);
}

size_t webui_body_length(webui_request_t *req)
{
    struct esp32_request_s *impl = (struct esp32_request_s *)req->impl;

    return (size_t)impl->req->content_len;
}

int webui_body_read(webui_request_t *req, char *buf, size_t buf_size)
{
    struct esp32_request_s *impl = (struct esp32_request_s *)req->impl;

    int n = httpd_req_recv(impl->req, buf, buf_size);

    if (n == HTTPD_SOCK_ERR_TIMEOUT)
        return 0;

    return n;
}

#endif /* !CONFIG_IDF_TARGET_LINUX */
