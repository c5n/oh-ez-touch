#ifndef WEBUI_OUT_H
#define WEBUI_OUT_H

/**
 * @file webui_out.h
 *
 * The chunked page writer, shared by the handlers.
 *
 * This used to be static inside webui.cpp, where every page is streamed
 * through one fixed buffer and never assembled anywhere -- the comment there
 * says what that saves against PageBuilder's repeated reallocs. webui_api.cpp
 * streams its JSON through the same machinery now, which is what pulled it
 * into a header: a second writer would be the drift config_fields[] exists to
 * prevent, in miniature.
 *
 * Everything is static inline: two translation units include this, and each
 * keeps its own copy, which is exactly what the static functions in webui.cpp
 * already were.
 */

#include "webui_transport.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* The buffer lives on the handler's stack rather than in .bss, so it costs
 * nothing while nobody is browsing. 512 bytes against the 8 KB httpd task is
 * affordable; the handlers add little else. */
struct webui_out_s
{
    webui_request_t *req;
    char             buf[512];
    size_t           len;
};

#define WEBUI_OUT_FLUSH_AT (sizeof(((struct webui_out_s *)0)->buf) - 128)

static inline void webui_flush(struct webui_out_s *o)
{
    /* Not merely an optimisation: a zero-length write is the terminating
     * chunk, so flushing an empty buffer would end the response in the middle
     * of the page. */
    if (o->len == 0)
        return;

    webui_write(o->req, o->buf, o->len);
    o->len = 0;
}

static inline void webui_put(struct webui_out_s *o, const char *s)
{
    size_t len = strlen(s);

    while (len > 0)
    {
        size_t room = sizeof(o->buf) - o->len;

        if (room == 0)
        {
            webui_flush(o);
            room = sizeof(o->buf);
        }

        size_t take = (len < room) ? len : room;

        memcpy(o->buf + o->len, s, take);
        o->len += take;
        s += take;
        len -= take;
    }

    if (o->len >= WEBUI_OUT_FLUSH_AT)
        webui_flush(o);
}

/* HTML-escape everything that came out of Config or off the network. Labels
 * and argument names are compile-time constants we wrote, and go through
 * webui_put() unescaped. */
static inline void webui_put_escaped(struct webui_out_s *o, const char *s)
{
    for (; *s != '\0'; s++)
    {
        switch (*s)
        {
        case '&':
            webui_put(o, "&amp;");
            break;
        case '<':
            webui_put(o, "&lt;");
            break;
        case '>':
            webui_put(o, "&gt;");
            break;
        case '"':
            webui_put(o, "&quot;");
            break;
        case '\'':
            webui_put(o, "&#39;");
            break;
        default:
        {
            char one[2] = {*s, '\0'};
            webui_put(o, one);
            break;
        }
        }
    }
}

static inline void webui_putf(struct webui_out_s *o, const char *fmt, ...)
{
    char    tmp[192];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);

    webui_put(o, tmp);
}

/* The same job for a JSON string body: only what a string may not contain
 * literally. Everything that goes through this came off the network -- the
 * sitemap names and labels a server chose -- or out of Config, and lands in a
 * document a browser runs. */
static inline void webui_put_json_escaped(struct webui_out_s *o, const char *s)
{
    for (; *s != '\0'; s++)
    {
        unsigned char c = (unsigned char)*s;

        if (c == '"' || c == '\\')
        {
            char pair[3] = {'\\', (char)c, '\0'};
            webui_put(o, pair);
        }
        else if (c < 0x20)
        {
            /* Control characters, which a string may not carry raw. openHAB
             * will not send one, and a body that did would otherwise leave
             * the page's JSON unparseable. */
            webui_putf(o, "\\u%04x", (unsigned)c);
        }
        else
        {
            char one[2] = {(char)c, '\0'};
            webui_put(o, one);
        }
    }
}

#endif /* WEBUI_OUT_H */
