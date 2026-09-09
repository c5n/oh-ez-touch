/**
 * @file webui_transport.cpp
 *
 * The half of the transport that is the same on both targets: the route table
 * and the argument parsing.
 *
 * Percent-decoding a form is not something two servers should each get wrong
 * in their own way, and a route table of six entries does not need to exist
 * twice. What is left in webui_transport_esp32.cpp and
 * webui_transport_linux.cpp is only the sockets.
 */

#include "webui_transport.h"

#include <string.h>

#include "esp_log.h"

static const char *TAG = "webui";

#define WEBUI_ROUTE_MAX 8

static struct
{
    const char         *path;
    enum webui_method_e method;
    webui_route_fn_t    fn;
    bool                stream;
} routes[WEBUI_ROUTE_MAX];

static size_t           route_count;
static webui_route_fn_t route_default;

void webui_transport_route(const char *path, enum webui_method_e method, webui_route_fn_t fn)
{
    if (route_count >= WEBUI_ROUTE_MAX)
    {
        /* A build-time list, so this is a programming error rather than a
         * runtime condition -- but silently dropping a route would present as
         * a page that redirects to the front for no reason. */
        ESP_LOGE(TAG, "route table full, dropping %s", path);
        return;
    }

    routes[route_count].path = path;
    routes[route_count].method = method;
    routes[route_count].fn = fn;
    routes[route_count].stream = false;
    route_count++;
}

void webui_transport_route_stream(const char *path, webui_route_fn_t fn)
{
    webui_transport_route(path, WEBUI_POST, fn);

    if (route_count > 0)
        routes[route_count - 1].stream = true;
}

bool webui_transport_route_is_stream(const char *path, enum webui_method_e method)
{
    for (size_t i = 0; i < route_count; i++)
    {
        if (routes[i].method == method && strcmp(routes[i].path, path) == 0)
            return routes[i].stream;
    }

    return false;
}

void webui_transport_route_default(webui_route_fn_t fn)
{
    route_default = fn;
}

void webui_transport_dispatch(webui_request_t *req, const char *path,
                              enum webui_method_e method)
{
    for (size_t i = 0; i < route_count; i++)
    {
        if (routes[i].method != method)
            continue;

        if (strcmp(routes[i].path, path) != 0)
            continue;

        routes[i].fn(req);
        return;
    }

    if (route_default != NULL)
        route_default(req);
}

static int hex_value(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;

    return -1;
}

/* Copy one percent-encoded value out, decoding as it goes. Truncates rather
 * than failing: every destination in Config::item is a fixed-width field, and
 * an over-long submission has always been cut to fit rather than rejected. */
static void decode_into(const char *src, size_t src_len, char *buf, size_t buf_size)
{
    size_t out = 0;

    for (size_t i = 0; i < src_len && out + 1 < buf_size; i++)
    {
        char c = src[i];

        if (c == '+')
        {
            c = ' ';
        }
        else if (c == '%' && i + 2 < src_len)
        {
            int hi = hex_value(src[i + 1]);
            int lo = hex_value(src[i + 2]);

            /* A malformed escape is passed through as the literal '%' it is,
             * which is what every other decoder does and is harmless here. */
            if (hi >= 0 && lo >= 0)
            {
                c = (char)((hi << 4) | lo);
                i += 2;
            }
        }

        buf[out++] = c;
    }

    buf[out] = '\0';
}

/* Walk `name=value&name=value`, which is both a query string and an
 * application/x-www-form-urlencoded body. The name is compared raw: every name
 * this project uses is [a-z0-9_], so there is nothing to decode on that side. */
static bool find_arg(const webui_request_t *req, const char *name,
                     const char **value, size_t *value_len)
{
    if (req->args == NULL)
        return false;

    size_t      name_len = strlen(name);
    const char *p = req->args;
    const char *end = req->args + req->args_len;

    while (p < end)
    {
        const char *amp = (const char *)memchr(p, '&', (size_t)(end - p));
        const char *pair_end = (amp != NULL) ? amp : end;
        const char *eq = (const char *)memchr(p, '=', (size_t)(pair_end - p));

        const char *key_end = (eq != NULL) ? eq : pair_end;

        if ((size_t)(key_end - p) == name_len && memcmp(p, name, name_len) == 0)
        {
            /* A bare name with no '=' is present with an empty value, which is
             * how some browsers submit a checkbox. */
            *value = (eq != NULL) ? eq + 1 : pair_end;
            *value_len = (size_t)(pair_end - *value);
            return true;
        }

        if (amp == NULL)
            break;

        p = amp + 1;
    }

    return false;
}

bool webui_arg(webui_request_t *req, const char *name, char *buf, size_t buf_size)
{
    const char *value = NULL;
    size_t      value_len = 0;

    if (buf_size == 0)
        return false;

    buf[0] = '\0';

    if (find_arg(req, name, &value, &value_len) == false)
        return false;

    decode_into(value, value_len, buf, buf_size);

    return true;
}

bool webui_has_arg(webui_request_t *req, const char *name)
{
    const char *value = NULL;
    size_t      value_len = 0;

    return find_arg(req, name, &value, &value_len);
}
