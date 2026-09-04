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
 * And every setting is described exactly once, in settings_fields[], which one
 * loop renders and another parses; the AutoConnect version had the field list
 * written out three times, as the GET prefill, the POST parse and the echo
 * page, and they had drifted apart.
 *
 * Note for anyone extending this: it runs on the loop task, inside
 * handleClient(), with lv_timer_handler() not being pumped. It must not touch
 * LVGL. openhab_ui.hpp is included for the theme request, which is explicitly
 * a request -- recorded here, carried out from openhab_ui_loop().
 */

#include "webui.hpp"

#include "settings_fields.hpp"

#include "openhab_ui.hpp"
#include "ota/HTTPUpdateServer.h"
#include "wlan.hpp"
#include "ui_theme.hpp"
#include "version.h"
#include "debug.h"

#include <Arduino.h>
#include <WebServer.h>
#include <WiFi.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <uptime.h>

#ifndef DEBUG_WEBUI
#define DEBUG_WEBUI 0
#endif

/* Widest destination in Config::item is char[32]; the rest is headroom so
 * that an over-long submission is truncated here rather than rejected. */
#define WEBUI_VALUE_MAX 80

/* Unauthenticated, as it has always been. Kept as macros so that a build can
 * override them without touching this file. */
#ifndef UPDATER_USERNAME
#define UPDATER_USERNAME ""
#endif

#ifndef UPDATER_PASSWORD
#define UPDATER_PASSWORD ""
#endif

#ifndef WEBUI_PORT
#define WEBUI_PORT 80
#endif

/* No HTTP authentication, which is what AutoConnect was configured for too
 * (auth = AC_AUTH_NONE, and the OTA endpoint never had any). Worth knowing
 * before exposing one of these outside a home network. */
static WebServer        webui_http(WEBUI_PORT);
static HTTPUpdateServer webui_updater;

static Config    *webui_config = NULL;
static WebServer *webui_server = &webui_http;

/* ------------------------------------------------------------------ fields */

/* The row macros, the field table and the by-offset accessors live in
 * settings_fields.hpp: the touch settings screen walks the same rows, and a
 * second copy of the list here is exactly the drift the table was introduced
 * to stop. What stays below is the HTML rendering and the POST parsing.
 *
 * The WEBSERVER_MAX_POST_ARGS guard moved there with the table, as
 * SETTINGS_MAX_POST_ARGS, where the row count is a constant expression. */

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
    char   buf[512];
    size_t len;
};

#define WEBUI_OUT_FLUSH_AT (sizeof(((struct webui_out_s *)0)->buf) - 128)

static void webui_flush(struct webui_out_s *o)
{
    /* Not merely an optimisation: a zero-length sendContent() is the
     * terminating chunk, so flushing an empty buffer would end the response
     * in the middle of the page. */
    if (o->len == 0)
        return;

    webui_server->sendContent(o->buf, o->len);
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

/* -------------------------------------------------------------------- page */

static const char webui_page_head[] PROGMEM =
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
    "table.s{width:100%;font-size:.82em;border-collapse:collapse}"
    "table.s td{padding:1px 0}table.s td:first-child{color:#666;width:38%}"
    "p.n{font-size:.8em;color:#666}"
    "</style></head><body><div class='w'><h1>OhEzTouch</h1>";

static const char webui_page_tail[] PROGMEM = "</div></body></html>";

static void webui_send_status(struct webui_out_s *o)
{
    webui_put(o, "<fieldset><legend>Status</legend><table class='s'>");

    webui_putf(o, "<tr><td>Version</td><td>%u.%02u</td></tr>", VERSION_MAJOR, VERSION_MINOR);
    webui_putf(o, "<tr><td>Build</td><td>%s %s</td></tr>", __DATE__, __TIME__);

    uptime::calculateUptime();
    webui_putf(o, "<tr><td>Uptime</td><td>%lu days, %luh %lum %lus</td></tr>",
               uptime::getDays(), uptime::getHours(), uptime::getMinutes(), uptime::getSeconds());

    webui_put(o, "<tr><td>Hostname</td><td>");
    webui_put_escaped(o, WiFi.getHostname());
    webui_put(o, "</td></tr>");

    webui_put(o, "<tr><td>SSID</td><td>");
    webui_put_escaped(o, WiFi.SSID().c_str());
    webui_put(o, "</td></tr>");

    webui_putf(o, "<tr><td>RSSI</td><td>%i dBm</td></tr>", (int)WiFi.RSSI());
    webui_putf(o, "<tr><td>IP</td><td>%s</td></tr>", WiFi.localIP().toString().c_str());
    webui_putf(o, "<tr><td>MAC</td><td>%s</td></tr>", WiFi.macAddress().c_str());
    webui_putf(o, "<tr><td>Free heap</td><td>%u bytes</td></tr>", (unsigned)ESP.getFreeHeap());

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

    for (size_t i = 0; i < settings_field_count; i++)
    {
        const struct settings_field_s *f = &settings_fields[i];

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
            webui_putf(o, "<input type='text' name='%s' maxlength='%u' value='",
                       f->name, (unsigned)(f->size - 1));
            webui_put_escaped(o, settings_field_text(f, &config->item));
            webui_put(o, "'>");
            break;

        case SETTINGS_BOOL:
            /* No <br> for this one: a checkbox belongs on the same line as
             * its text, where every other kind wants its input underneath. */
            webui_putf(o, "<input type='checkbox' name='%s'%s>", f->name,
                       settings_field_read(f, &config->item) ? " checked" : "");
            break;

        case SETTINGS_ENUM:
        {
            int32_t selected = settings_field_read(f, &config->item);

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
                       (long)settings_field_read(f, &config->item));
            break;
        }

        webui_put(o, "</label>");
    }

    if (open == true)
        webui_put(o, "</fieldset>");

    webui_put(o, "<p class='n'>* takes effect after a restart.</p>"
                 "<button type='submit'>Save</button></form>");
}

/* A form of its own, and not only for tidiness: WebServer stops parsing a
 * body after SETTINGS_MAX_POST_ARGS arguments, and provisioning must not depend
 * on a valid settings round-trip. The passphrase is never sent back to the
 * browser, and an empty one is submitted as an open network. */
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

static void webui_begin_page(struct webui_out_s *o)
{
    o->len = 0;

    /* Chunked, because the length of the page is not known until it has been
     * written -- which is the point of not assembling it. */
    webui_server->setContentLength(CONTENT_LENGTH_UNKNOWN);
    webui_server->send(200, "text/html", "");
    webui_put(o, webui_page_head);
}

static void webui_end_page(struct webui_out_s *o)
{
    webui_put(o, webui_page_tail);
    webui_flush(o);
    webui_server->sendContent("", 0); /* the terminating empty chunk */
}

static void webui_handle_root()
{
    struct webui_out_s out;

    webui_begin_page(&out);

    if (webui_server->hasArg("saved") == true)
        webui_put(&out, "<fieldset><legend>Saved</legend>"
                        "<p class='n'>Settings stored.</p></fieldset>");

    if (webui_server->hasArg("wifi") == true)
        webui_put(&out, "<fieldset><legend>WLAN</legend>"
                        "<p class='n'>Credentials stored, connecting.</p></fieldset>");

    webui_send_status(&out);
    webui_send_wlan_form(&out);
    webui_send_form(&out, webui_config);

    webui_put(&out, "<form method='get' action='/update'>"
                    "<button type='submit'>Firmware update</button></form>"
                    "<form method='post' action='/restart'>"
                    "<button type='submit'>Restart</button></form>");

    webui_end_page(&out);
}

static void webui_handle_save()
{
    Config *config = webui_config;

    for (size_t i = 0; i < settings_field_count; i++)
    {
        const struct settings_field_s *f = &settings_fields[i];
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
            settings_field_write(f, &config->item, webui_server->hasArg(f->name) ? 1 : 0);
            continue;
        }

        if (webui_server->hasArg(f->name) == false)
            continue;

        strlcpy(value, webui_server->arg(f->name).c_str(), sizeof(value));

        /* The character check, the clamp and the option lookup all live in
         * settings_fields.cpp, so the touch screen applies the same rules to
         * the same rows -- a value the web form rejects is not one the panel
         * can smuggle in. */
        switch (f->kind)
        {
        case SETTINGS_TEXT:
            settings_field_set_text(f, &config->item, value);
            break;

        case SETTINGS_ENUM:
            settings_field_write(f, &config->item, settings_field_enum_from_name(f, value));
            break;

        default:
            settings_field_set_number(f, &config->item, strtol(value, NULL, 10));
            break;
        }
    }

    config->saveConfig();

#if DEBUG_WEBUI
    Serial.println("webui: settings saved");
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
    webui_server->sendHeader("Location", "/?saved=1", true);
    webui_server->send(303, "text/plain", "");
}

static void webui_handle_wifi()
{
    char ssid[WLAN_SSID_SIZE];
    char psk[WLAN_PSK_SIZE];

    strlcpy(ssid, webui_server->arg("ssid").c_str(), sizeof(ssid));
    strlcpy(psk, webui_server->arg("psk").c_str(), sizeof(psk));

    if (wlan_set_credentials(ssid, psk) == false)
    {
        webui_server->sendHeader("Location", "/", true);
        webui_server->send(303, "text/plain", "");
        return;
    }

    webui_server->sendHeader("Location", "/?wifi=1", true);
    webui_server->send(303, "text/plain", "");
}

static void webui_handle_restart()
{
    webui_server->send(200, "text/html",
                       "<!DOCTYPE html><html><head><meta charset='utf-8'>"
                       "<meta http-equiv='refresh' content='15;URL=/'>"
                       "</head><body>Restarting...</body></html>");

    /* Let the response reach the client before the radio goes away. */
    delay(200);
    ESP.restart();
}

static void webui_handle_not_found()
{
    webui_server->sendHeader("Location", "/", true);
    webui_server->send(302, "text/plain", "");
}

void webui_setup(Config *config)
{
    webui_config = config;

#if DEBUG_WEBUI
    /* The accessors reach into Config by offset, so a row naming a field that
     * has since moved or shrunk would quietly read and write its neighbours.
     * offsetof() keeps the offsets right by construction; this catches the
     * remaining case, a row whose kind no longer matches its field's width.
     * The table is shared with the touch settings screen now, so this checks
     * that screen's rows too -- it just happens to run from here. */
    for (size_t i = 0; i < settings_field_count; i++)
    {
        const struct settings_field_s *f = &settings_fields[i];
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

        if (f->offset + width > sizeof(settings_item_t))
            Serial.printf("settings: field '%s' runs past Config::item\r\n", f->name);
    }
#endif

    webui_server->on("/", HTTP_GET, webui_handle_root);
    webui_server->on("/save", HTTP_POST, webui_handle_save);
    webui_server->on("/wifi", HTTP_POST, webui_handle_wifi);
    webui_server->on("/restart", HTTP_POST, webui_handle_restart);
    webui_server->on("/restart", HTTP_GET, webui_handle_restart);

    /* Browsers ask for this unprompted; answering 204 keeps it out of the
     * not-found redirect. */
    webui_server->on("/favicon.ico", HTTP_GET, []() { webui_server->send(204); });

    /* One release of grace for bookmarks of the AutoConnect page. */
    webui_server->on("/openhab_settings", HTTP_GET, webui_handle_not_found);

    webui_server->onNotFound(webui_handle_not_found);

    /* Registered after our own pages so the explicit paths win the match, and
     * kept exactly as it was: tools/batchupdate.sh POSTs a multipart body to
     * /update, and HTTPUpdateServer ignores the field name. */
    webui_updater.setup(webui_server, "/update", UPDATER_USERNAME, UPDATER_PASSWORD);

    webui_server->begin();
}

void webui_loop(void)
{
    webui_server->handleClient();
}
