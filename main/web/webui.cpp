/**
 * The configuration web interface.
 *
 * This replaces AutoConnect's aux pages. The library cost about 197 KB of
 * flash and 6.8 KB of static RAM to render a form of twenty settings, most of
 * it in the AutoConnectCore<> template instantiation and in the object tree
 * PageBuilder assembled per request.
 *
 * Two things keep this small. Every page is streamed through one fixed buffer
 * and is never assembled anywhere -- PageBuilder grew each page as a single
 * repeatedly-realloc'ed String, with a transient heap peak of some 8 to 12 KB.
 * And every setting is described exactly once, in config_fields[], which one
 * loop renders and another parses; the AutoConnect version had the field list
 * written out three times, as the GET prefill, the POST parse and the echo
 * page, and they had drifted apart.
 *
 * Note for anyone extending this: the handlers run on the *server's* task, not
 * on the one that owns LVGL, and they must not touch it. That used to be a
 * convention -- WebServer::handleClient() called them from the loop task, so
 * they could not race anything -- and it is now enforced by the two things
 * openhab_ui.hpp is included for: openhab_ui_request_theme() and
 * openhab_ui_request_connect(), both of which only record a request that
 * openhab_ui_loop() carries out. Writes to Config go under config->lock().
 *
 * Which server delivers this is webui_transport.h's business. There are two,
 * and the reason there have to be is written down there.
 */

#include "webui.hpp"

#include "config/config_fields.hpp"

#include "openhab/openhab_discover.hpp"
#include "openhab/openhab_sitemaps.hpp"
#include "ui/openhab_ui.hpp"
#include "ui/ui_frame_stats.h"
#include "port/port_net.h"
#include "port/port_sys.h"
#include "webui_ota.hpp"
#include "webui_transport.h"
#include "net/wlan.hpp"
#include "ui/ui_theme.hpp"
#include "version.h"
#include "debug.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* Widest destination in Config::item is the char[64] MQTT password; the rest is
 * headroom so that an over-long submission is truncated here rather than
 * rejected. */
#define WEBUI_VALUE_MAX 80

/* No HTTP authentication, which is what AutoConnect was configured for too
 * (auth = AC_AUTH_NONE, and the OTA endpoint never had any). Worth knowing
 * before exposing one of these outside a home network. */
#ifndef WEBUI_PORT
#define WEBUI_PORT 80
#endif

static Config *webui_config = NULL;

/* ------------------------------------------------------------------ fields */

/* The row macros, the field table and the by-offset accessors live in
 * config_fields.hpp: the touch settings screen walks the same rows, and a
 * second copy of the list here is exactly the drift the table was introduced
 * to stop. What stays below is the HTML rendering and the POST parsing.
 *
 * The bound on the table's size lives there too, as SETTINGS_MAX_FIELDS,
 * where the row count is a constant expression. */

/* ------------------------------------------------------------------ writer */

/* sendContent() mallocs a chunk header and writes three times per call, so
 * emitting a field at a time would mean a hundred mallocs and three hundred
 * packets per page. Everything goes through one buffer that flushes when it
 * fills, which brings the whole form down to about a dozen calls.
 *
 * The buffer lives on the handler's stack rather than in .bss, so it costs
 * nothing while nobody is browsing. 512 bytes against the 8 KB Arduino loop
 * task is affordable; the handlers below add little else. */
struct webui_out_s
{
    webui_request_t *req;
    char             buf[512];
    size_t           len;
};

#define WEBUI_OUT_FLUSH_AT (sizeof(((struct webui_out_s *)0)->buf) - 128)

static void webui_flush(struct webui_out_s *o)
{
    /* Not merely an optimisation: a zero-length sendContent() is the
     * terminating chunk, so flushing an empty buffer would end the response
     * in the middle of the page. */
    if (o->len == 0)
        return;

    webui_write(o->req, o->buf, o->len);
    o->len = 0;
}

static void webui_put(struct webui_out_s *o, const char *s)
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
static void webui_put_escaped(struct webui_out_s *o, const char *s)
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

static void webui_putf(struct webui_out_s *o, const char *fmt, ...)
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
static void webui_put_json_escaped(struct webui_out_s *o, const char *s)
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

/* -------------------------------------------------------------------- page */

static const char webui_page_head[] =
    "<!DOCTYPE html><html><head><meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>OhEzTouch</title><style>"
    "body{font-family:sans-serif;margin:0;padding:12px;background:#eee;color:#222}"
    "div.w{max-width:380px;margin:0 auto}"
    "h1{font-size:1.3em;margin:0 0 8px}"
    "fieldset{border:1px solid #bbb;border-radius:4px;margin:0 0 12px;padding:8px 10px;background:#fff}"
    "legend{font-weight:bold;padding:0 4px}"
    "label{display:block;margin:6px 0;font-size:.9em}"
    "input[type=text],input[type=number],input[type=password],select{"
    "width:100%;box-sizing:border-box;padding:6px;margin-top:2px;"
    "border:1px solid #bbb;border-radius:3px;font-size:1em}"
    "input[type=checkbox]{margin-right:6px}"
    "button{width:100%;padding:10px;margin:4px 0;font-size:1em;border:0;"
    "border-radius:3px;background:#1fa3ec;color:#fff}"
    /* The found servers, which are a row of choices rather than the page's
     * verbs and must not look like Save does. */
    "button.s{width:auto;padding:5px 9px;margin:3px 5px 3px 0;font-size:.85em;"
    "background:#5a6570}"
    "table.s{width:100%;font-size:.82em;border-collapse:collapse}"
    "table.s td{padding:1px 0}table.s td:first-child{color:#666;width:38%}"
    "p.n,span.n{font-size:.8em;color:#666}"
    "</style></head><body><div class='w'><h1>OhEzTouch</h1>";

/* Fills the sitemap field's datalist from /sitemaps, and says where it got to.
 *
 * The one piece of script in this interface, and it earns its place: the list
 * comes from a server the *panel* talks to, so it is not known when the page is
 * rendered -- the fetch has usually only just been started by the handler that
 * rendered it. The alternative is a page that shows whatever the last visit
 * left in the cache, which is precisely the stale answer the list exists to
 * replace.
 *
 * Nothing here is required for the form to work: the field is a text input with
 * a datalist, so a browser with no script, or a device that cannot reach its
 * openHAB, leaves a field that can still be typed into and saved.
 *
 * Options are built through the DOM and never through innerHTML. The names and
 * labels in that JSON came off another machine. */
static const char webui_page_script[] =
    "<script>(function(){var n=0;var p=function(){"
    "fetch('/sitemaps').then(function(r){return r.json()}).then(function(d){"
    "var l=document.getElementById('sitemaps'),s=document.getElementById('sitemaps-state');"
    "if(!l||!s)return;"
    "l.innerHTML='';"
    "d.sitemaps.forEach(function(m){var o=document.createElement('option');"
    "o.value=m.name;o.label=m.label;l.appendChild(o)});"
    "if(d.state=='fetching'){s.textContent='Asking '+d.host+' for its sitemaps...';"
    /* Fifteen seconds of polling at most: the panel gives its own fetch about
     * twelve before it calls it failed, so a page that is still being told
     * "fetching" after this has lost the answer rather than be waiting for
     * it. */
    "if(++n<15)setTimeout(p,1000);return}"
    "if(d.state=='ready'){s.textContent=d.count?(d.count+' sitemap'+(d.count==1?'':'s')+"
    "' on '+d.host+(d.total>d.count?' (of '+d.total+' -- the rest can be typed)':'')):"
    "(d.host+' serves no sitemaps');return}"
    "s.textContent='No sitemap list from '+d.host"
    "}).catch(function(){var s=document.getElementById('sitemaps-state');"
    "if(s)s.textContent=''})};p()})();</script>";

/* And the same for the servers openHAB announced. Clicking one fills the Host
 * and Port fields rather than saving anything: what the panel found is a
 * suggestion, and the form is still submitted by the one Save button at the
 * bottom.
 *
 * The fields are reached by their POST argument name, which is the same name
 * config_fields[] gives them and the same one the form renders -- there is no
 * second spelling of it here. */
static const char webui_servers_script[] =
    "<script>(function(){var n=0;var p=function(){"
    "fetch('/servers').then(function(r){return r.json()}).then(function(d){"
    "var l=document.getElementById('servers'),s=document.getElementById('servers-state');"
    "if(!l||!s)return;"
    "l.innerHTML='';"
    "d.servers.forEach(function(m){var b=document.createElement('button');"
    "b.type='button';b.className='s';b.textContent=m.label+'  '+m.host+':'+m.port;"
    "b.onclick=function(){var h=document.querySelector('[name=oh_host]'),"
    "o=document.querySelector('[name=oh_port]');"
    "if(h)h.value=m.host;if(o)o.value=m.port};l.appendChild(b)});"
    "if(d.state=='scanning'){s.textContent='Looking for openHAB servers...';"
    /* The panel's own window is two and a half seconds; eight tries is ample
     * for the answer and short enough that a page left open is not polling
     * this for ever. */
    "if(++n<8)setTimeout(p,1000);return}"
    "if(d.state=='ready'){s.textContent=d.count?"
    "'Found on this network -- pick one to fill in the host and port:':"
    "'No openHAB announced itself on this network';return}"
    "s.textContent=d.state=='failed'?'Cannot look for servers':''"
    "}).catch(function(){var s=document.getElementById('servers-state');"
    "if(s)s.textContent=''})};p()})();</script>";

static const char webui_page_tail[] = "</div></body></html>";

static void webui_send_status(struct webui_out_s *o)
{
    webui_put(o, "<fieldset><legend>Status</legend><table class='s'>");

    webui_putf(o, "<tr><td>Version</td><td>%u.%02u</td></tr>", VERSION_MAJOR, VERSION_MINOR);
    webui_putf(o, "<tr><td>Build</td><td>%s %s</td></tr>", __DATE__, __TIME__);

    unsigned long long up = (unsigned long long)(port_millis() / 1000);

    webui_putf(o, "<tr><td>Uptime</td><td>%llu days, %lluh %llum %llus</td></tr>",
               up / 86400, (up / 3600) % 24, (up / 60) % 60, up % 60);

    port_net_info_t net;

    port_net_info(&net);

    webui_put(o, "<tr><td>Hostname</td><td>");
    webui_put_escaped(o, net.hostname);
    webui_put(o, "</td></tr>");

    webui_put(o, "<tr><td>SSID</td><td>");
    webui_put_escaped(o, net.ssid);
    webui_put(o, "</td></tr>");

    if (net.rssi == PORT_NET_RSSI_WIRED)
        webui_put(o, "<tr><td>RSSI</td><td>wired</td></tr>");
    else
        webui_putf(o, "<tr><td>RSSI</td><td>%i dBm</td></tr>", (int)net.rssi);

    webui_putf(o, "<tr><td>IP</td><td>%s</td></tr>", net.ip);
    webui_putf(o, "<tr><td>MAC</td><td>%s</td></tr>", net.mac);
    webui_putf(o, "<tr><td>Free heap</td><td>%u bytes</td></tr>", (unsigned)port_free_heap());

    /* The frame, in the three numbers that decide what to do about it: how fast
     * the screen is going, how much of a frame the software renderer took, and
     * how much of it was spent waiting for the panel to accept the last strip.
     * Which of the last two is larger is the whole question -- see the README's
     * "Where the frame time goes". One row saying so replaces all of them until
     * a window has closed, because a panel that has not drawn yet has no frame
     * rate and 0.0 fps would read like one. */
    ui_frame_stats_t frame;

    ui_frame_stats_get((uint32_t)port_millis(), &frame);

    if (frame.valid == false)
    {
        webui_put(o, "<tr><td>Frame</td><td>not measured yet</td></tr>");
    }
    else
    {
        webui_putf(o, "<tr><td>Frame rate</td><td>%u.%u fps (%u frames over %u ms, "
                      "%u ms ago)</td></tr>",
                   (unsigned)(frame.fps_x10 / 10), (unsigned)(frame.fps_x10 % 10),
                   (unsigned)frame.frames, (unsigned)frame.window_ms,
                   (unsigned)frame.age_ms);

        webui_putf(o, "<tr><td>Frame time</td><td>%u us mean, %u us worst</td></tr>",
                   (unsigned)frame.frame_us, (unsigned)frame.frame_us_max);

        webui_putf(o, "<tr><td>Renderer</td><td>%u us</td></tr>",
                   (unsigned)frame.render_us);

        webui_putf(o, "<tr><td>Waiting for panel</td><td>%u us</td></tr>",
                   (unsigned)frame.wait_us);

        webui_putf(o, "<tr><td>Flushed</td><td>%u px in %u.%u strips</td></tr>",
                   (unsigned)frame.pixels,
                   (unsigned)(frame.flushes_x10 / 10), (unsigned)(frame.flushes_x10 % 10));
    }

    if (wlan_ap_ssid() != NULL)
    {
        uint32_t ip = wlan_ap_ip();

        webui_put(o, "<tr><td>Setup AP</td><td>");
        webui_put_escaped(o, wlan_ap_ssid());
        webui_putf(o, " (%u.%u.%u.%u)</td></tr>",
                   (unsigned)(ip & 0xFF), (unsigned)((ip >> 8) & 0xFF),
                   (unsigned)((ip >> 16) & 0xFF), (unsigned)((ip >> 24) & 0xFF));
    }

    webui_put(o, "</table></fieldset>");
}

static void webui_send_form(struct webui_out_s *o, const Config *config)
{
    bool open = false;

    webui_put(o, "<form method='post' action='/save'>");

    for (size_t i = 0; i < config_field_count; i++)
    {
        const struct config_field_s *f = &config_fields[i];

        if (f->kind == SETTINGS_SECTION)
        {
            if (open == true)
                webui_put(o, "</fieldset>");

            webui_putf(o, "<fieldset><legend>%s</legend>", f->label);
            open = true;
            continue;
        }

        webui_putf(o, "<label>%s%s%s", f->label,
                   (f->flags & SETTINGS_F_RESTART) ? " *" : "",
                   (f->kind == SETTINGS_BOOL) ? " " : "<br>");

        switch (f->kind)
        {
        case SETTINGS_TEXT:
            /* A SETTINGS_F_SECRET row is a password input, and is still
             * prefilled: saving this form must not require retyping the broker
             * password, and the page it appears in has no authentication in
             * front of it anyway. See the flag's own comment. */
            webui_putf(o, "<input type='%s' name='%s' maxlength='%u'%s value='",
                       (f->flags & SETTINGS_F_SECRET) ? "password" : "text", f->name,
                       (unsigned)(f->size - 1),
                       /* The one row with a list behind it. A datalist and not
                        * a <select>, so that the field stays what it is: the
                        * sitemaps the server offers are suggestions, and a
                        * name can still be typed when the server is not up or
                        * has more sitemaps than the panel keeps. */
                       (strcmp(f->name, SETTINGS_FIELD_SITEMAP) == 0)
                           ? " list='sitemaps' autocomplete='off'" : "");
            webui_put_escaped(o, config_field_text(f, &config->item));
            webui_put(o, "'>");

            if (strcmp(f->name, SETTINGS_FIELD_SITEMAP) == 0)
                webui_put(o, "<datalist id='sitemaps'></datalist>"
                             "<span class='n' id='sitemaps-state'>Loading sitemaps...</span>");
            /* Under the Host row, because that is the row it fills in -- and
             * the Port row with it, which is why this is a list of buttons
             * rather than a datalist on the input. */
            else if (strcmp(f->name, SETTINGS_FIELD_HOST) == 0)
                webui_put(o, "<span class='n' id='servers-state'>"
                             "Looking for openHAB servers...</span><div id='servers'></div>");
            break;

        case SETTINGS_BOOL:
            /* No <br> for this one: a checkbox belongs on the same line as
             * its text, where every other kind wants its input underneath. */
            webui_putf(o, "<input type='checkbox' name='%s'%s>", f->name,
                       config_field_read(f, &config->item) ? " checked" : "");
            break;

        case SETTINGS_ENUM:
        {
            int32_t selected = config_field_read(f, &config->item);

            webui_putf(o, "<select name='%s'>", f->name);

            for (uint8_t n = 0; n < f->count; n++)
                webui_putf(o, "<option%s>%s</option>",
                           (n == selected) ? " selected" : "", f->names[n]);

            webui_put(o, "</select>");
            break;
        }

        default:
            webui_putf(o, "<input type='number' name='%s' min='%ld' max='%ld' value='%ld'>",
                       f->name, (long)f->min, (long)f->max,
                       (long)config_field_read(f, &config->item));
            break;
        }

        webui_put(o, "</label>");
    }

    if (open == true)
        webui_put(o, "</fieldset>");

    webui_put(o, "<p class='n'>* takes effect after a restart.</p>"
                 "<button type='submit'>Save</button></form>");
}

/* A form of its own, and not only for tidiness: provisioning must not depend on
 * a valid settings round-trip, and a device that has just come up on its setup
 * access point has nothing else to offer. The passphrase is never sent back to
 * the browser, and an empty one is submitted as an open network. */
static void webui_send_wlan_form(struct webui_out_s *o)
{
    webui_put(o, "<form method='post' action='/wifi'>"
                 "<fieldset><legend>WLAN</legend>"
                 "<label>Network (SSID)<br>"
                 "<input type='text' name='ssid' maxlength='32' value='");
    webui_put_escaped(o, wlan_sta_ssid());
    webui_putf(o, "'></label><label>Password<br>"
                  "<input type='password' name='psk' maxlength='%u'></label>",
               (unsigned)(WLAN_PSK_SIZE - 1));
    webui_put(o, "<p class='n'>The device reconnects immediately; the setup "
                 "access point closes a few seconds later.</p>"
                 "<button type='submit'>Connect</button>"
                 "</fieldset></form>");
}

static void webui_begin_page(struct webui_out_s *o, webui_request_t *req)
{
    o->req = req;
    o->len = 0;

    /* Chunked, because the length of the page is not known until it has been
     * written -- which is the point of not assembling it. */
    webui_begin_chunked(req, "text/html");
    webui_put(o, webui_page_head);
}

static void webui_end_page(struct webui_out_s *o)
{
    webui_put(o, webui_page_tail);
    webui_flush(o);
    webui_end_chunked(o->req);
}

static void webui_handle_root(webui_request_t *req)
{
    struct webui_out_s out;

    webui_begin_page(&out, req);

    if (webui_has_arg(req, "saved") == true)
        webui_put(&out, "<fieldset><legend>Saved</legend>"
                        "<p class='n'>Settings stored.</p></fieldset>");

    if (webui_has_arg(req, "wifi") == true)
        webui_put(&out, "<fieldset><legend>WLAN</legend>"
                        "<p class='n'>Credentials stored, connecting.</p></fieldset>");

    /* Opening the page is the request, the same way opening the panel's
     * openHAB settings is: by the time the scripts below ask for the two
     * lists, the panel is usually already waiting on the answers. */
    openhab_sitemaps_request(webui_config->item.openhab.hostname,
                             (uint16_t)webui_config->item.openhab.port);
    openhab_discover_request();

    webui_send_status(&out);
    webui_send_wlan_form(&out);
    webui_send_form(&out, webui_config);

    webui_put(&out, "<form method='get' action='/update'>"
                    "<button type='submit'>Firmware update</button></form>"
                    "<form method='post' action='/restart'>"
                    "<button type='submit'>Restart</button></form>");

    webui_put(&out, webui_page_script);
    webui_put(&out, webui_servers_script);

    webui_end_page(&out);
}

static void webui_handle_save(webui_request_t *req)
{
    Config *config = webui_config;

    /* Every write to item is under this, and so is settings_apply_live() at the
     * end -- it reads back what was just written, and the two together have to
     * look atomic to a save from the panel. */
    config->lock();

    for (size_t i = 0; i < config_field_count; i++)
    {
        const struct config_field_s *f = &config_fields[i];
        char                           value[WEBUI_VALUE_MAX];

        if (f->kind == SETTINGS_SECTION)
            continue;

        /* An unchecked box is simply absent from the body, so a checkbox is
         * the one kind whose absence carries meaning. For every other kind an
         * absent argument leaves the stored value alone, which is what makes a
         * partial POST -- an older firmware's bookmarked form, say -- harmless
         * rather than destructive. */
        if (f->kind == SETTINGS_BOOL)
        {
            config_field_write(f, &config->item, webui_has_arg(req, f->name) ? 1 : 0);
            continue;
        }

        if (webui_has_arg(req, f->name) == false)
            continue;

        webui_arg(req, f->name, value, sizeof(value));

        /* The character check, the clamp and the option lookup all live in
         * config_fields.cpp, so the touch screen applies the same rules to
         * the same rows -- a value the web form rejects is not one the panel
         * can smuggle in. */
        switch (f->kind)
        {
        case SETTINGS_TEXT:
            config_field_set_text(f, &config->item, value);
            break;

        case SETTINGS_ENUM:
            config_field_write(f, &config->item, config_field_enum_from_name(f, value));
            break;

        default:
            config_field_set_number(f, &config->item, strtol(value, NULL, 10));
            break;
        }
    }

    config->saveConfig();

#if CONFIG_OHEZ_DEBUG_WEBUI
    printf("webui: settings saved\r\n");
#endif

    /* Re-apply everything that does not need a reboot -- the theme, the openHAB
     * endpoint, the backlight timings, the beeper. Shared with the touch
     * settings screen, and the reason only two rows still carry
     * SETTINGS_F_RESTART. The theme part of it is a request rather than a
     * repaint: the repaint happens in openhab_ui_loop(), on the task that owns
     * LVGL, which this handler is not. */
    settings_apply_live(config);

    /* 303 rather than a page of its own: the browser re-GETs /, which renders
     * the values actually stored. That is what AutoConnect's echo page was
     * for, minus a third copy of the field list, and it also means a reload
     * does not re-post the form. */
    config->unlock();

    webui_redirect(req, 303, "/?saved=1");
}

/* The list of sitemaps, as JSON, for the script above.
 *
 * Reading, not refreshing. The page handler is what asks for a fresh list, and
 * this is polled once a second until that answer arrives -- a request here
 * would restart the fetch on every poll and the answer would never be reported
 * as finished. The one exception is a list that has never been fetched at all,
 * which is what a bookmarked /sitemaps or a script asking what the panel can
 * see would otherwise get nothing from.
 *
 * "host" is echoed back because every line the script writes names it, and
 * because the value the panel asked can differ from the value in the form: the
 * fetch goes to the *saved* endpoint, and the field above it may have been
 * edited since.
 */
static void webui_handle_sitemaps(webui_request_t *req)
{
    struct webui_out_s out;

    if (openhab_sitemaps_state() == OPENHAB_SITEMAPS_IDLE)
        openhab_sitemaps_request(webui_config->item.openhab.hostname,
                                 (uint16_t)webui_config->item.openhab.port);

    const char *state = "idle";

    switch (openhab_sitemaps_state())
    {
    case OPENHAB_SITEMAPS_FETCHING:
        state = "fetching";
        break;
    case OPENHAB_SITEMAPS_READY:
        state = "ready";
        break;
    case OPENHAB_SITEMAPS_FAILED:
        state = "failed";
        break;
    case OPENHAB_SITEMAPS_IDLE:
    default:
        /* The request above has only been recorded, not carried out: the loop
         * task picks it up within milliseconds and the script asks again. */
        break;
    }

    out.req = req;
    out.len = 0;

    webui_begin_chunked(req, "application/json");

    webui_putf(&out, "{\"state\":\"%s\",\"host\":\"", state);
    webui_put_json_escaped(&out, webui_config->item.openhab.hostname);
    webui_putf(&out, ":%d\",\"count\":%u,\"total\":%u,\"sitemaps\":[",
               webui_config->item.openhab.port, (unsigned)openhab_sitemaps_count(),
               (unsigned)openhab_sitemaps_total());

    for (size_t i = 0; i < openhab_sitemaps_count(); i++)
    {
        webui_put(&out, (i == 0) ? "{\"name\":\"" : ",{\"name\":\"");
        webui_put_json_escaped(&out, openhab_sitemaps_name(i));
        webui_put(&out, "\",\"label\":\"");
        webui_put_json_escaped(&out, openhab_sitemaps_label(i));
        webui_put(&out, "\"}");
    }

    webui_put(&out, "]}");
    webui_flush(&out);
    webui_end_chunked(req);
}

/* The openHAB servers that answered an mDNS query, as JSON, for the script
 * above. A read like /sitemaps is, and for the same reason -- a scan started on
 * every poll would never be reported as finished -- except when nothing has
 * ever been looked for.
 */
static void webui_handle_servers(webui_request_t *req)
{
    struct webui_out_s out;

    if (openhab_discover_state() == OPENHAB_DISCOVER_IDLE)
        openhab_discover_request();

    const char *state = "idle";

    switch (openhab_discover_state())
    {
    case OPENHAB_DISCOVER_SCANNING:
        state = "scanning";
        break;
    case OPENHAB_DISCOVER_READY:
        state = "ready";
        break;
    case OPENHAB_DISCOVER_FAILED:
        state = "failed";
        break;
    case OPENHAB_DISCOVER_IDLE:
    default:
        break;
    }

    out.req = req;
    out.len = 0;

    webui_begin_chunked(req, "application/json");

    webui_putf(&out, "{\"state\":\"%s\",\"count\":%u,\"servers\":[", state,
               (unsigned)openhab_discover_count());

    for (size_t i = 0; i < openhab_discover_count(); i++)
    {
        webui_put(&out, (i == 0) ? "{\"label\":\"" : ",{\"label\":\"");
        webui_put_json_escaped(&out, openhab_discover_label(i));
        webui_put(&out, "\",\"host\":\"");
        webui_put_json_escaped(&out, openhab_discover_host(i));
        webui_putf(&out, "\",\"port\":%u}", (unsigned)openhab_discover_port(i));
    }

    webui_put(&out, "]}");
    webui_flush(&out);
    webui_end_chunked(req);
}

static void webui_handle_wifi(webui_request_t *req)
{
    char ssid[WLAN_SSID_SIZE];
    char psk[WLAN_PSK_SIZE];

    webui_arg(req, "ssid", ssid, sizeof(ssid));
    webui_arg(req, "psk", psk, sizeof(psk));

    if (wlan_set_credentials(ssid, psk) == false)
    {
        webui_redirect(req, 303, "/");
        return;
    }

    webui_redirect(req, 303, "/?wifi=1");
}

static void webui_handle_restart(webui_request_t *req)
{
    webui_send(req, 200, "text/html",
               "<!DOCTYPE html><html><head><meta charset='utf-8'>"
               "<meta http-equiv='refresh' content='15;URL=/'>"
               "</head><body>Restarting...</body></html>");

    /* Let the response reach the client before the radio goes away. */
    vTaskDelay(pdMS_TO_TICKS(200));
    port_restart();
}

static void webui_handle_favicon(webui_request_t *req)
{
    webui_send(req, 204, "text/plain", NULL);
}

static void webui_handle_not_found(webui_request_t *req)
{
    webui_redirect(req, 302, "/");
}

void webui_setup(Config *config)
{
    webui_config = config;

#if CONFIG_OHEZ_DEBUG_WEBUI
    /* The accessors reach into Config by offset, so a row naming a field that
     * has since moved or shrunk would quietly read and write its neighbours.
     * offsetof() keeps the offsets right by construction; this catches the
     * remaining case, a row whose kind no longer matches its field's width.
     * The table is shared with the touch settings screen now, so this checks
     * that screen's rows too -- it just happens to run from here. */
    for (size_t i = 0; i < config_field_count; i++)
    {
        const struct config_field_s *f = &config_fields[i];
        size_t                         width;

        switch (f->kind)
        {
        case SETTINGS_SECTION:
            continue;
        case SETTINGS_TEXT:
            width = f->size;
            break;
        case SETTINGS_BOOL:
            width = sizeof(bool);
            break;
        case SETTINGS_ULONG:
            width = sizeof(unsigned long);
            break;
        default:
            width = sizeof(unsigned int);
            break;
        }

        if (f->offset + width > sizeof(config_item_t))
            printf("settings: field '%s' runs past Config::item\r\n", f->name);
    }
#endif

    webui_transport_route("/", WEBUI_GET, webui_handle_root);
    webui_transport_route("/save", WEBUI_POST, webui_handle_save);
    webui_transport_route("/wifi", WEBUI_POST, webui_handle_wifi);
    webui_transport_route("/sitemaps", WEBUI_GET, webui_handle_sitemaps);
    webui_transport_route("/servers", WEBUI_GET, webui_handle_servers);
    webui_transport_route("/restart", WEBUI_POST, webui_handle_restart);
    webui_transport_route("/restart", WEBUI_GET, webui_handle_restart);

    /* Browsers ask for this unprompted; answering 204 keeps it out of the
     * not-found redirect. */
    webui_transport_route("/favicon.ico", WEBUI_GET, webui_handle_favicon);

    /* The path and the method are what tools/batchupdate.sh knows, so they do
     * not change. The GET is the upload form. */
    webui_transport_route("/update", WEBUI_GET, webui_ota_handle_form);
    webui_transport_route_stream("/update", webui_ota_handle_upload);

    /* Everything else, which includes one release of grace for bookmarks of
     * the AutoConnect page at /openhab_settings. */
    webui_transport_route_default(webui_handle_not_found);

    webui_transport_start(WEBUI_PORT);
}

void webui_loop(void)
{
    webui_transport_loop();
}
