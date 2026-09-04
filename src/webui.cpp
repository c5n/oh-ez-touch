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
 * And every setting is described exactly once, in webui_fields[], which one
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

#include "openhab_ui.hpp"
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

/* WebServer's own cap lives in its Parsing.cpp rather than its header, so it
 * cannot be asserted against directly. This mirrors the framework default;
 * should the framework ever raise it, this stays wrong in the harmless
 * direction. */
#define WEBUI_MAX_POST_ARGS 32

static Config    *webui_config = NULL;
static WebServer *webui_server = NULL;

/* ------------------------------------------------------------------ fields */

enum webui_kind_e
{
    WEBUI_SECTION = 0, /* heading only, no field                            */
    WEBUI_TEXT,        /* char[]                                            */
    WEBUI_INT,         /* int                                               */
    WEBUI_UINT,        /* unsigned int                                      */
    WEBUI_ULONG,       /* unsigned long                                     */
    WEBUI_BOOL,        /* bool, rendered as a checkbox                      */
    WEBUI_ENUM         /* enum, rendered as a select over ->names           */
};

/* Reject '/' and ':' -- this was the ^[^/:]*$ pattern on the AutoConnect
 * inputs, which the browser enforced and the firmware did not, so a
 * hand-written POST could put anything into Config. */
#define WEBUI_F_HOSTCHARS 0x01u
/* Mark the label: the setting is only read during setup(). */
#define WEBUI_F_RESTART 0x02u

struct webui_field_s
{
    const char        *name;   /* POST argument name; [a-z0-9_] only        */
    const char        *label;
    const char *const *names;  /* WEBUI_ENUM: the option names              */
    uint16_t           offset; /* byte offset into Config::item             */
    int32_t            min;
    int32_t            max;
    uint8_t            kind;
    uint8_t            size;   /* WEBUI_TEXT: sizeof the destination        */
    uint8_t            count;  /* WEBUI_ENUM: number of options             */
    uint8_t            flags;
};

/* Config itself is not standard-layout -- it mixes a private String with the
 * public settings struct -- so offsetof() on it would be ill-formed.
 * Config::item is, and every offset below is relative to it.
 *
 * The target has to be an offset rather than a pointer or a lambda: a table
 * of pointers into a global would need dynamic initialisation and would land
 * in RAM, where this one stays in flash. Same reasoning as the uint32_t
 * colours in ui_style.hpp. */
typedef decltype(Config::item) webui_item_t;

#define OFF(path) ((uint16_t)offsetof(webui_item_t, path))
#define SZ(path) ((uint8_t)sizeof(((webui_item_t *)0)->path))

/* Shorthand, so that a row fits on one line and the table can be read against
 * the settings tables in README.md. */
#define SEC(lbl)                    {NULL, (lbl), NULL, 0,          0,     0, WEBUI_SECTION, 0,        0,   0}
#define TXT(nm, lbl, path, fl)      {(nm), (lbl), NULL, OFF(path),  0,     0, WEBUI_TEXT,    SZ(path), 0,   (fl)}
#define SINT(nm, lbl, path, lo, hi) {(nm), (lbl), NULL, OFF(path), (lo), (hi), WEBUI_INT,    0,        0,   0}
#define UINT(nm, lbl, path, lo, hi) {(nm), (lbl), NULL, OFF(path), (lo), (hi), WEBUI_UINT,   0,        0,   0}
#define ULNG(nm, lbl, path, lo, hi) {(nm), (lbl), NULL, OFF(path), (lo), (hi), WEBUI_ULONG,  0,        0,   0}
#define CHK(nm, lbl, path)          {(nm), (lbl), NULL, OFF(path),  0,     0, WEBUI_BOOL,    0,        0,   0}
#define SEL(nm, lbl, path, tbl, n)  {(nm), (lbl), (tbl), OFF(path), 0, (n) - 1, WEBUI_ENUM,  0,      (n),   0}

static const struct webui_field_s webui_fields[] = {

    SEC("General"),
    TXT("hostname", "Hostname", general.hostname, WEBUI_F_HOSTCHARS | WEBUI_F_RESTART),

    SEC("NTP Time"),
    TXT("ntp_host", "Host", ntp.hostname, WEBUI_F_HOSTCHARS | WEBUI_F_RESTART),
    SINT("ntp_gmt", "GMT offset [h]", ntp.gmt_offset, -12, 14),
    CHK("ntp_dst", "Daylight saving (+1h)", ntp.daylightsaving),

    SEC("Appearance"),
    /* The option names come straight from ui_theme.hpp, so the dropdown, the
     * config file and the simulator's environment variables cannot drift
     * apart. Unlike the AutoConnect version this needs no 1-based index
     * arithmetic: the POST carries the name, and the lookup owns the
     * fallback. */
    SEL("theme", "Theme", ui.theme, ui_theme_names, UI_THEME_FAMILY_COUNT),
    SEL("night_mode", "Night mode", ui.night_mode, ui_night_mode_names, UI_NIGHT_MODE_COUNT),
    UINT("night_from", "Night from [h]", ui.night_from, 0, 23),
    UINT("night_to", "Night to [h]", ui.night_to, 0, 23),

    SEC("LCD Backlight Dimming"),
    ULNG("bl_timeout", "Activity timeout [s] (0=off)", backlight.activity_timeout, 0, 86400),
    UINT("bl_normal", "Normal brightness [%]", backlight.normal_brightness, 0, 100),
    UINT("bl_dim", "Dim brightness [%]", backlight.dim_brightness, 0, 100),

    SEC("Beeper"),
    CHK("beeper", "Enable beeper", beeper.enabled),

    SEC("OpenHAB Server"),
    TXT("oh_host", "Host", openhab.hostname, WEBUI_F_HOSTCHARS),
    SINT("oh_port", "Port", openhab.port, 1, 65535),
    TXT("oh_sitemap", "Sitemap", openhab.sitemap, WEBUI_F_HOSTCHARS),

    SEC("Sensors"),
    CHK("bme_use", "Use BME280 sensor", openhab.sensors.bme280.use),
    SINT("bme_interval", "Update interval [s]", openhab.sensors.bme280.interval, 1, 86400),
    TXT("bme_temp", "Temperature item", openhab.sensors.bme280.items.temperature, 0),
    TXT("bme_hum", "Humidity item", openhab.sensors.bme280.items.humidity, 0),
    TXT("bme_press", "Pressure item", openhab.sensors.bme280.items.pressure, 0),
};

#define WEBUI_FIELD_COUNT (sizeof(webui_fields) / sizeof(webui_fields[0]))

/* WebServer stops parsing after WEBSERVER_MAX_POST_ARGS arguments and says so
 * only through one log_e(), so a form that outgrows the cap loses fields in
 * silence. The settings form posts one argument per non-section row; the WLAN
 * credentials are deliberately a second form for that reason. */
static_assert(WEBUI_FIELD_COUNT <= WEBUI_MAX_POST_ARGS,
              "the settings form would exceed WEBSERVER_MAX_POST_ARGS");

/* The generic accessors below reach into Config by offset, so a field whose C
 * type stops matching its kind would corrupt its neighbours rather than fail
 * to compile. These are the checks that keep the table honest. */
static_assert(sizeof(int) == sizeof(int32_t), "WEBUI_INT width");
static_assert(sizeof(enum ui_theme_family_e) == sizeof(unsigned int), "WEBUI_ENUM width");
static_assert(sizeof(enum ui_night_mode_e) == sizeof(unsigned int), "WEBUI_ENUM width");

static int32_t webui_field_read(const struct webui_field_s *f, const Config *config)
{
    const void *p = (const uint8_t *)&config->item + f->offset;

    switch (f->kind)
    {
    case WEBUI_INT:
        return (int32_t) * (const int *)p;
    case WEBUI_UINT:
    case WEBUI_ENUM:
        return (int32_t) * (const unsigned int *)p;
    case WEBUI_ULONG:
        return (int32_t) * (const unsigned long *)p;
    case WEBUI_BOOL:
        return *(const bool *)p ? 1 : 0;
    default:
        return 0;
    }
}

static void webui_field_write(const struct webui_field_s *f, Config *config, int32_t value)
{
    void *p = (uint8_t *)&config->item + f->offset;

    switch (f->kind)
    {
    case WEBUI_INT:
        *(int *)p = (int)value;
        break;
    case WEBUI_UINT:
    case WEBUI_ENUM:
        *(unsigned int *)p = (unsigned int)value;
        break;
    case WEBUI_ULONG:
        *(unsigned long *)p = (unsigned long)value;
        break;
    case WEBUI_BOOL:
        *(bool *)p = (value != 0);
        break;
    default:
        break;
    }
}

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

    webui_put(o, "</table></fieldset>");
}

static void webui_send_form(struct webui_out_s *o, const Config *config)
{
    bool open = false;

    webui_put(o, "<form method='post' action='/save'>");

    for (size_t i = 0; i < WEBUI_FIELD_COUNT; i++)
    {
        const struct webui_field_s *f = &webui_fields[i];
        const void                 *p = (const uint8_t *)&config->item + f->offset;

        if (f->kind == WEBUI_SECTION)
        {
            if (open == true)
                webui_put(o, "</fieldset>");

            webui_putf(o, "<fieldset><legend>%s</legend>", f->label);
            open = true;
            continue;
        }

        webui_putf(o, "<label>%s%s%s", f->label,
                   (f->flags & WEBUI_F_RESTART) ? " *" : "",
                   (f->kind == WEBUI_BOOL) ? " " : "<br>");

        switch (f->kind)
        {
        case WEBUI_TEXT:
            webui_putf(o, "<input type='text' name='%s' maxlength='%u' value='",
                       f->name, (unsigned)(f->size - 1));
            webui_put_escaped(o, (const char *)p);
            webui_put(o, "'>");
            break;

        case WEBUI_BOOL:
            /* No <br> for this one: a checkbox belongs on the same line as
             * its text, where every other kind wants its input underneath. */
            webui_putf(o, "<input type='checkbox' name='%s'%s>", f->name,
                       *(const bool *)p ? " checked" : "");
            break;

        case WEBUI_ENUM:
        {
            int32_t selected = webui_field_read(f, config);

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
                       (long)webui_field_read(f, config));
            break;
        }

        webui_put(o, "</label>");
    }

    if (open == true)
        webui_put(o, "</fieldset>");

    webui_put(o, "<p class='n'>* takes effect after a restart.</p>"
                 "<button type='submit'>Save</button></form>");
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

    webui_send_status(&out);
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

    for (size_t i = 0; i < WEBUI_FIELD_COUNT; i++)
    {
        const struct webui_field_s *f = &webui_fields[i];
        char                        value[WEBUI_VALUE_MAX];

        if (f->kind == WEBUI_SECTION)
            continue;

        /* An unchecked box is simply absent from the body, so a checkbox is
         * the one kind whose absence carries meaning. For every other kind an
         * absent argument leaves the stored value alone, which is what makes a
         * partial POST -- an older firmware's bookmarked form, say -- harmless
         * rather than destructive. */
        if (f->kind == WEBUI_BOOL)
        {
            webui_field_write(f, config, webui_server->hasArg(f->name) ? 1 : 0);
            continue;
        }

        if (webui_server->hasArg(f->name) == false)
            continue;

        strlcpy(value, webui_server->arg(f->name).c_str(), sizeof(value));

        switch (f->kind)
        {
        case WEBUI_TEXT:
            /* Enforced here and not only by the browser's pattern attribute,
             * because a hand-written POST does not run the browser's. */
            if ((f->flags & WEBUI_F_HOSTCHARS) && strpbrk(value, "/:") != NULL)
                break;

            strlcpy((char *)&config->item + f->offset, value, f->size);
            break;

        case WEBUI_ENUM:
        {
            /* Unknown names resolve to the first option, the same fallback
             * ui_theme_from_name() applies to the config file. */
            int32_t index = 0;

            for (uint8_t n = 0; n < f->count; n++)
            {
                if (strcasecmp(value, f->names[n]) == 0)
                {
                    index = n;
                    break;
                }
            }

            webui_field_write(f, config, index);
            break;
        }

        default:
        {
            long n = strtol(value, NULL, 10);

            if (n < f->min)
                n = f->min;
            if (n > f->max)
                n = f->max;

            webui_field_write(f, config, (int32_t)n);
            break;
        }
        }
    }

    config->saveConfig();

#if DEBUG_WEBUI
    Serial.println("webui: settings saved");
#endif

    /* The theme is the one setting that applies without a restart. This is a
     * request rather than a repaint: the repaint happens in openhab_ui_loop(),
     * on the task that owns LVGL. */
    openhab_ui_request_theme(config->item.ui.theme, openhab_ui_night_active(config));

    /* 303 rather than a page of its own: the browser re-GETs /, which renders
     * the values actually stored. That is what AutoConnect's echo page was
     * for, minus a third copy of the field list, and it also means a reload
     * does not re-post the form. */
    webui_server->sendHeader("Location", "/?saved=1", true);
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

void webui_setup(Config *config, WebServer *server)
{
    webui_config = config;
    webui_server = server;

#if DEBUG_WEBUI
    /* The accessors reach into Config by offset, so a row naming a field that
     * has since moved or shrunk would quietly read and write its neighbours.
     * offsetof() keeps the offsets right by construction; this catches the
     * remaining case, a row whose kind no longer matches its field's width. */
    for (size_t i = 0; i < WEBUI_FIELD_COUNT; i++)
    {
        const struct webui_field_s *f = &webui_fields[i];
        size_t                      width;

        switch (f->kind)
        {
        case WEBUI_SECTION:
            continue;
        case WEBUI_TEXT:
            width = f->size;
            break;
        case WEBUI_BOOL:
            width = sizeof(bool);
            break;
        case WEBUI_ULONG:
            width = sizeof(unsigned long);
            break;
        default:
            width = sizeof(unsigned int);
            break;
        }

        if (f->offset + width > sizeof(webui_item_t))
            Serial.printf("webui: field '%s' runs past Config::item\r\n", f->name);
    }
#endif

    webui_server->on("/", HTTP_GET, webui_handle_root);
    webui_server->on("/save", HTTP_POST, webui_handle_save);
    webui_server->on("/restart", HTTP_POST, webui_handle_restart);
    webui_server->on("/restart", HTTP_GET, webui_handle_restart);

    /* Browsers ask for this unprompted; answering 204 keeps it out of the
     * not-found redirect. */
    webui_server->on("/favicon.ico", HTTP_GET, []() { webui_server->send(204); });

    /* One release of grace for bookmarks of the AutoConnect page. */
    webui_server->on("/openhab_settings", HTTP_GET, webui_handle_not_found);

    webui_install_not_found();
}

void webui_install_not_found(void)
{
    webui_server->onNotFound(webui_handle_not_found);
}
