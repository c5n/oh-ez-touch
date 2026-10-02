/**
 * @file openhab_events.cpp
 *
 * See openhab_events.hpp.
 */

#include "sdkconfig.h"

#include "openhab_events.hpp"

#include "json_squeeze.h"
#include "openhab_http.hpp"
#include "port/port_sys.h"
#include "sim/sim_offline.hpp"

#include <atomic>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "esp_http_client.h"
#include "esp_log.h"

static const char *TAG = "openhab_events";

/* As the client task's, and for the same reasons; see openhab_client.cpp. */
#if CONFIG_IDF_TARGET_LINUX
#define OPENHAB_EVENTS_TASK_STACK_SIZE 32768
#else
#define OPENHAB_EVENTS_TASK_STACK_SIZE 4096
#endif

#define OPENHAB_EVENTS_TASK_PRIORITY 1

/* How long one read waits before the task looks at its flags again. It is
 * also the latency of an event: esp_http_client_read() returns early only on
 * a timeout, so a read asking for more than one event's worth comes back this
 * long after the last byte arrived. A quarter of a second is below what a
 * finger notices and costs the task four wakeups a second. */
#define OPENHAB_EVENTS_READ_TIMEOUT_MS 250

/* Three missed keepalives. openHAB 3.4 and later send one every ten seconds,
 * so a stream this quiet is a stream whose server has gone; an older server
 * sends none, and gets a reconnect -- and a resync -- every thirty seconds,
 * which is still less traffic than polling. */
#define OPENHAB_EVENTS_WATCHDOG_MS 30000

/* The sitemap stream's keepalive comes about once a minute on openHAB 5.2.1,
 * so two of them missed. */
#define OPENHAB_EVENTS_SITEMAP_WATCHDOG_MS 130000

/* The answer to the subscribe POST: a status and a context object, about 330
 * bytes with a long host in its Location. */
#define OPENHAB_EVENTS_SUBSCRIBE_BODY_LEN 768

/* A subscription id is a UUID, 36 characters. */
#define OPENHAB_EVENTS_SUBSCRIPTION_LEN 48

#define OPENHAB_EVENTS_BACKOFF_MIN_MS 5000
#define OPENHAB_EVENTS_BACKOFF_MAX_MS 60000

/* The request line has to fit esp_http_client's transmit buffer together with
 * the headers, so the buffer is sized from the URL with room for those. A
 * subscription whose topic list does not fit falls back to every item's
 * changes and leaves the UI to pick out its own, which it does anyway. */
#define OPENHAB_EVENTS_URL_LEN 768
#define OPENHAB_EVENTS_TX_BUFFER_SIZE (OPENHAB_EVENTS_URL_LEN + 256)

static QueueHandle_t events = NULL;
static SemaphoreHandle_t sub_lock = NULL;
static TaskHandle_t task = NULL;

/* Written by subscribe() under sub_lock, copied out by the task under it. */
static struct openhab_events_subscription_s sub_wanted;
static std::atomic<bool> sub_changed{false};
static std::atomic<bool> reset_requested{false};

static std::atomic<bool> streaming{false};
static std::atomic<bool> resync{false};
static std::atomic<bool> reload{false};
static std::atomic<bool> sitemap_mode{false};

/* The task's own copy, and everything else only it touches. */
static struct openhab_events_subscription_s sub_active;
static esp_http_client_handle_t client;
static struct sse_reader reader;
static char url[OPENHAB_EVENTS_URL_LEN];

/* The website whose server refused the sitemap stream, so that it is asked
 * once and not on every reconnect. "" while none has. */
static char sitemap_refused[OPENHAB_EVENTS_WEBSITE_LEN];

/* A sitemap stream for this subscription has been up before: the next one to
 * come up has missed whatever changed in between. */
static bool sitemap_was_up;

static char subscribe_body[OPENHAB_EVENTS_SUBSCRIBE_BODY_LEN];

static void openhab_events_task(void *parameter);

bool openhab_events_setup(void)
{
    if (task != NULL)
        return true;

    events = xQueueCreate(OPENHAB_EVENTS_QUEUE_DEPTH, sizeof(struct openhab_event_s));
    sub_lock = xSemaphoreCreateMutex();

    if (events == NULL || sub_lock == NULL)
    {
        ESP_LOGE(TAG, "cannot create the queue");
        goto fail;
    }

    if (xTaskCreate(openhab_events_task, "openhab_events", OPENHAB_EVENTS_TASK_STACK_SIZE,
                    NULL, OPENHAB_EVENTS_TASK_PRIORITY, &task) != pdPASS)
    {
        ESP_LOGE(TAG, "cannot create the task");
        task = NULL;
        goto fail;
    }

    return true;

fail:
    if (events != NULL)
    {
        vQueueDelete(events);
        events = NULL;
    }

    if (sub_lock != NULL)
    {
        vSemaphoreDelete(sub_lock);
        sub_lock = NULL;
    }

    return false;
}

/* The part of a subscription the sitemap stream is opened for. */
static bool subscription_page_equal(const struct openhab_events_subscription_s *a,
                                    const struct openhab_events_subscription_s *b)
{
    return strcmp(a->website, b->website) == 0 && strcmp(a->sitemap, b->sitemap) == 0
        && strcmp(a->page, b->page) == 0;
}

static bool subscription_equal(const struct openhab_events_subscription_s *a,
                               const struct openhab_events_subscription_s *b)
{
    if (a->count != b->count || subscription_page_equal(a, b) == false)
        return false;

    for (size_t i = 0; i < a->count; ++i)
        if (   a->item[i].group != b->item[i].group
            || strcmp(a->item[i].name, b->item[i].name) != 0)
            return false;

    return true;
}

void openhab_events_subscribe(const struct openhab_events_subscription_s *sub)
{
    if (task == NULL)
        return;

    bool changed = false;

    xSemaphoreTake(sub_lock, portMAX_DELAY);

    if (subscription_equal(sub, &sub_wanted) == false)
    {
        sub_wanted = *sub;

        if (sub_wanted.count > OPENHAB_EVENTS_ITEM_MAX)
            sub_wanted.count = OPENHAB_EVENTS_ITEM_MAX;

        changed = true;
    }

    xSemaphoreGive(sub_lock);

    if (changed == true)
    {
        sub_changed.store(true, std::memory_order_release);
        xTaskNotifyGive(task);
    }
}

void openhab_events_reset(void)
{
    if (task == NULL)
        return;

    reset_requested.store(true, std::memory_order_release);
    xTaskNotifyGive(task);
}

bool openhab_events_streaming(void)
{
    return streaming.load(std::memory_order_relaxed);
}

bool openhab_events_take_resync(void)
{
    return resync.exchange(false, std::memory_order_relaxed);
}

bool openhab_events_take_reload(void)
{
    return reload.exchange(false, std::memory_order_relaxed);
}

bool openhab_events_poll(struct openhab_event_s *out)
{
    if (events == NULL)
        return false;

    return xQueueReceive(events, out, 0) == pdTRUE;
}

/* Append to url[] at *len; false when it did not fit. */
static bool url_append(size_t *len, const char *a, const char *b, const char *c)
{
    int n = snprintf(url + *len, sizeof(url) - *len, "%s%s%s", a, b, c);

    if (n < 0 || (size_t)n >= sizeof(url) - *len)
        return false;

    *len += (size_t)n;

    return true;
}

/* The topic filter, one entry per item.
 *
 * "*" at the front matches the root segment, which is "openhab" today and was
 * "smarthome" on openHAB 2; the server turns each "*" into ".*", so it also
 * crosses the slash. The item names need no escaping: openHAB allows only
 * letters, digits and the underscore in them, all of which the filter's own
 * character check (\w) accepts. */
static void url_build(void)
{
    size_t len = 0;
    bool fits = url_append(&len, sub_active.website, "/rest/events?topics=", "");

    for (size_t i = 0; fits == true && i < sub_active.count; ++i)
    {
        const char *name = sub_active.item[i].name;

        fits = url_append(&len, (i > 0) ? "," : "", "*/items/", name)
            && url_append(&len, "/statechanged", "", "");

        if (fits == true && sub_active.item[i].group == true)
            fits = url_append(&len, ",*/items/", name, "/*/statechanged");
    }

    if (fits == true)
        return;

    ESP_LOGW(TAG, "%u items do not fit a %u byte URL; listening to every item",
             (unsigned)sub_active.count, (unsigned)sizeof(url));

    len = 0;
    url_append(&len, sub_active.website,
               "/rest/events?topics=*/items/*/statechanged", "");
}

/* Never waits. The UI drains this every few milliseconds, so a full queue is
 * a burst the UI has not caught up with yet -- and the cure for a dropped
 * change is the poll a resync brings, or on the sitemap stream the page
 * fetched again, not a stream that stops being read. */
static void event_queue(const struct openhab_event_s *ev)
{
    if (xQueueSend(events, ev, 0) == pdTRUE)
        return;

    ESP_LOGW(TAG, "queue full; dropping an event and asking for a resync");

    if (ev->kind == OPENHAB_EVENT_KIND_WIDGET)
        reload.store(true, std::memory_order_relaxed);
    else
        resync.store(true, std::memory_order_relaxed);
}

static void on_sitemap_data(const char *data, size_t len, void *ctx)
{
    uint64_t *last_data = (uint64_t *)ctx;
    struct openhab_event_s ev;

    ev.kind = OPENHAB_EVENT_KIND_WIDGET;

    switch (openhab_sitemap_event_parse(data, len, &ev.widget))
    {
    case OPENHAB_SITEMAP_EVENT_WIDGET:
#if CONFIG_OHEZ_DEBUG_OPENHAB_EVENTS
        ESP_LOGI(TAG, "widget %08x = \"%s\" [%s]", (unsigned)ev.widget.widget,
                 ev.widget.state, ev.widget.display);
#endif
        event_queue(&ev);
        break;

    case OPENHAB_SITEMAP_EVENT_RELOAD:
        ESP_LOGI(TAG, "the sitemap changed; asking for the page again");
        reload.store(true, std::memory_order_relaxed);
        break;

    case OPENHAB_SITEMAP_EVENT_INVALID:
        ESP_LOGW(TAG, "not an event: %.*s", (int)len, data);
        break;

    default:
        break;
    }

    *last_data = port_millis();
}

static void on_data(const char *data, size_t len, void *ctx)
{
    uint64_t *last_data = (uint64_t *)ctx;
    struct openhab_event_s ev;

    ev.kind = OPENHAB_EVENT_KIND_ITEM;

    switch (openhab_event_parse(data, len, ev.item.name, sizeof(ev.item.name),
                                ev.item.value, sizeof(ev.item.value)))
    {
    case OPENHAB_EVENT_STATE:
#if CONFIG_OHEZ_DEBUG_OPENHAB_EVENTS
        ESP_LOGI(TAG, "%s = \"%s\"", ev.item.name, ev.item.value);
#endif
        event_queue(&ev);
        break;

    case OPENHAB_EVENT_INVALID:
        ESP_LOGW(TAG, "not an event: %.*s", (int)len, data);
        break;

    case OPENHAB_EVENT_ALIVE:
    case OPENHAB_EVENT_OTHER:
    default:
        break;
    }

    /* Only the keepalive is there to say the stream is alive, but anything
     * that arrived says so as well. */
    *last_data = port_millis();
}

enum stream_end_e
{
    STREAM_CHANGED, /* asked to stop: reconnect at once                  */
    STREAM_FAILED,  /* never opened, or lost: reconnect after the backoff */
};

static bool stop_requested(void)
{
    if (reset_requested.load(std::memory_order_acquire) == true)
        return true;

    if (sub_changed.load(std::memory_order_acquire) == false)
        return false;

    /* The sitemap stream follows the page, not the items on it: the UI
     * changes those whenever the clock screen comes up or goes away, and a
     * new POST and a new stream for that would be traffic for nothing. Taken
     * in place, then, as long as the page is the same. */
    if (sitemap_mode.load(std::memory_order_relaxed) == true)
    {
        bool same_page;

        xSemaphoreTake(sub_lock, portMAX_DELAY);
        same_page = subscription_page_equal(&sub_wanted, &sub_active);

        if (same_page == true)
        {
            sub_active = sub_wanted;
            sub_changed.store(false, std::memory_order_release);
        }

        xSemaphoreGive(sub_lock);

        return same_page == false;
    }

    return true;
}

static bool client_prepare(esp_http_client_method_t method, const char *accept)
{
    if (client == NULL)
    {
        esp_http_client_config_t cfg = {};

        cfg.url = url;
        cfg.method = method;
        cfg.timeout_ms = OPENHAB_HTTP_TIMEOUT_MS;
        cfg.buffer_size_tx = OPENHAB_EVENTS_TX_BUFFER_SIZE;

        client = esp_http_client_init(&cfg);

        if (client == NULL)
        {
            ESP_LOGE(TAG, "cannot create a client");
            return false;
        }
    }
    else if (esp_http_client_set_url(client, url) != ESP_OK)
    {
        ESP_LOGE(TAG, "cannot set the URL: %s", url);
        return false;
    }

    /* The open and the headers wait as long as any other request; only the
     * reads of the body are short. Set again on every connect, because the
     * last stream left it short. */
    esp_http_client_set_timeout_ms(client, OPENHAB_HTTP_TIMEOUT_MS);

    /* With the port, for the reason session_prepare() in openhab_http.cpp
     * gives: esp_http_client drops it when the host changes. The website is
     * always "http://host:port". */
    const char *authority = strstr(sub_active.website, "://");

    esp_http_client_set_header(client, "Host",
                               (authority != NULL) ? authority + 3 : sub_active.website);
    esp_http_client_set_method(client, method);
    esp_http_client_set_header(client, "Accept", accept);

    /* A stream ended by the server can leave the tail of its last read cached
     * on the handle; see session_prepare() for what that cost once. */
    esp_http_client_clear_response_buffer(client);

    return true;
}

enum subscribe_e
{
    SUBSCRIBE_OK,      /* `id` is set                                        */
    SUBSCRIBE_REFUSED, /* the server answered, and not with a subscription   */
    SUBSCRIBE_FAILED,  /* no answer: a server that is not there right now    */
};

/* POST /rest/sitemaps/events/subscribe, for the id the stream is opened on.
 *
 * A fresh one for every stream, rather than one kept: openHAB lets go of a
 * subscription some time after its stream closes, and a GET on an id it has
 * let go of is not refused -- it is never answered at all, which would cost a
 * whole HTTP timeout to find out. */
static enum subscribe_e subscribe(char *id, size_t id_size)
{
    size_t len = 0;

    if (url_append(&len, sub_active.website, "/rest/sitemaps/events/subscribe", "") == false
        || client_prepare(HTTP_METHOD_POST, "application/json") == false)
        return SUBSCRIBE_FAILED;

    esp_err_t err = esp_http_client_open(client, 0);

    if (err != ESP_OK)
    {
        ESP_LOGW(TAG, "POST %s: %s", url, esp_err_to_name(err));
        esp_http_client_close(client);
        return SUBSCRIBE_FAILED;
    }

    if (esp_http_client_fetch_headers(client) < 0)
    {
        ESP_LOGW(TAG, "POST %s: no response headers", url);
        esp_http_client_close(client);
        return SUBSCRIBE_FAILED;
    }

    int status = esp_http_client_get_status_code(client);
    int body = esp_http_client_read_response(client, subscribe_body, sizeof(subscribe_body) - 1);

    esp_http_client_close(client);

    /* 404 is a server without sitemap events, 401 one that wants a token the
     * panel does not send. Neither changes by asking again. */
    if (status != 200)
    {
        ESP_LOGW(TAG, "POST %s: HTTP %d", url, status);
        return SUBSCRIBE_REFUSED;
    }

    if (body <= 0 || openhab_sitemap_subscription_id(subscribe_body, (size_t)body, id, id_size) == false)
    {
        ESP_LOGW(TAG, "POST %s: no subscription in the answer", url);
        return SUBSCRIBE_REFUSED;
    }

    return SUBSCRIBE_OK;
}

enum wait_e
{
    WAIT_READABLE,
    WAIT_STOPPED,  /* stop_requested() */
    WAIT_TIMEOUT,
};

/* Wait for the first bytes of an answer, looking at the flags every read
 * timeout.
 *
 * openHAB answers the sitemap stream's GET with nothing at all -- not even the
 * status line -- until it has a first event to send, and on a quiet page that
 * is the keepalive, up to a minute later. esp_http_client_fetch_headers()
 * cannot be asked to wait that long: it would not see the page change under
 * it, and every timeout it does see it logs. So the socket is watched here,
 * and the headers fetched once there is something to fetch. */
static enum wait_e wait_readable(uint32_t max_ms)
{
    int      fd = esp_http_client_get_socket(client);
    uint64_t start = port_millis();

    if (fd < 0)
        return WAIT_READABLE; /* let fetch_headers() find out */

    while (port_millis() - start < max_ms)
    {
        if (stop_requested() == true)
            return WAIT_STOPPED;

        fd_set         readable;
        struct timeval tv = { 0, OPENHAB_EVENTS_READ_TIMEOUT_MS * 1000 };

        FD_ZERO(&readable);
        FD_SET(fd, &readable);

        int ready = select(fd + 1, &readable, NULL, NULL, &tv);

        if (ready != 0)
            return WAIT_READABLE; /* data, or an error fetch_headers() reports */
    }

    return WAIT_TIMEOUT;
}

static enum stream_end_e stream(bool *answered)
{
    *answered = false;

    if (client_prepare(HTTP_METHOD_GET, "text/event-stream") == false)
        return STREAM_FAILED;

    esp_err_t err = esp_http_client_open(client, 0);

    if (err != ESP_OK)
    {
        ESP_LOGW(TAG, "GET %s: %s", url, esp_err_to_name(err));
        esp_http_client_close(client);
        return STREAM_FAILED;
    }

    if (sitemap_mode.load(std::memory_order_relaxed) == true)
    {
        switch (wait_readable(OPENHAB_EVENTS_SITEMAP_WATCHDOG_MS))
        {
        case WAIT_STOPPED:
            esp_http_client_close(client);
            sitemap_mode.store(false, std::memory_order_relaxed);
            return STREAM_CHANGED;

        case WAIT_TIMEOUT:
            ESP_LOGW(TAG, "GET %s: no answer", url);
            esp_http_client_close(client);
            sitemap_mode.store(false, std::memory_order_relaxed);
            return STREAM_FAILED;

        case WAIT_READABLE:
        default:
            break;
        }
    }

    if (esp_http_client_fetch_headers(client) < 0)
    {
        ESP_LOGW(TAG, "GET %s: no response headers", url);
        esp_http_client_close(client);
        return STREAM_FAILED;
    }

    int status = esp_http_client_get_status_code(client);

    if (status != 200)
    {
        /* 400 is a topic list the server would not take, 404 a server without
         * the endpoint. Neither improves by asking faster, which is what the
         * backoff is for; polling carries on meanwhile. */
        ESP_LOGW(TAG, "GET %s: HTTP %d", url, status);
        esp_http_client_close(client);
        return STREAM_FAILED;
    }

    *answered = true;

    bool     sitemap = sitemap_mode.load(std::memory_order_relaxed);
    uint32_t watchdog_ms = sitemap ? OPENHAB_EVENTS_SITEMAP_WATCHDOG_MS : OPENHAB_EVENTS_WATCHDOG_MS;

    if (sitemap == true)
        ESP_LOGI(TAG, "streaming page %s of %s", sub_active.page, sub_active.sitemap);
    else
        ESP_LOGI(TAG, "streaming %u items", (unsigned)sub_active.count);

    esp_http_client_set_timeout_ms(client, OPENHAB_EVENTS_READ_TIMEOUT_MS);
    sse_reader_reset(&reader);

    struct json_squeeze_s squeeze;

    json_squeeze_reset(&squeeze);

    /* Whatever changed while there was no stream went unseen. A state comes
     * back with a poll; what the sitemap's rules made of it -- a colour, a
     * widget shown or hidden -- only with the page, so a sitemap stream that
     * comes up again asks for that as well. */
    streaming.store(true, std::memory_order_relaxed);
    resync.store(true, std::memory_order_relaxed);

    if (sitemap == true)
    {
        if (sitemap_was_up == true)
            reload.store(true, std::memory_order_relaxed);

        sitemap_was_up = true;
    }

    uint64_t last_data = port_millis();
    enum stream_end_e end = STREAM_FAILED;

    for (;;)
    {
        if (stop_requested() == true)
        {
            end = STREAM_CHANGED;
            break;
        }

        char buf[256];
        int read = esp_http_client_read(client, buf, sizeof(buf));

        if (read > 0)
        {
            /* An Image item's new snapshot arrives as an event carrying the
             * whole picture, far over SSE_LINE_LEN; cut down to its first few
             * hundred bytes it is an ordinary widget event again, which is
             * what tells an open picture to fetch the new one -- instead of a
             * dropped line and the page loaded again for every ring. */
            size_t kept = json_squeeze(&squeeze, buf, (size_t)read);

            sse_reader_feed(&reader, buf, kept, sitemap ? on_sitemap_data : on_data,
                            &last_data);

            if (sse_reader_take_overflow(&reader) == true)
            {
                ESP_LOGW(TAG, "an event did not fit %u bytes; dropped", (unsigned)SSE_LINE_LEN);

                if (sitemap == true)
                    reload.store(true, std::memory_order_relaxed);
                else
                    resync.store(true, std::memory_order_relaxed);
            }

            continue;
        }

        if (read == -ESP_ERR_HTTP_EAGAIN)
        {
            if (port_millis() - last_data < watchdog_ms)
                continue;

            ESP_LOGW(TAG, "nothing for %u ms; reconnecting", (unsigned)watchdog_ms);
            break;
        }

        /* 0 is the end of the body -- a server shutting down ends its streams
         * cleanly -- and anything below it a socket that failed. */
        ESP_LOGW(TAG, "stream ended (%d)", read);
        break;
    }

    streaming.store(false, std::memory_order_relaxed);
    sitemap_mode.store(false, std::memory_order_relaxed);
    esp_http_client_close(client);

    return end;
}

static void openhab_events_task(void *parameter)
{
    (void)parameter;

    uint32_t backoff_ms = OPENHAB_EVENTS_BACKOFF_MIN_MS;

    for (;;)
    {
        reset_requested.store(false, std::memory_order_relaxed);

        if (sub_changed.exchange(false, std::memory_order_acquire) == true)
        {
            xSemaphoreTake(sub_lock, portMAX_DELAY);
            sub_active = sub_wanted;
            xSemaphoreGive(sub_lock);

            /* A new set is not the old server failing again. */
            backoff_ms = OPENHAB_EVENTS_BACKOFF_MIN_MS;

            /* Nor is a new page one whose changes were missed. */
            sitemap_was_up = false;
        }

        /* Offline mode answers polls from fixtures, and there is nothing at
         * the other end to stream from. */
        if ((sub_active.count == 0 && sub_active.page[0] == '\0') || sim_offline() == true)
        {
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
            continue;
        }

        /* A server that refused the sitemap stream is not asked again; a
         * different server is. */
        if (sitemap_refused[0] != '\0' && strcmp(sitemap_refused, sub_active.website) != 0)
            sitemap_refused[0] = '\0';

        bool use_sitemap = sub_active.sitemap[0] != '\0' && sub_active.page[0] != '\0'
                        && sitemap_refused[0] == '\0';
        bool answered = false;
        enum stream_end_e end = STREAM_FAILED;

        if (use_sitemap == true)
        {
            char id[OPENHAB_EVENTS_SUBSCRIPTION_LEN];
            size_t len = 0;

            switch (subscribe(id, sizeof(id)))
            {
            case SUBSCRIBE_OK:
                sitemap_mode.store(true, std::memory_order_relaxed);

                /* Sitemap names and page ids are letters, digits and the
                 * underscore -- a group's page is named after its item -- and
                 * the id is hex and dashes, so nothing here needs escaping. */
                if (   url_append(&len, sub_active.website, "/rest/sitemaps/events/", id)
                    && url_append(&len, "?sitemap=", sub_active.sitemap, "")
                    && url_append(&len, "&pageid=", sub_active.page, ""))
                    end = stream(&answered);
                break;

            case SUBSCRIBE_REFUSED:
                ESP_LOGW(TAG, "no sitemap events at %s; following items instead",
                         sub_active.website);
                strlcpy(sitemap_refused, sub_active.website, sizeof(sitemap_refused));
                /* Not a failure of the server's: on to the item stream now. */
                continue;

            case SUBSCRIBE_FAILED:
            default:
                break;
            }
        }
        else if (sub_active.count > 0)
        {
            sitemap_mode.store(false, std::memory_order_relaxed);
            url_build();
            end = stream(&answered);
        }
        else
        {
            /* Nothing on screen to follow by item, and no page either. */
            sitemap_mode.store(false, std::memory_order_relaxed);
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
            continue;
        }

#if CONFIG_OHEZ_DEBUG_OPENHAB_EVENTS
        ESP_LOGI(TAG, "stack_free=%u", (unsigned)uxTaskGetStackHighWaterMark(NULL));
#endif

        if (end == STREAM_CHANGED)
            continue;

        if (answered == true)
            backoff_ms = OPENHAB_EVENTS_BACKOFF_MIN_MS;

        /* Woken early by a new subscription or a reset, both of which are
         * reasons to try at once. */
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(backoff_ms));

        if (answered == false && backoff_ms < OPENHAB_EVENTS_BACKOFF_MAX_MS)
            backoff_ms = (backoff_ms * 2 > OPENHAB_EVENTS_BACKOFF_MAX_MS)
                           ? OPENHAB_EVENTS_BACKOFF_MAX_MS
                           : backoff_ms * 2;
    }
}
