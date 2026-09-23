/**
 * @file openhab_client.cpp
 *
 * See openhab_client.hpp.
 */

#include "sdkconfig.h"

#include "openhab_client.hpp"

#include "icons/icon_set.hpp"
#include "openhab_connector.hpp"
#include "openhab_http.hpp"
#include "sim/icon_fixture.hpp"
#include "sim/sim_offline.hpp"
#include "sim/sitemap_fixture.hpp"

#include <atomic>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "esp_log.h"

static const char *TAG = "openhab_client";

/* The URLs are built by Item and Sitemap, so the width they are built at and
 * the width they are carried at have to agree. Checked rather than shared,
 * because openhab_client.hpp is included by code that has no business pulling
 * in ArduinoJson. */
static_assert(OPENHAB_CLIENT_URL_LEN >= STR_URL_LEN,
              "OPENHAB_CLIENT_URL_LEN is narrower than the URLs Item builds");
static_assert(OPENHAB_CLIENT_BODY_LEN >= STR_STATE_TEXT_LEN,
              "OPENHAB_CLIENT_BODY_LEN cannot hold an item state");

/* A host pthread doing libc and socket work needs far more than a device task
 * does, for the reason sdkconfig.defaults.linux gives twice already: a
 * FreeRTOS stack overflow on the simulator corrupts the ready lists and
 * surfaces as an assertion in vTaskSwitchContext(), a long way from the cause.
 * The device figure is for plain HTTP with no TLS; the same work runs today on
 * the 8 KB main task with LVGL, ArduinoJson and lodepng on it as well. */
#if CONFIG_IDF_TARGET_LINUX
#define OPENHAB_CLIENT_TASK_STACK_SIZE 32768
#else
#define OPENHAB_CLIENT_TASK_STACK_SIZE 4096
#endif

/* Equal to the task that draws, and to beeper_task. The web server's 4 is
 * deliberately higher, because a browser waiting on a response should preempt
 * the screen; a socket wait that may last five seconds should not. Equal
 * priority means the two round-robin under time slicing, so this cannot starve
 * LVGL however long a read takes. */
#define OPENHAB_CLIENT_TASK_PRIORITY 1

/* Copied into the queue whole rather than pointed at: no allocation on the
 * submitting task, no lifetime question, and nothing to free when a request is
 * dropped unmade. The same shape beeper_control.cpp uses. */
struct request_s
{
    enum openhab_request_e type;
    uint32_t generation;
    uint8_t  slot;
    char     url[OPENHAB_CLIENT_URL_LEN];
    char     body[OPENHAB_CLIENT_BODY_LEN];
};

static QueueHandle_t requests = NULL;
static QueueHandle_t results = NULL;

/* The one buffer every body but a state is read into.
 *
 * Sized for the largest class and allocated once, because the alternative was
 * a 12 KB malloc and free on every page load -- see the buffer sizes in the
 * header for what that cost on a weak link.
 *
 * On the heap rather than in BSS, which is the same DRAM taken at a different
 * moment and is deliberately the later one. openhab_client_setup() runs after
 * wlan_setup() and webui_setup(), so a board that cannot spare this says so
 * here, in a log line and a setup that fails, instead of taking it at link
 * time and leaving esp_wifi_init() to fail on a wall panel for want of it.
 *
 * Only perform() touches it, and perform() runs only on the worker below --
 * with one exception: a page result hands it to the UI for as long as that
 * result is unconsumed, which is what payload_static in the result means.
 * page_result_live is the handshake for that exception: set by the worker
 * when it posts a page result, cleared by openhab_client_result_release() on
 * whichever task releases it. The worker waits for it before the buffer is
 * reused -- the generation rules out a second *page* landing in it while the
 * UI parses, but not an icon read. */
static char *rx_buf;

static std::atomic<bool> page_result_live{false};

static_assert(OPENHAB_CLIENT_PAGE_BUFFER_SIZE >= OPENHAB_CLIENT_ICON_BUFFER_SIZE,
              "the receive buffer is sized on the page, so no class may exceed it");
static_assert(OPENHAB_CLIENT_PAGE_BUFFER_SIZE >= OPENHAB_CLIENT_SITEMAPS_BUFFER_SIZE,
              "the receive buffer is sized on the page, so no class may exceed it");

/* Written only by openhab_client_request_page(), on the UI task; read by the
 * worker. An aligned 32-bit access is atomic on both targets anyway, but
 * saying so is what stops the compiler hoisting the worker's read out of its
 * loop. Relaxed because there is no other state to order against it: the value
 * is compared for equality and nothing is published through it. Starts at 1,
 * because 0 is the generation that never goes stale. */
static std::atomic<uint32_t> generation{1};

static void openhab_client_task(void *parameter);

/* True when `request_generation` names a page that is no longer the one being
 * fetched or displayed. Commands are exempt. */
static bool is_stale(uint32_t request_generation)
{
    if (request_generation == OPENHAB_CLIENT_GENERATION_ALWAYS)
        return false;

    return (request_generation != generation.load(std::memory_order_relaxed));
}

static bool submit(enum openhab_request_e type, const char *url, const char *body,
                   uint8_t slot, uint32_t request_generation)
{
    if (requests == NULL)
    {
        ESP_LOGE(TAG, "no client task; dropping request for %s", url);
        return false;
    }

    struct request_s req = {};

    req.type = type;
    req.generation = request_generation;
    req.slot = slot;

    /* Truncation here would send a request that can only fail, so it is
     * reported instead. Item::stateUrl() and Item::iconUrl() already refuse to
     * build a URL this wide, which leaves the sitemap page URL -- assembled
     * from the configured host and sitemap name -- as the one that can reach
     * it. */
    if (strlcpy(req.url, url, sizeof(req.url)) >= sizeof(req.url))
    {
        ESP_LOGE(TAG, "URL longer than %u bytes: %s", (unsigned)sizeof(req.url), url);
        return false;
    }

    if (body != NULL)
        strlcpy(req.body, body, sizeof(req.body));

    /* A page goes to the front of the queue, everything else to the back.
     *
     * Not a preference but the fix for a wait the user sees. The worker is one
     * task and a request in flight cannot be cancelled, so a page submitted
     * behind a queue of state polls waited for all of them -- up to
     * OPENHAB_HTTP_TIMEOUT_MS each, twice over where openhab_http_get()
     * retries -- and on a slow link it could still be waiting when the UI's
     * own answer deadline expired. The polls it overtakes are the previous
     * page's and went stale at the generation bump that came with this
     * submit, so they cost a dequeue each and nothing more.
     *
     * It can also overtake a command, which is the one thing given up here: a
     * queued tap is delayed by one page fetch. It is never dropped -- a
     * command carries _GENERATION_ALWAYS and no page change can stale it --
     * and a tap that is waiting on a page fetch is a panel whose user is
     * watching the page anyway.
     *
     * Never waits, either way. The only caller is the task that draws, and a
     * queue this deep is only full when something is already badly wrong -- in
     * which case the caller counting a failure is more use than the screen
     * stopping. */
    BaseType_t sent = (type == OPENHAB_REQ_PAGE)
                        ? xQueueSendToFront(requests, &req, 0)
                        : xQueueSend(requests, &req, 0);

    if (sent != pdTRUE)
    {
        ESP_LOGW(TAG, "request queue full; dropping %s", url);
        return false;
    }

#if CONFIG_OHEZ_DEBUG_OPENHAB_CLIENT
    ESP_LOGD(TAG, "submit type=%u gen=%u slot=%u %s",
             (unsigned)type, (unsigned)request_generation, (unsigned)slot, url);
#endif

    return true;
}

bool openhab_client_setup(void)
{
    if (requests != NULL)
        return true;

    requests = xQueueCreate(OPENHAB_CLIENT_REQUEST_QUEUE_DEPTH, sizeof(struct request_s));
    results = xQueueCreate(OPENHAB_CLIENT_RESULT_QUEUE_DEPTH, sizeof(struct openhab_result_s));

    if (requests == NULL || results == NULL)
    {
        ESP_LOGE(TAG, "cannot create the queues");
        goto fail;
    }

    /* One over, so that a body read into it can be terminated. */
    rx_buf = (char *)malloc(OPENHAB_CLIENT_PAGE_BUFFER_SIZE + 1);

    if (rx_buf == NULL)
    {
        ESP_LOGE(TAG, "cannot allocate the %u byte receive buffer",
                 (unsigned)OPENHAB_CLIENT_PAGE_BUFFER_SIZE + 1);
        goto fail;
    }

    if (xTaskCreate(openhab_client_task, "openhab_client", OPENHAB_CLIENT_TASK_STACK_SIZE,
                    NULL, OPENHAB_CLIENT_TASK_PRIORITY, NULL) != pdPASS)
    {
        ESP_LOGE(TAG, "cannot create the task");
        goto fail;
    }

    /* Unconditional, and worth the line: whether the icons are in the firmware
     * is the difference between a page load costing one request and costing
     * seven, and it is decided by whether a gitignored generated header was
     * there at compile time. A build that cannot say which it is leaves that to
     * be guessed at from a packet capture. */
    if (icon_set_count() > 0)
        ESP_LOGI(TAG, "built-in icons: %u, %u bytes",
                 (unsigned)icon_set_count(), (unsigned)icon_set_bytes());
    else
        ESP_LOGI(TAG, "no built-in icons; every icon comes from openHAB "
                      "(see tools/build_icon_set.py)");

    return true;

fail:
    /* Leave both handles NULL, so that submit() reports a failure the caller
     * can count rather than queueing into a task that will never run. */
    if (requests != NULL)
    {
        vQueueDelete(requests);
        requests = NULL;
    }

    if (results != NULL)
    {
        vQueueDelete(results);
        results = NULL;
    }

    free(rx_buf);
    rx_buf = NULL;

    return false;
}

uint32_t openhab_client_request_page(const char *url)
{
    /* Bumped here rather than in a call of its own: the generation is the
     * identity of the page being fetched, so a page fetch and a new generation
     * are the same event. Everything queued or in flight for the previous page
     * becomes stale at this line. */
    uint32_t current = generation.load(std::memory_order_relaxed);
    uint32_t next = current + 1;

    /* Zero is reserved for the commands that never go stale, so skip it on the
     * wrap. Four billion page loads is not reachable, but a counter that has
     * one forbidden value should say what it does at it. */
    if (next == OPENHAB_CLIENT_GENERATION_ALWAYS)
        next = 1;

    generation.store(next, std::memory_order_relaxed);

    if (submit(OPENHAB_REQ_PAGE, url, NULL, OPENHAB_CLIENT_SLOT_NONE, next) == false)
    {
        /* Put it back. Nothing was queued, so nothing was superseded -- and a
         * generation left ahead of the one the UI holds staled every request
         * already in flight without the UI ever hearing about it. Those
         * requests produce no result at all, so the icon_pending and
         * state_pending flags that were set for them are never cleared, and
         * the tiles they belong to stop asking for an icon or a state until
         * the next page load. A tile can sit like that for as long as somebody
         * leaves the panel on one page, which is all day. */
        generation.store(current, std::memory_order_relaxed);
        return 0;
    }

    return next;
}

bool openhab_client_request_icon(const char *url, uint8_t slot, uint32_t request_generation)
{
    return submit(OPENHAB_REQ_ICON, url, NULL, slot, request_generation);
}

bool openhab_client_request_state(const char *url, uint8_t slot, uint32_t request_generation)
{
    return submit(OPENHAB_REQ_STATE, url, NULL, slot, request_generation);
}

bool openhab_client_command(const char *url, const char *body)
{
    return submit(OPENHAB_REQ_COMMAND, url, body, OPENHAB_CLIENT_SLOT_NONE,
                  OPENHAB_CLIENT_GENERATION_ALWAYS);
}

bool openhab_client_request_sitemaps(const char *url)
{
    /* _ALWAYS, like a command: the page the tiles are on has nothing to do
     * with this request, and a page load while it is in flight -- which is
     * exactly what a save from the settings screen causes -- must not cancel
     * it. */
    return submit(OPENHAB_REQ_SITEMAPS, url, NULL, OPENHAB_CLIENT_SLOT_NONE,
                  OPENHAB_CLIENT_GENERATION_ALWAYS);
}

bool openhab_client_poll(struct openhab_result_s *out)
{
    if (results == NULL)
        return false;

    if (xQueueReceive(results, out, 0) != pdTRUE)
        return false;

    /* The queue copies the struct, so a payload that rode inside it is at a
     * new address: the worker's pointer names the worker's copy, which is a
     * stack frame that has already returned. Re-aiming it here is what keeps
     * `payload` the only thing any consumer has to know about. */
    if (out->payload_inline == true)
        out->payload = out->body;

    return true;
}

void openhab_client_result_release(struct openhab_result_s *res)
{
    if (res->payload_static == true)
    {
        /* The payload is rx_buf, which belongs to the worker -- "released" here
         * means handed back to it. The worker is waiting on this before it
         * reuses the buffer. */
        page_result_live.store(false, std::memory_order_release);
    }
    else if (res->payload_inline == false)
    {
        free(res->payload);
    }

    res->payload = NULL;
    res->payload_len = 0;
    res->payload_inline = false;
    res->payload_static = false;
}

/* How much of a body of each kind will be read. A page and an icon are
 * rejected when they do not fit, because half of either is worse than none --
 * it would fail to parse or decode somewhere far from the cause. A state is
 * truncated, because it lands in a fixed-width field either way.
 *
 * "Does not fit" means the body is longer than this, and nothing else. A body
 * the network cut short is a failure for every kind, including the truncated
 * one; openhab_http.cpp is where the two are told apart. */
static size_t body_capacity(enum openhab_request_e type, bool *truncate)
{
    switch (type)
    {
    case OPENHAB_REQ_PAGE:
        *truncate = false;
        return OPENHAB_CLIENT_PAGE_BUFFER_SIZE;

    case OPENHAB_REQ_ICON:
        *truncate = false;
        return OPENHAB_CLIENT_ICON_BUFFER_SIZE;

    case OPENHAB_REQ_SITEMAPS:
        *truncate = false;
        return OPENHAB_CLIENT_SITEMAPS_BUFFER_SIZE;

    case OPENHAB_REQ_STATE:
    default:
        *truncate = true;
        return STR_STATE_TEXT_LEN - 1;
    }
}

/* Answer from the compiled-in fixtures instead of the network.
 *
 * Here rather than at the four call sites it used to be at, so that offline
 * mode and a real server are one code path from the UI's point of view: the
 * same submit, the same queue, the same result. */
static void perform_offline(const struct request_s *req, struct openhab_result_s *res)
{
    const char *page = NULL;
    const unsigned char *icon = NULL;
    size_t len = 0;

    switch (req->type)
    {
    case OPENHAB_REQ_PAGE:
        page = sim_sitemap_fixture_get(req->url);

        if (page == NULL)
        {
            ESP_LOGE(TAG, "no fixture page for %s", req->url);
            return;
        }

        len = strlen(page);
        break;

    case OPENHAB_REQ_ICON:
        icon = sim_icon_fixture_get_by_url(req->url, &len);

        /* Not an error, and reported as the success it is: until
         * tools/fetch_sim_icons.py has been run there are no icons at all, and
         * the widgets are drawn without one exactly as they are when a request
         * to a real server 404s. */
        if (icon == NULL)
        {
            res->ok = true;
            return;
        }

        page = (const char *)icon;
        break;

    case OPENHAB_REQ_SITEMAPS:
        page = sim_sitemap_fixture_list();

        if (page == NULL)
        {
            ESP_LOGE(TAG, "no fixture list of sitemaps");
            return;
        }

        len = strlen(page);
        break;

    case OPENHAB_REQ_STATE:
    case OPENHAB_REQ_COMMAND:
    default:
        /* Nowhere to send it, and nothing that would come back changed. The
         * fixture pages carry a fixed state per item, so leaving the item as
         * it is keeps whatever the UI set locally -- which is what makes a
         * switch toggled in offline mode look like it worked. */
        res->ok = true;
        return;
    }

    res->payload = (char *)malloc(len + 1);

    if (res->payload == NULL)
    {
        ESP_LOGE(TAG, "out of memory for a %u byte fixture", (unsigned)len);
        return;
    }

    memcpy(res->payload, page, len);
    res->payload[len] = '\0';
    res->payload_len = len;
    res->ok = true;
}

/* Answer an icon request from the set compiled into the firmware.
 *
 * Copied onto the heap rather than handed out as a pointer into flash, so that
 * a result is a result: openhab_client_result_release() frees every payload it
 * is given, and a second ownership rule -- one flag saying "this one is not
 * yours" -- would have to be got right on every path that drops a result, for
 * the sake of not copying three hundred bytes.
 *
 * Returns false for anything it cannot answer, which is both an icon the set
 * does not have and an allocation that failed; the caller then makes the
 * request it would have made anyway.
 */
static bool perform_builtin_icon(const struct request_s *req, struct openhab_result_s *res)
{
    size_t len = 0;
    const unsigned char *icon = icon_set_get_by_url(req->url, &len);

    if (icon == NULL)
        return false;

    res->payload = (char *)malloc(len + 1);

    if (res->payload == NULL)
    {
        ESP_LOGE(TAG, "out of memory for a %u byte built-in icon", (unsigned)len);
        return false;
    }

    memcpy(res->payload, icon, len);
    res->payload[len] = '\0';
    res->payload_len = len;
    res->ok = true;

#if CONFIG_OHEZ_DEBUG_OPENHAB_CLIENT
    ESP_LOGD(TAG, "built-in icon, %u bytes, for %s", (unsigned)len, req->url);
#endif

    return true;
}

static void perform(const struct request_s *req, struct openhab_result_s *res)
{
    /* The icons the firmware has beat the ones the server would serve, and they
     * beat them before anything else is decided -- including offline mode,
     * whose own fixture is a sixteen-icon subset of this same set.
     *
     * This is the whole of the priority, and it is here rather than in the UI
     * on purpose: a built-in icon and a fetched one then differ in nothing the
     * caller can see. Same submit, same generation, same queue, same PNG in the
     * same result -- the tile simply gets its answer on the next turn of the
     * loop instead of after a round trip.
     *
     * A miss falls through to the network, which is what keeps a custom icon
     * working: $OPENHAB_CONF/icons/classic/ is a per-server thing that no
     * firmware can have been built with. */
    if (req->type == OPENHAB_REQ_ICON && perform_builtin_icon(req, res) == true)
        return;

    if (sim_offline())
    {
        perform_offline(req, res);
        return;
    }

    if (req->type == OPENHAB_REQ_COMMAND)
    {
        res->ok = (openhab_http_post_text(req->url, req->body) == 0);
        return;
    }

    bool truncate = false;
    size_t capacity = body_capacity(req->type, &truncate);

    /* A state is read straight into the result and never touches the heap.
     * body_capacity() returns one less than the field is wide -- the
     * static_assert at the top of this file is what keeps that true -- so the
     * terminator below is inside it. */
    if (req->type == OPENHAB_REQ_STATE)
    {
        ssize_t read = openhab_http_get(req->url, res->body, capacity, truncate);

        if (read < 0)
            return;

        res->body[read] = '\0';
        res->payload = res->body;
        res->payload_len = (size_t)read;
        res->payload_inline = true;
        res->ok = true;
        return;
    }

    /* Everything else into the one buffer. It is a ceiling, not a size: the
     * body that arrives is usually a fraction of it. */
    ssize_t read = openhab_http_get(req->url, rx_buf, capacity, truncate);

    if (read < 0)
    {
        /* openhab_http_get() has already logged the method, the URL and the
         * status; there is nothing to add here that it did not say. */
        return;
    }

    rx_buf[read] = '\0';

    /* A page rides the buffer itself rather than a copy. What that removes is
     * the per-page malloc of up to twelve kilobytes -- the one allocation on
     * this panel big enough to be refused by a fragmented heap, and the one
     * whose refusal is the page that will not load. The buffer is held
     * permanently anyway, exactly one page result is ever live (the generation
     * sees to that), and page_result_live keeps the next request's read out of
     * the buffer until the UI has released this one. */
    if (req->type == OPENHAB_REQ_PAGE)
    {
        res->payload = rx_buf;
        res->payload_len = (size_t)read;
        res->payload_static = true;
        res->ok = true;

        /* Before the result is queued: release orders it against the UI's
         * read of the buffer, the way the queue's own ordering would if the
         * flag travelled inside the struct. */
        page_result_live.store(true, std::memory_order_release);
        return;
    }

    /* Copied out at the size that arrived, and this is the only allocation a
     * request still makes. It has to be an allocation because the result
     * outlives the request: it waits in the queue until the UI has a frame to
     * spare, and the worker is reading the next body by then.
     *
     * What it is not any more is a 12 KB ask followed by a realloc down to
     * two. That pair looked frugal and was the opposite -- it took the largest
     * block the heap had, split it, and handed the tail back, so the ceiling
     * had to be found in one contiguous run on every page load and a hole was
     * left behind each time. This asks for what it needs and keeps it. */
    char *payload = (char *)malloc((size_t)read + 1);

    if (payload == NULL)
    {
        ESP_LOGE(TAG, "out of memory for a %u byte payload", (unsigned)read + 1);
        return;
    }

    memcpy(payload, rx_buf, (size_t)read + 1);

    res->payload = payload;
    res->payload_len = (size_t)read;
    res->ok = true;
}

static void openhab_client_task(void *parameter)
{
    (void)parameter;

    for (;;)
    {
        struct request_s req;

        if (xQueueReceive(requests, &req, portMAX_DELAY) != pdTRUE)
            continue;

        /* Before performing. This is where the saving is: after a page change
         * the icon requests still queued for the old page cost a dequeue each
         * and nothing else -- no socket, no allocation, no five second wait. */
        if (is_stale(req.generation) == true)
        {
#if CONFIG_OHEZ_DEBUG_OPENHAB_CLIENT
            ESP_LOGD(TAG, "dropping unmade gen=%u %s", (unsigned)req.generation, req.url);
#endif
            continue;
        }

        /* A page result the UI has not released yet still holds rx_buf, and
         * every request below but a command and a state reads into it. One
         * spin per loop iteration of the UI is the whole of the wait: the UI
         * releases every result it takes, on every path, so this cannot wedge
         * -- it can only pace an icon behind a page parse, which is the order
         * they are wanted in anyway. */
        if (req.type != OPENHAB_REQ_COMMAND && req.type != OPENHAB_REQ_STATE)
            while (page_result_live.load(std::memory_order_acquire) == true)
                vTaskDelay(1);

        struct openhab_result_s res = {};

        res.type = req.type;
        res.generation = req.generation;
        res.slot = req.slot;

        perform(&req, &res);

        /* And again after. The request just took up to OPENHAB_HTTP_TIMEOUT_MS,
         * which is ample time for the page to have changed underneath it. */
        if (is_stale(req.generation) == true)
        {
#if CONFIG_OHEZ_DEBUG_OPENHAB_CLIENT
            ESP_LOGD(TAG, "dropping stale gen=%u %s", (unsigned)req.generation, req.url);
#endif
            openhab_client_result_release(&res);
            continue;
        }

        /* Blocks when the queue is full, which is the whole of the back
         * pressure: the worker cannot run ahead of the UI and pile up finished
         * payloads. Nothing deadlocks against it, because the UI never waits
         * on the request queue. */
        xQueueSend(results, &res, portMAX_DELAY);

#if CONFIG_OHEZ_DEBUG_OPENHAB_CLIENT
        static UBaseType_t stack_free = 0;
        UBaseType_t stack_free_new = uxTaskGetStackHighWaterMark(NULL);

        if (stack_free_new != stack_free)
        {
            stack_free = stack_free_new;
            ESP_LOGI(TAG, "stack_free=%u", (unsigned)stack_free);
        }
#endif
    }
}
