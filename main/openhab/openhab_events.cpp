/**
 * @file openhab_events.cpp
 *
 * See openhab_events.hpp.
 */

#include "sdkconfig.h"

#include "openhab_events.hpp"

#include "openhab_http.hpp"
#include "port/port_sys.h"
#include "sim/sim_offline.hpp"

#include <atomic>
#include <stdlib.h>
#include <string.h>

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

/* The task's own copy, and everything else only it touches. */
static struct openhab_events_subscription_s sub_active;
static esp_http_client_handle_t client;
static struct sse_reader reader;
static char url[OPENHAB_EVENTS_URL_LEN];

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

static bool subscription_equal(const struct openhab_events_subscription_s *a,
                               const struct openhab_events_subscription_s *b)
{
    if (a->count != b->count || strcmp(a->website, b->website) != 0)
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

static void on_data(const char *data, size_t len, void *ctx)
{
    uint64_t *last_data = (uint64_t *)ctx;
    struct openhab_event_s ev;

    switch (openhab_event_parse(data, len, ev.name, sizeof(ev.name),
                                ev.value, sizeof(ev.value)))
    {
    case OPENHAB_EVENT_STATE:
#if CONFIG_OHEZ_DEBUG_OPENHAB_EVENTS
        ESP_LOGI(TAG, "%s = \"%s\"", ev.name, ev.value);
#endif
        /* Never waits. The UI drains this every few milliseconds, so a full
         * queue is a burst the UI has not caught up with yet -- and the cure
         * for a dropped change is the poll a resync brings, not a stream that
         * stops being read. */
        if (xQueueSend(events, &ev, 0) != pdTRUE)
        {
            ESP_LOGW(TAG, "queue full; dropping %s and asking for a resync", ev.name);
            resync.store(true, std::memory_order_relaxed);
        }
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
    return sub_changed.load(std::memory_order_acquire) == true
        || reset_requested.load(std::memory_order_acquire) == true;
}

static bool client_prepare(void)
{
    if (client == NULL)
    {
        esp_http_client_config_t cfg = {};

        cfg.url = url;
        cfg.method = HTTP_METHOD_GET;
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
    esp_http_client_set_header(client, "Accept", "text/event-stream");

    /* A stream ended by the server can leave the tail of its last read cached
     * on the handle; see session_prepare() for what that cost once. */
    esp_http_client_clear_response_buffer(client);

    return true;
}

static enum stream_end_e stream(bool *answered)
{
    *answered = false;

    if (client_prepare() == false)
        return STREAM_FAILED;

    esp_err_t err = esp_http_client_open(client, 0);

    if (err != ESP_OK)
    {
        ESP_LOGW(TAG, "GET %s: %s", url, esp_err_to_name(err));
        esp_http_client_close(client);
        return STREAM_FAILED;
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

    ESP_LOGI(TAG, "streaming %u items", (unsigned)sub_active.count);

    esp_http_client_set_timeout_ms(client, OPENHAB_EVENTS_READ_TIMEOUT_MS);
    sse_reader_reset(&reader);

    /* Whatever changed while there was no stream went unseen. */
    streaming.store(true, std::memory_order_relaxed);
    resync.store(true, std::memory_order_relaxed);

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
            sse_reader_feed(&reader, buf, (size_t)read, on_data, &last_data);
            continue;
        }

        if (read == -ESP_ERR_HTTP_EAGAIN)
        {
            if (port_millis() - last_data < OPENHAB_EVENTS_WATCHDOG_MS)
                continue;

            ESP_LOGW(TAG, "nothing for %u ms; reconnecting", (unsigned)OPENHAB_EVENTS_WATCHDOG_MS);
            break;
        }

        /* 0 is the end of the body -- a server shutting down ends its streams
         * cleanly -- and anything below it a socket that failed. */
        ESP_LOGW(TAG, "stream ended (%d)", read);
        break;
    }

    streaming.store(false, std::memory_order_relaxed);
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
        }

        /* Offline mode answers polls from fixtures, and there is nothing at
         * the other end to stream from. */
        if (sub_active.count == 0 || sim_offline() == true)
        {
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
            continue;
        }

        url_build();

        bool answered;
        enum stream_end_e end = stream(&answered);

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
