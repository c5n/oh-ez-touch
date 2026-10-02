/**
 * @file openhab_bell.cpp
 *
 * See openhab_bell.hpp. The stream itself is openhab_events.cpp's item stream
 * cut down to one topic: the same SSE reader, the same event parser, the same
 * squeeze in front of them, and none of the sitemap half.
 */

#include "sdkconfig.h"

#include "openhab_bell.hpp"

#include "json_squeeze.h"
#include "openhab_event_parse.hpp"
#include "openhab_events.hpp"
#include "openhab_http.hpp"
#include "port/port_sys.h"
#include "sim/sim_offline.hpp"

#include <atomic>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "esp_http_client.h"
#include "esp_log.h"

static const char *TAG = "openhab_bell";

#if CONFIG_IDF_TARGET_LINUX
#define BELL_TASK_STACK_SIZE 32768
#else
#define BELL_TASK_STACK_SIZE 4096
#endif

#define BELL_READ_TIMEOUT_MS 250
/* openHAB sends an "alive" event every ten seconds. */
#define BELL_WATCHDOG_MS     30000
#define BELL_BACKOFF_MIN_MS  5000
#define BELL_BACKOFF_MAX_MS  60000

#define BELL_ITEM_LEN 48
#define BELL_URL_LEN  (OPENHAB_EVENTS_WEBSITE_LEN + 64 + BELL_ITEM_LEN)

struct target_s
{
    char website[OPENHAB_EVENTS_WEBSITE_LEN];
    char item[BELL_ITEM_LEN];
};

/* Written by follow() under the lock, copied out by the task under it. */
static struct target_s wanted;
static std::atomic<bool> changed{false};
static SemaphoreHandle_t lock;
static TaskHandle_t task;

static std::atomic<bool> rang{false};

/* The task's own. */
static struct target_s active;
static esp_http_client_handle_t client;
static struct sse_reader reader;
static char url[BELL_URL_LEN];

static void on_data(const char *data, size_t len, void *ctx)
{
    uint64_t *last_data = (uint64_t *)ctx;
    char name[OPENHAB_EVENT_NAME_LEN];
    char value[OPENHAB_EVENTS_VALUE_LEN];

    *last_data = port_millis();

    if (openhab_event_parse(data, len, name, sizeof(name), value, sizeof(value))
        != OPENHAB_EVENT_STATE)
        return;

    if (strcmp(name, active.item) != 0)
        return;

    ESP_LOGI(TAG, "%s = %s", name, value);

    if (strcmp(value, "ON") == 0 || strcmp(value, "OPEN") == 0)
        rang.store(true, std::memory_order_relaxed);
}

/* One connection, for as long as it lasts. True if the server answered, which
 * is what resets the backoff. */
static bool stream(void)
{
    if (client == NULL)
    {
        esp_http_client_config_t cfg = {};

        cfg.url = url;
        cfg.timeout_ms = OPENHAB_HTTP_TIMEOUT_MS;

        client = esp_http_client_init(&cfg);

        if (client == NULL)
        {
            ESP_LOGE(TAG, "cannot create a client");
            return false;
        }
    }
    else if (esp_http_client_set_url(client, url) != ESP_OK)
    {
        return false;
    }

    const char *authority = strstr(active.website, "://");

    esp_http_client_set_timeout_ms(client, OPENHAB_HTTP_TIMEOUT_MS);
    esp_http_client_set_header(client, "Host", (authority != NULL) ? authority + 3 : active.website);
    esp_http_client_set_header(client, "Accept", "text/event-stream");
    esp_http_client_clear_response_buffer(client);

    if (esp_http_client_open(client, 0) != ESP_OK || esp_http_client_fetch_headers(client) < 0)
    {
        ESP_LOGW(TAG, "GET %s: no answer", url);
        esp_http_client_close(client);
        return false;
    }

    int status = esp_http_client_get_status_code(client);

    if (status != 200)
    {
        ESP_LOGW(TAG, "GET %s: HTTP %d", url, status);
        esp_http_client_close(client);
        return false;
    }

    ESP_LOGI(TAG, "following %s", active.item);

    esp_http_client_set_timeout_ms(client, BELL_READ_TIMEOUT_MS);
    sse_reader_reset(&reader);

    struct json_squeeze_s squeeze;
    uint64_t last_data = port_millis();

    json_squeeze_reset(&squeeze);

    for (;;)
    {
        if (changed.load(std::memory_order_relaxed) == true)
            break;

        char buf[256];
        int read = esp_http_client_read(client, buf, sizeof(buf));

        if (read > 0)
        {
            size_t kept = json_squeeze(&squeeze, buf, (size_t)read);

            sse_reader_feed(&reader, buf, kept, on_data, &last_data);
            sse_reader_take_overflow(&reader);
            continue;
        }

        if (read == -ESP_ERR_HTTP_EAGAIN && port_millis() - last_data < BELL_WATCHDOG_MS)
            continue;

        ESP_LOGW(TAG, "stream ended (%d)", read);
        break;
    }

    esp_http_client_close(client);

    return true;
}

static void bell_task(void *parameter)
{
    (void)parameter;

    uint32_t backoff_ms = BELL_BACKOFF_MIN_MS;

    for (;;)
    {
        if (changed.exchange(false, std::memory_order_acquire) == true)
        {
            xSemaphoreTake(lock, portMAX_DELAY);
            active = wanted;
            xSemaphoreGive(lock);

            backoff_ms = BELL_BACKOFF_MIN_MS;
        }

        if (active.item[0] == '\0' || active.website[0] == '\0' || sim_offline() == true)
        {
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
            continue;
        }

        /* "*" for the root segment, as openhab_events.cpp's url_build()
         * explains; an item name needs no escaping. */
        snprintf(url, sizeof(url), "%s/rest/events?topics=*/items/%s/statechanged",
                 active.website, active.item);

        bool answered = stream();

        if (changed.load(std::memory_order_relaxed) == true)
            continue;

        if (answered == true)
            backoff_ms = BELL_BACKOFF_MIN_MS;

        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(backoff_ms));

        if (answered == false && backoff_ms < BELL_BACKOFF_MAX_MS)
            backoff_ms = (backoff_ms * 2 > BELL_BACKOFF_MAX_MS) ? BELL_BACKOFF_MAX_MS
                                                                : backoff_ms * 2;
    }
}

void openhab_bell_follow(const char *website, const char *item)
{
    /* Compared on the UI task's own copy, so the common call -- nothing
     * changed -- takes no lock. */
    static struct target_s last;

    if (strcmp(last.website, website) == 0 && strcmp(last.item, item) == 0)
        return;

    strlcpy(last.website, website, sizeof(last.website));
    strlcpy(last.item, item, sizeof(last.item));

    if (task == NULL)
    {
        if (item[0] == '\0')
            return;

        lock = xSemaphoreCreateMutex();

        if (lock == NULL
            || xTaskCreate(bell_task, "openhab_bell", BELL_TASK_STACK_SIZE, NULL, 1, &task)
                   != pdPASS)
        {
            ESP_LOGE(TAG, "cannot create the task");
            task = NULL;
            last.item[0] = '\0';
            return;
        }
    }

    xSemaphoreTake(lock, portMAX_DELAY);
    wanted = last;
    xSemaphoreGive(lock);

    changed.store(true, std::memory_order_release);
    xTaskNotifyGive(task);
}

bool openhab_bell_take_ring(void)
{
    return rang.exchange(false, std::memory_order_relaxed);
}
