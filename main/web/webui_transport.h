/**
 * @file webui_transport.h
 *
 * What the web interface needs from an HTTP server, and nothing else.
 *
 * The valuable half of webui.cpp -- the chunked writer, and the one walk over
 * config_fields[] that renders and parses every setting exactly once -- has
 * never had anything to do with which server delivers it. This is the seam.
 *
 * There are two servers because there have to be. The device runs
 * esp_http_server. The host cannot: esp_http_server's linux port creates its
 * threads with a raw pthread_create(), and such a thread inherits an unblocked
 * signal mask, so the process-directed SIGALRM that drives the FreeRTOS tick
 * gets delivered to a thread with no task control block. vTaskSwitchContext()
 * is then called from outside FreeRTOS and asserts. That is not a theory: it
 * was measured, and it is why Espressif build-test but never run-test that
 * port (the http_server/simple example is disable_test for linux). So the host
 * gets its own POSIX server, which is about two hundred lines and is worth
 * having anyway -- it makes the web interface something that can be developed
 * and clicked through on a desktop.
 */
#ifndef WEBUI_TRANSPORT_H
#define WEBUI_TRANSPORT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * One request, as the handlers see it.
 *
 * `args` is the query string for a GET and the form body for a POST, which is
 * the whole of what the handlers ever ask about; parsing it is shared, so a
 * transport only has to say where it is.
 */
struct webui_request_s
{
    void       *impl;     /* the transport's own handle for this request */
    const char *args;     /* percent-encoded, & separated; not terminated */
    size_t      args_len;
};

typedef struct webui_request_s webui_request_t;

enum webui_method_e
{
    WEBUI_GET,
    WEBUI_POST,
};

typedef void (*webui_route_fn_t)(webui_request_t *req);

/* ------------------------------------------------------------- registration */

/** Register a handler. Paths are matched exactly, and in registration order. */
void webui_transport_route(const char *path, enum webui_method_e method, webui_route_fn_t fn);

/**
 * Register a POST handler whose body the transport must not read for it.
 *
 * The firmware upload is the only one: its body is a megabyte of multipart,
 * and buffering that to parse it as a form would need a megabyte of RAM the
 * device does not have. Such a handler reads the body itself, through
 * webui_body_read().
 */
void webui_transport_route_stream(const char *path, webui_route_fn_t fn);

/** Whether that applies to this request, asked by a transport before it
 * decides whether to buffer the body. */
bool webui_transport_route_is_stream(const char *path, enum webui_method_e method);

/** Where anything unmatched goes. */
void webui_transport_route_default(webui_route_fn_t fn);

/** Bring the server up. */
bool webui_transport_start(uint16_t port);

/**
 * Give the server a turn, from the task that calls it.
 *
 * A no-op where the server has a task of its own, which is both of them today
 * -- kept because a transport that needed pumping would otherwise have nowhere
 * to be pumped from, and because the call site reads the same on both.
 */
void webui_transport_loop(void);

/** Called by a transport once it has a path, a method and an argument string. */
void webui_transport_dispatch(webui_request_t *req, const char *path,
                              enum webui_method_e method);

/* ------------------------------------------------------------------ request */

/**
 * Copy the value of `name` into `buf`, percent-decoded.
 *
 * @return false when the argument is absent, in which case `buf` is emptied.
 *   That distinction is load-bearing for checkboxes: an unchecked box is simply
 *   not submitted, so absence is what "off" looks like.
 */
bool webui_arg(webui_request_t *req, const char *name, char *buf, size_t buf_size);

/** Whether `name` was submitted at all, value or not. */
bool webui_has_arg(webui_request_t *req, const char *name);

/* ----------------------------------------------------------------- response */

/** Begin a chunked response. The length of a page is not known until it has
 * been written, which is the point of not assembling it anywhere. */
void webui_begin_chunked(webui_request_t *req, const char *content_type);

/** One chunk. */
void webui_write(webui_request_t *req, const char *data, size_t len);

/** Finish it. */
void webui_end_chunked(webui_request_t *req);

/** A complete response in one call. `body` may be NULL for an empty one. */
void webui_send(webui_request_t *req, int status, const char *content_type,
                const char *body);

/** A redirect: 302 or 303, with a Location. */
void webui_redirect(webui_request_t *req, int status, const char *location);

/* --------------------------------------------------------------- raw bodies */

/**
 * Total length of the request body, for a handler that streams it rather than
 * having it parsed as a form -- which is the firmware upload and nothing else.
 */
size_t webui_body_length(webui_request_t *req);

/**
 * Read the next piece of the body.
 *
 * @return bytes read, 0 at the end, or -1 on a broken connection.
 */
int webui_body_read(webui_request_t *req, char *buf, size_t buf_size);

#endif /* WEBUI_TRANSPORT_H */
