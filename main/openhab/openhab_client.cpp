/**
 * @file openhab_client.cpp
 *
 * See openhab_client.hpp.
 */

#include "sdkconfig.h"

#include "openhab_client.hpp"

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

    /* Never waits. The only caller is the task that draws, and a queue this
     * deep is only full when something is already badly wrong -- in which case
     * the caller counting a failure is more use than the screen stopping. */
    if (xQueueSend(requests, &req, 0) != pdTRUE)
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

    if (xTaskCreate(openhab_client_task, "openhab_client", OPENHAB_CLIENT_TASK_STACK_SIZE,
                    NULL, OPENHAB_CLIENT_TASK_PRIORITY, NULL) != pdPASS)
    {
        ESP_LOGE(TAG, "cannot create the task");
        goto fail;
    }

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

    return false;
}

uint32_t openhab_client_request_page(const char *url)
{
    /* Bumped here rather than in a call of its own: the generation is the
     * identity of the page being fetched, so a page fetch and a new generation
     * are the same event. Everything queued or in flight for the previous page
     * becomes stale at this line. */
    uint32_t next = generation.load(std::memory_order_relaxed) + 1;

    /* Zero is reserved for the commands that never go stale, so skip it on the
     * wrap. Four billion page loads is not reachable, but a counter that has
     * one forbidden value should say what it does at it. */
    if (next == OPENHAB_CLIENT_GENERATION_ALWAYS)
        next = 1;

    generation.store(next, std::memory_order_relaxed);

    if (submit(OPENHAB_REQ_PAGE, url, NULL, OPENHAB_CLIENT_SLOT_NONE, next) == false)
        return 0;

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

bool openhab_client_poll(struct openhab_result_s *out)
{
    if (results == NULL)
        return false;

    return (xQueueReceive(results, out, 0) == pdTRUE);
}

void openhab_client_result_release(struct openhab_result_s *res)
{
    free(res->payload);

    res->payload = NULL;
    res->payload_len = 0;
}

/* How much of a body of each kind will be read. A page and an icon are
 * rejected when they do not fit, because half of either is worse than none --
 * it would fail to parse or decode somewhere far from the cause. A state is
 * truncated, because it lands in a fixed-width field either way. */
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

static void perform(const struct request_s *req, struct openhab_result_s *res)
{
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

    /* One over, so that the body can be terminated: a caller that treats it as
     * a string -- which the state does -- should not have to. */
    char *body = (char *)malloc(capacity + 1);

    if (body == NULL)
    {
        ESP_LOGE(TAG, "out of memory for a %u byte body", (unsigned)capacity);
        return;
    }

    ssize_t read = openhab_http_get(req->url, body, capacity, truncate);

    if (read < 0)
    {
        /* openhab_http_get() has already logged the method, the URL and the
         * status; there is nothing to add here that it did not say. */
        free(body);
        return;
    }

    body[read] = '\0';

    /* Down to what actually arrived. An icon buffer allocated at 5000 bytes
     * for a 900 byte PNG would otherwise sit at full size for as long as the
     * result is queued. A refusal is not a failure -- the original allocation
     * is still perfectly good. */
    char *shrunk = (char *)realloc(body, (size_t)read + 1);

    res->payload = (shrunk != NULL) ? shrunk : body;
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
