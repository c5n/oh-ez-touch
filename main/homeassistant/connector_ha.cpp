/**
 * @file connector_ha.cpp
 *
 * connector/connector.hpp for Home Assistant.
 *
 * One task holds one WebSocket connection to /api/websocket (ha_ws.c),
 * authenticated with the long-lived access token. Everything goes over it:
 *
 * - A page is a view of a Lovelace dashboard. lovelace/config sends the whole
 *   dashboard in one message, which can be larger than any buffer the panel
 *   has, so it is parsed while it arrives: the views array is walked one
 *   element at a time, the wanted view is deserialized through a filter
 *   (ha_view_filter()), and of every other view only the title and path are
 *   kept, for the root page's link tiles. ha_view_build() makes the tiles.
 * - The states of the page's entities come from subscribe_entities. Its first
 *   message gives every entity's state and attributes, which decide the tile
 *   types (ha_tile_apply_state()); then the page goes to the UI. The later
 *   messages give changes, which become item events.
 * - A command becomes a call_service message (ha_command_message()).
 * - A state poll of the UI is answered from the task's copy of the states:
 *   the subscription keeps it current.
 * - A change of the dashboard (event lovelace_updated) asks the UI to fetch
 *   the page again.
 *
 * The page is built in the shared work buffer, at its front, with the JSON
 * documents' pool behind it, and handed to the UI as the result's payload
 * (WORK_BUF_HA_READ, then WORK_BUF_HA_RESULT) -- the way the openHAB client
 * hands over a page body. Sitemap::parse() below copies it into the page
 * model.
 *
 * The task reads no Config: connector_configure() copies the four values it
 * needs under a lock.
 */
#include "sdkconfig.h"

#include "connector/connector.hpp"

#include "ha_model.hpp"
#include "ha_ws.h"
#include "icons/icon_set.hpp"
#include "mem/work_buf.h"

#include <ArduinoJson.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "connector";

/* The simulator's host pthread needs far more stack than a device task; see
 * the same note in openhab_client.cpp. On the device ArduinoJson's recursion
 * for the nested cards is the largest user. */
#if CONFIG_IDF_TARGET_LINUX
#define HA_TASK_STACK_SIZE 32768
#else
#define HA_TASK_STACK_SIZE 6144
#endif

/* Equal to the task that draws; see OPENHAB_CLIENT_TASK_PRIORITY. */
#define HA_TASK_PRIORITY 1

#define HA_REQUEST_QUEUE_DEPTH 12
#define HA_RESULT_QUEUE_DEPTH  4
#define HA_EVENT_QUEUE_DEPTH   8

/* Every wait inside one message, and the connect. */
#define HA_IO_TIMEOUT_MS 5000
/* The whole of a page: the dashboard, the subscription and its first state. */
#define HA_PAGE_TIMEOUT_MS 10000
/* After a failed connection. */
#define HA_RETRY_MS 10000
/* A ping when the connection has been quiet this long, and the connection is
 * given up when nothing at all arrived for HA_DEAD_MS. */
#define HA_PING_MS 30000
#define HA_DEAD_MS 75000

/* The tiles and the clock items of one page. */
#define HA_ENTITY_MAX (ITEM_COUNT_MAX + CLOCK_ITEM_COUNT)

static_assert(sizeof(struct ha_page_s) < CONNECTOR_PAGE_BUFFER_SIZE / 2,
              "a page leaves too little of the work buffer for the JSON");
static_assert(HA_ENTITY_LEN == CONNECTOR_EVENT_NAME_LEN, "entity id width");

/* ------------------------------------------------------- the configuration */

static SemaphoreHandle_t config_lock;

static struct
{
    char     host[32];
    int      port;
    char     token[256];
    char     clock_frame[STR_LABEL_LEN];
    uint32_t revision;
} config_copy;

void connector_configure(const Config *config)
{
    if (config_lock == NULL)
        config_lock = xSemaphoreCreateMutex();

    if (config_lock == NULL)
        return;

    xSemaphoreTake(config_lock, portMAX_DELAY);

    bool changed = strcmp(config_copy.host, config->item.homeassistant.hostname) != 0
                || config_copy.port != config->item.homeassistant.port
                || strcmp(config_copy.token, config->item.homeassistant.token) != 0;

    strlcpy(config_copy.host, config->item.homeassistant.hostname, sizeof(config_copy.host));
    config_copy.port = config->item.homeassistant.port;
    strlcpy(config_copy.token, config->item.homeassistant.token, sizeof(config_copy.token));
    strlcpy(config_copy.clock_frame, config->item.backlight.clock_frame,
            sizeof(config_copy.clock_frame));

    if (changed)
        config_copy.revision++;

    xSemaphoreGive(config_lock);
}

struct connector_server_s connector_server(const Config *config)
{
    struct connector_server_s server;

    server.host = config->item.homeassistant.hostname;
    server.port = config->item.homeassistant.port;
    server.root = config->item.homeassistant.dashboard;

    return server;
}

bool connector_root_ref(char *out, size_t out_size, const char *website, const char *root)
{
    (void)website;

    int len = snprintf(out, out_size, "ha:%s", root);

    return (len > 0 && (size_t)len < out_size);
}

const char *connector_name(void)
{
    return "Home Assistant";
}

/* -------------------------------------------------------------- the queues */

enum ha_request_e
{
    HA_REQ_PAGE,
    HA_REQ_STATE,
    HA_REQ_COMMAND,
    HA_REQ_IMAGE,
};

struct ha_request_s
{
    uint8_t  type;       /* enum ha_request_e */
    uint8_t  slot;
    uint32_t generation;
    char     ref[HA_REF_LEN];
    char     body[CONNECTOR_BODY_LEN];
};

static QueueHandle_t requests;
static QueueHandle_t results;
static QueueHandle_t events;

/* The generation of the newest page request, read by the task to drop the
 * polls of a page the user has left. */
static volatile uint32_t page_generation;

/* Set by the task, taken by the UI. */
static volatile bool flag_streaming;
static volatile bool flag_resync;
static volatile bool flag_reload;
static volatile bool flag_link_reset;

static void post_result(struct connector_result_s *res)
{
    /* Blocking when the UI is behind: the queue is the back-pressure, as on
     * the openHAB client. */
    xQueueSend(results, res, portMAX_DELAY);
}

static void post_failure(enum connector_request_e type, uint32_t generation, uint8_t slot)
{
    struct connector_result_s res = {};

    res.type       = type;
    res.generation = generation;
    res.slot       = slot;
    res.ok         = false;
    post_result(&res);
}

/* ---------------------------------------------------- the JSON pool, a bump
 * allocator over the part of the work buffer the page does not use. Blocks
 * are only ever freed all at once (begin()); reallocate() grows the newest
 * block in place and copies an older one. */

class HaArena : public ArduinoJson::Allocator
{
  public:
    void begin(void *mem, size_t size)
    {
        base = (uint8_t *)mem;
        cap  = size;
        used = 0;
        last = NULL;
        last_size = 0;
    }

    void *allocate(size_t size) override
    {
        size = (size + 7u) & ~(size_t)7u;

        if (used + size > cap)
            return NULL;

        last = base + used;
        last_size = size;
        used += size;

        return last;
    }

    void deallocate(void *ptr) override
    {
        if (ptr == last)
        {
            used -= last_size;
            last = NULL;
            last_size = 0;
        }
    }

    void *reallocate(void *ptr, size_t size) override
    {
        size = (size + 7u) & ~(size_t)7u;

        if (ptr == last)
        {
            size_t start = (size_t)((uint8_t *)ptr - base);

            if (start + size > cap)
                return NULL;

            used = start + size;
            last_size = size;

            return ptr;
        }

        void *fresh = allocate(size);

        /* The old block's size is not kept; a shrink never copies past it,
         * and a grow of an older block copies what the new size allows,
         * which reads into the blocks after it -- harmless, they are ours. */
        if (fresh != NULL && ptr != NULL)
            memmove(fresh, ptr, size);

        return fresh;
    }

    size_t peak() const { return used; }

  private:
    uint8_t *base = NULL;
    size_t   cap = 0;
    size_t   used = 0;
    uint8_t *last = NULL;
    size_t   last_size = 0;
};

static HaArena arena;

/* ------------------------------------------------------------ the messages */

static struct ha_ws_s ws;
static uint32_t       next_id = 1;
static uint64_t       last_rx_ms;

static uint64_t now_ms(void)
{
    return (uint64_t)xTaskGetTickCount() * portTICK_PERIOD_MS;
}

/* One message as a byte stream for ArduinoJson: first the few bytes already
 * read to learn its id and type, then the rest from the socket. One byte can
 * be pushed back, for the hand-walked parts of lovelace/config. */
class MessageReader
{
  public:
    void begin(const char *prefix, size_t prefix_len)
    {
        memcpy(head, prefix, prefix_len);
        head_len = prefix_len;
        head_pos = 0;
        pushed = -1;
        failed = false;
    }

    int read()
    {
        if (pushed >= 0)
        {
            int c = pushed;
            pushed = -1;
            return c;
        }

        if (head_pos < head_len)
            return (unsigned char)head[head_pos++];

        char c;
        int  n = ha_ws_read(&ws, &c, 1);

        if (n <= 0)
        {
            failed = (n < 0);
            return -1;
        }

        return (unsigned char)c;
    }

    size_t readBytes(char *buf, size_t len)
    {
        size_t done = 0;

        while (done < len && (pushed >= 0 || head_pos < head_len))
        {
            int c = read();

            if (c < 0)
                return done;

            buf[done++] = (char)c;
        }

        while (done < len)
        {
            int n = ha_ws_read(&ws, buf + done, len - done);

            if (n <= 0)
            {
                failed = (n < 0);
                break;
            }

            done += (size_t)n;
        }

        return done;
    }

    void unread(int c) { pushed = c; }

    /* Read up to and including `token`. False at the end of the message. */
    bool find(const char *token)
    {
        size_t len = strlen(token);
        size_t match = 0;

        for (;;)
        {
            int c = read();

            if (c < 0)
                return false;

            if (c == token[match])
            {
                if (++match == len)
                    return true;
            }
            else
            {
                match = (c == token[0]) ? 1 : 0;
            }
        }
    }

    /* The next byte that is not white space, consumed; -1 at the end. */
    int next_token()
    {
        for (;;)
        {
            int c = read();

            if (c != ' ' && c != '\n' && c != '\r' && c != '\t')
                return c;
        }
    }

    bool failed = false;

  private:
    char   head[64];
    size_t head_len = 0;
    size_t head_pos = 0;
    int    pushed = -1;
};

static MessageReader reader;

/* What a message is: its "id" and "type", read from its first bytes. Home
 * Assistant puts both first ({"id":5,"type":"event",...}); a message without
 * an id (auth) has id 0. */
struct message_s
{
    uint32_t id;
    char     type[24];
    bool     refused; /* a result with "success":false */
};

static void scan_head(const char *head, struct message_s *m)
{
    const char *id = strstr(head, "\"id\":");
    const char *type = strstr(head, "\"type\":\"");

    m->id = (id != NULL) ? (uint32_t)strtoul(id + 5, NULL, 10) : 0;
    m->type[0] = '\0';
    m->refused = (strstr(head, "\"success\":false") != NULL);

    if (type != NULL)
    {
        type += 8;
        const char *end = strchr(type, '"');

        if (end != NULL && (size_t)(end - type) < sizeof(m->type))
        {
            memcpy(m->type, type, (size_t)(end - type));
            m->type[end - type] = '\0';
        }
    }
}

/* Wait for the next message and learn its id and type. The message body is
 * then read through `reader`, or skipped. 1, 0 for none in time, -1 failed. */
static int next_message(uint32_t timeout_ms, struct message_s *m)
{
    int rc = ha_ws_begin_message(&ws, timeout_ms);

    if (rc <= 0)
        return rc;

    last_rx_ms = now_ms();

    char   head[64];
    size_t len = 0;

    /* Enough for the id and the type, and no further: the rest stays in the
     * socket for whoever reads the message. */
    while (len < 48)
    {
        int n = ha_ws_read(&ws, head + len, 48 - len);

        if (n < 0)
            return -1;
        if (n == 0)
            break;

        len += (size_t)n;
    }

    head[len] = '\0';
    scan_head(head, m);
    reader.begin(head, len);

    return 1;
}

static bool send_json(JsonDocument &doc)
{
    char   buf[512];
    size_t len = serializeJson(doc, buf, sizeof(buf));

    if (len == 0 || len >= sizeof(buf))
        return false;

    return ha_ws_send_text(&ws, buf, len);
}

/* --------------------------------------------------------- the entity cache */

struct ha_slot_s
{
    struct ha_entity_s e;
    char               panel[STR_STATE_TEXT_LEN]; /* last state given to the UI */
};

static struct ha_slot_s slots[HA_ENTITY_MAX];
static size_t           slot_count;

/* The subscription that feeds them, and the one for dashboard changes. */
static uint32_t entities_sub_id;
static uint32_t lovelace_sub_id;

static struct ha_slot_s *slot_find(const char *id)
{
    for (size_t i = 0; i < slot_count; i++)
        if (strcmp(slots[i].e.id, id) == 0)
            return &slots[i];

    return NULL;
}

static void post_item_event(struct ha_slot_s *s)
{
    char panel[STR_STATE_TEXT_LEN];

    ha_entity_panel_state(&s->e, panel, sizeof(panel));


    if (strcmp(panel, s->panel) == 0)
        return;

    strlcpy(s->panel, panel, sizeof(s->panel));

    struct connector_event_s ev = {};

    ev.kind = CONNECTOR_EVENT_KIND_ITEM;
    strlcpy(ev.item.name, s->e.id, sizeof(ev.item.name));
    strlcpy(ev.item.value, panel, sizeof(ev.item.value));

    /* A full queue loses the change; the resync polls answer from the cache,
     * which has it. */
    if (xQueueSend(events, &ev, 0) != pdTRUE)
        flag_resync = true;
}

/* The filter for an event message: its "id", and of a subscribe_entities
 * event the state and the attributes ha_model.cpp reads, of additions ("a")
 * and changes ("c"). Any other event keeps its id only.
 *
 * The id has to come from the document: Home Assistant puts it first in a
 * result and in a subscription's first event, and last in every change
 * ({"type":"event","event":{...},"id":10}). */
static void event_filter(JsonDocument &filter)
{
    static const char *const attrs[] = {
        "friendly_name", "unit_of_measurement", "device_class", "brightness",
        "supported_color_modes", "hs_color", "current_position", "temperature",
        "temperature_unit", "min_temp", "max_temp", "target_temp_step", "min", "max",
        "step", "mode", "options",
    };

    filter.clear();
    filter["id"] = true;

    JsonObject add = filter["event"]["a"]["*"].to<JsonObject>();
    JsonObject chg = filter["event"]["c"]["*"]["+"].to<JsonObject>();

    add["s"] = true;
    chg["s"] = true;

    for (const char *a : attrs)
    {
        add["a"][a] = true;
        chg["a"][a] = true;
    }

    filter["event"]["c"]["*"]["-"]["a"] = true;
}

/* Parse the rest of an event message into the pool at `mem`. */
static bool parse_event(JsonDocument &doc, void *mem, size_t size)
{
    JsonDocument filter;

    event_filter(filter);
    arena.begin(mem, size);

    DeserializationError err =
        deserializeJson(doc, reader, DeserializationOption::Filter(filter));

    ha_ws_skip_message(&ws);

    if (err)
    {
        ESP_LOGW(TAG, "event: %s (pool %u of %u)", err.c_str(), (unsigned)arena.peak(),
                 (unsigned)size);
        return false;
    }

    return true;
}

/* A parsed event: a change of the page's entities goes into the cache and to
 * the UI, a dashboard change asks for a reload, anything else is old. */
static void apply_event(JsonDocument &doc)
{
    uint32_t id = doc["id"] | 0u;

    if (id != 0 && id == lovelace_sub_id)
    {
        ESP_LOGI(TAG, "dashboard changed");
        flag_reload = true;
        return;
    }

    if (id == 0 || id != entities_sub_id)
        return;

    for (JsonPairConst kv : doc["event"]["a"].as<JsonObjectConst>())
    {
        struct ha_slot_s *s = slot_find(kv.key().c_str());

        if (s == NULL)
            continue;

        /* A full state, after a reconnect: everything is replaced but what the
         * page decided about the tile. The id is copied out first, since the
         * reset clears it before it copies. */
        char    id_copy[HA_ENTITY_LEN];
        uint8_t type = s->e.type;
        uint8_t words = s->e.binary_words;

        strlcpy(id_copy, s->e.id, sizeof(id_copy));
        ha_entity_reset(&s->e, id_copy);
        s->e.type = type;
        s->e.binary_words = words;
        ha_entity_update(&s->e, kv.value(), JsonVariantConst());
        post_item_event(s);
    }

    for (JsonPairConst kv : doc["event"]["c"].as<JsonObjectConst>())
    {
        struct ha_slot_s *s = slot_find(kv.key().c_str());

        if (s == NULL)
            continue;

        ha_entity_update(&s->e, kv.value()["+"], kv.value()["-"]);
        post_item_event(s);
    }
}

/* A result nobody waits for: a command's answer. A refused command is worth
 * a log line. */
static void log_result(const struct message_s *m)
{
    JsonDocument doc;
    JsonDocument filter;

    filter["success"] = true;
    filter["error"]["message"] = true;

    if (!deserializeJson(doc, reader, DeserializationOption::Filter(filter))
        && doc["success"] == false)
        ESP_LOGW(TAG, "message %u refused: %s", (unsigned)m->id, doc["error"]["message"] | "?");

    ha_ws_skip_message(&ws);
}

/* Any message, outside a page load or inside one. `pool` is the memory an
 * event is parsed into: inside a page load the part of the work buffer the
 * page does not use, outside one NULL, and the work buffer is claimed. */
static void dispatch(const struct message_s *m, void *pool, size_t pool_size)
{
    if (strcmp(m->type, "result") == 0)
    {
        log_result(m);
        return;
    }

    if (strcmp(m->type, "event") != 0)
    {
        ha_ws_skip_message(&ws);
        return;
    }

    bool claimed = false;

    if (pool == NULL)
    {
        pool = work_buf_claim(WORK_BUF_HA_READ);
        pool_size = work_buf_size();
        claimed = true;

        if (pool == NULL)
        {
            ha_ws_skip_message(&ws);
            return;
        }
    }

    {
        JsonDocument doc(&arena);

        if (parse_event(doc, pool, pool_size))
            apply_event(doc);
    }

    if (claimed)
        work_buf_release(WORK_BUF_HA_READ);
}

/* ------------------------------------------------------------- connecting */

static void unsubscribe(uint32_t *sub_id)
{
    if (*sub_id == 0)
        return;

    JsonDocument doc;

    doc["id"] = next_id++;
    doc["type"] = "unsubscribe_events";
    doc["subscription"] = *sub_id;
    send_json(doc);
    *sub_id = 0;
}

static bool subscribe_entities(void)
{
    JsonDocument doc;

    entities_sub_id = next_id++;
    doc["id"] = entities_sub_id;
    doc["type"] = "subscribe_entities";

    JsonArray ids = doc["entity_ids"].to<JsonArray>();

    for (size_t i = 0; i < slot_count; i++)
        ids.add(slots[i].e.id);

    return send_json(doc);
}

/* Read the next message, which has to be a small one, into `doc`. */
static bool read_small(JsonDocument &doc, uint32_t timeout_ms)
{
    struct message_s m;

    if (next_message(timeout_ms, &m) <= 0)
        return false;

    DeserializationError err = deserializeJson(doc, reader);

    ha_ws_skip_message(&ws);

    return !err;
}

static uint32_t connected_revision;

static bool connect_and_auth(void)
{
    char     host[32];
    char     token[256];
    int      port;
    uint32_t revision;

    xSemaphoreTake(config_lock, portMAX_DELAY);
    strlcpy(host, config_copy.host, sizeof(host));
    strlcpy(token, config_copy.token, sizeof(token));
    port = config_copy.port;
    revision = config_copy.revision;
    xSemaphoreGive(config_lock);

    if (host[0] == '\0' || token[0] == '\0')
    {
        ESP_LOGW(TAG, "no Home Assistant host or access token configured");
        return false;
    }

    if (ha_ws_connect(&ws, host, (uint16_t)port, "/api/websocket", HA_IO_TIMEOUT_MS) == false)
        return false;

    JsonDocument doc;

    if (read_small(doc, HA_IO_TIMEOUT_MS) == false || doc["type"] != "auth_required")
    {
        ESP_LOGW(TAG, "%s:%d sent no auth_required", host, port);
        ha_ws_close(&ws);
        return false;
    }

    doc.clear();
    doc["type"] = "auth";
    doc["access_token"] = token;

    if (send_json(doc) == false)
        return false;

    doc.clear();

    if (read_small(doc, HA_IO_TIMEOUT_MS) == false || doc["type"] != "auth_ok")
    {
        ESP_LOGW(TAG, "%s:%d refused the access token", host, port);
        ha_ws_close(&ws);
        return false;
    }

    ESP_LOGI(TAG, "connected to Home Assistant %s at %s:%d", doc["ha_version"] | "?", host,
             port);

    connected_revision = revision;
    next_id = 1;
    last_rx_ms = now_ms();

    /* A dashboard edited in Home Assistant reloads the page. */
    doc.clear();
    lovelace_sub_id = next_id++;
    doc["id"] = lovelace_sub_id;
    doc["type"] = "subscribe_events";
    doc["event_type"] = "lovelace_updated";
    send_json(doc);

    /* The page on screen keeps its tiles; its entities are listened for
     * again, and the UI polls them once. */
    entities_sub_id = 0;

    if (slot_count > 0)
        subscribe_entities();

    flag_streaming = true;
    flag_resync = true;

    return true;
}

static void disconnect(void)
{
    ha_ws_close(&ws);
    entities_sub_id = 0;
    lovelace_sub_id = 0;
    flag_streaming = false;
}

/* --------------------------------------------------------------- a page */

/* Wait for the result of message `id`, or with `want_event` for the first
 * event of subscription `id`, which is then parsed into `doc`. Everything else
 * that arrives meanwhile is handled with `pool` as the events' memory: the
 * page holds the work buffer, so nothing here may claim it.
 *
 * 1 found (a result's body unread), 0 timeout, -1 failed. */
static int await(uint32_t id, bool want_event, uint64_t deadline, void *pool, size_t pool_size,
                 JsonDocument &doc, struct message_s *m)
{
    for (;;)
    {
        uint64_t now = now_ms();

        if (now >= deadline)
            return 0;

        int rc = next_message((uint32_t)(deadline - now), m);

        if (rc <= 0)
            return rc;

        if (strcmp(m->type, "event") == 0)
        {
            doc.clear();

            if (parse_event(doc, pool, pool_size) == false)
                continue;

            if (want_event && (doc["id"] | 0u) == id)
                return 1;

            apply_event(doc);
            doc.clear();
            continue;
        }

        if (want_event == false && m->id == id && strcmp(m->type, "result") == 0)
            return 1;

        dispatch(m, pool, pool_size);
    }
}

/* Walk lovelace/config's views and build the wanted one into `page`. The
 * body of the result is read through `reader`. */
static bool read_dashboard(struct ha_page_s *page, const char *want_view, void *pool,
                           size_t pool_size)
{
    struct ha_view_s views[HA_VIEW_MAX];
    size_t           view_count = 0;
    bool             found = false;
    bool             root = false;
    char             clock_frame[STR_LABEL_LEN];

    xSemaphoreTake(config_lock, portMAX_DELAY);
    strlcpy(clock_frame, config_copy.clock_frame, sizeof(clock_frame));
    xSemaphoreGive(config_lock);

    /* The wanted view is built while its document exists. The root page's
     * links to the views after it are added when the walk has seen them. */
    if (reader.find("\"views\"") == false || reader.next_token() != ':'
        || reader.next_token() != '[')
    {
        ESP_LOGW(TAG, "the dashboard answer has no views");
        return false;
    }

    JsonDocument full_filter;
    JsonDocument summary_filter;

    ha_view_filter(full_filter);
    ha_view_summary_filter(summary_filter);

    int c = reader.next_token();

    if (c != ']')
        reader.unread(c);

    for (size_t index = 0; c != ']'; index++)
    {
        arena.begin(pool, pool_size);

        JsonDocument doc(&arena);
        bool         wanted = (found == false);

        /* Which view is wanted is known only from its path, which is inside
         * it: every view is read with the full filter until the wanted one
         * has been found, and with the small one after it. */
        DeserializationError err = deserializeJson(
            doc, reader, DeserializationOption::Filter(wanted ? full_filter : summary_filter));

        if (err)
        {
            ESP_LOGW(TAG, "dashboard view %u: %s (pool %u of %u)", (unsigned)index, err.c_str(),
                     (unsigned)arena.peak(), (unsigned)pool_size);
            return false;
        }

        if (view_count < HA_VIEW_MAX)
            ha_view_summary(doc.as<JsonVariantConst>(), index, &views[view_count++]);

        if (wanted && ha_view_matches(want_view, index, doc["path"].as<const char *>()))
        {
            found = true;
            root = (index == 0);
            /* Built now, while the document exists; the link tiles that need
             * the later views are added below. */
            ha_view_build(doc.as<JsonVariantConst>(), root, clock_frame, views, view_count,
                          page);
        }

        doc.clear();

        c = reader.next_token();

        if (c != ',' && c != ']')
        {
            ESP_LOGW(TAG, "dashboard: unexpected '%c' between views", (c > 0) ? c : '?');
            return false;
        }
    }

    if (found == false)
    {
        ESP_LOGW(TAG, "the dashboard has no view \"%s\"", want_view);
        return false;
    }

    /* The root page's links to the views after it, which the walk had not
     * seen when the page was built. */
    if (root)
        ha_page_add_view_links(page, views, view_count);
    else if (view_count > 0)
    {
        /* The back tile's name: the root view's title. */
        if (page->tile_count > 0 && page->tiles[0].kind == HA_TILE_BACK)
            strlcpy(page->tiles[0].name, views[0].title, sizeof(page->tiles[0].name));
    }

    return true;
}

static void slots_from_page(const struct ha_page_s *page)
{
    slot_count = 0;

    const struct ha_tile_s *lists[2] = {page->tiles, page->clock};
    size_t                  counts[2] = {page->tile_count, page->clock_count};

    for (size_t l = 0; l < 2; l++)
    {
        for (size_t i = 0; i < counts[l]; i++)
        {
            const struct ha_tile_s *t = &lists[l][i];

            if (t->kind != HA_TILE_ENTITY || slot_find(t->entity) != NULL
                || slot_count >= HA_ENTITY_MAX)
                continue;

            memset(&slots[slot_count], 0, sizeof(slots[slot_count]));
            ha_entity_reset(&slots[slot_count].e, t->entity);
            slot_count++;
        }
    }
}

static void page_apply_states(struct ha_page_s *page, JsonVariantConst added)
{
    struct ha_tile_s *lists[2] = {page->tiles, page->clock};
    size_t            counts[2] = {page->tile_count, page->clock_count};

    for (size_t l = 0; l < 2; l++)
    {
        for (size_t i = 0; i < counts[l]; i++)
        {
            struct ha_tile_s *t = &lists[l][i];

            if (t->kind != HA_TILE_ENTITY)
                continue;

            struct ha_slot_s *s = slot_find(t->entity);
            struct ha_entity_s scratch;

            ha_tile_apply_state(t, added[t->entity], (s != NULL) ? &s->e : &scratch);

            if (s != NULL)
                strlcpy(s->panel, t->state, sizeof(s->panel));
        }
    }
}

static void load_page(const struct ha_request_s *req)
{
    char dashboard[HA_REF_LEN];
    char view[HA_VIEW_LEN];

    /* "ha:<dashboard>[/<view>]" */
    const char *p = (strncmp(req->ref, "ha:", 3) == 0) ? req->ref + 3 : req->ref;
    const char *slash = strchr(p, '/');
    size_t      dash_len = (slash != NULL) ? (size_t)(slash - p) : strlen(p);

    if (dash_len >= sizeof(dashboard))
        dash_len = sizeof(dashboard) - 1;

    memcpy(dashboard, p, dash_len);
    dashboard[dash_len] = '\0';
    ha_ref_view(req->ref, view, sizeof(view));

    struct ha_page_s *page = (struct ha_page_s *)work_buf_claim(WORK_BUF_HA_READ);

    if (page == NULL)
    {
        post_failure(CONNECTOR_REQ_PAGE, req->generation, CONNECTOR_SLOT_NONE);
        return;
    }

    memset(page, 0, sizeof(*page));
    strlcpy(page->ref, req->ref, sizeof(page->ref));

    uint8_t *pool = (uint8_t *)page + ((sizeof(*page) + 7u) & ~(size_t)7u);
    size_t   pool_size = work_buf_size() - (size_t)(pool - (uint8_t *)page);
    uint64_t deadline = now_ms() + HA_PAGE_TIMEOUT_MS;
    struct message_s m;
    bool     ok = false;

    JsonDocument doc;
    uint32_t     id = next_id++;

    doc["id"] = id;
    doc["type"] = "lovelace/config";

    if (dashboard[0] != '\0')
        doc["url_path"] = dashboard;

    JsonDocument event(&arena);

    if (send_json(doc) == false
        || await(id, false, deadline, pool, pool_size, event, &m) != 1)
    {
        ESP_LOGW(TAG, "no dashboard \"%s\"", dashboard[0] ? dashboard : "(default)");
        goto done;
    }

    if (m.refused)
    {
        /* config_not_found: no such dashboard, or one Home Assistant
         * generates itself and keeps no configuration for. */
        ESP_LOGW(TAG, "Home Assistant has no configuration for dashboard \"%s\"",
                 dashboard[0] ? dashboard : "(default)");
        ha_ws_skip_message(&ws);
        goto done;
    }

    if (read_dashboard(page, view, pool, pool_size) == false)
    {
        ha_ws_skip_message(&ws);
        goto done;
    }

    ha_ws_skip_message(&ws);

    /* Listen for the new page's entities, and no longer for the old one's. */
    unsubscribe(&entities_sub_id);
    slots_from_page(page);

    if (slot_count > 0)
    {
        if (subscribe_entities() == false
            || await(entities_sub_id, false, deadline, pool, pool_size, event, &m) != 1)
            goto done;

        ha_ws_skip_message(&ws);

        if (await(entities_sub_id, true, deadline, pool, pool_size, event, &m) != 1)
        {
            ESP_LOGW(TAG, "no states for the page's entities");
            goto done;
        }

        page_apply_states(page, event["event"]["a"]);
        event.clear();
    }

    ok = true;

done:
    if (ok == false)
    {
        work_buf_release(WORK_BUF_HA_READ);
        post_failure(CONNECTOR_REQ_PAGE, req->generation, CONNECTOR_SLOT_NONE);

        if (ha_ws_is_open(&ws) == false)
            disconnect();

        return;
    }

    struct connector_result_s res = {};

    res.type           = CONNECTOR_REQ_PAGE;
    res.generation     = req->generation;
    res.slot           = CONNECTOR_SLOT_NONE;
    res.ok             = true;
    res.payload        = (char *)page;
    res.payload_len    = sizeof(*page);
    res.payload_static = true;

    work_buf_handover(WORK_BUF_HA_READ, WORK_BUF_HA_RESULT);
    post_result(&res);

    /* A page load is the deepest the task's stack goes (ArduinoJson over the
     * nested cards). Logged at each new low, so a bench run says whether
     * HA_TASK_STACK_SIZE has to grow. */
    static UBaseType_t stack_low = (UBaseType_t)-1;
    UBaseType_t        stack_now = uxTaskGetStackHighWaterMark(NULL);

    if (stack_now < stack_low)
    {
        stack_low = stack_now;
        ESP_LOGI(TAG, "page \"%s\": %u tiles, stack low water %u", page->ref,
                 (unsigned)page->tile_count, (unsigned)stack_now);
    }
}

/* ------------------------------------------------------------ the requests */

static void answer_state(const struct ha_request_s *req)
{
    struct connector_result_s res = {};
    struct ha_slot_s *s = slot_find(req->ref);

    res.type       = CONNECTOR_REQ_STATE;
    res.generation = req->generation;
    res.slot       = req->slot;

    if (s != NULL)
    {
        ha_entity_panel_state(&s->e, res.body, sizeof(res.body));
        res.ok             = true;
        res.payload        = res.body;
        res.payload_len    = strlen(res.body);
        res.payload_inline = true;
    }

    /* By value through the queue: connector_poll() points the payload at the
     * receiver's copy of the body. */
    post_result(&res);
}

static void send_command(const struct ha_request_s *req)
{
    struct ha_slot_s  *s = slot_find(req->ref);
    struct ha_entity_s scratch;
    char               msg[384];

    if (s == NULL)
        ha_entity_reset(&scratch, req->ref);

    size_t len = ha_command_message((s != NULL) ? &s->e : &scratch, req->body, next_id++, msg,
                                    sizeof(msg));

    if (len == 0)
    {
        ESP_LOGW(TAG, "no service for \"%s\" on %s", req->body, req->ref);
        return;
    }

    if (ha_ws_send_text(&ws, msg, len) == false)
        disconnect();
}

static void handle_request(const struct ha_request_s *req)
{
    bool open = ha_ws_is_open(&ws);

    switch (req->type)
    {
    case HA_REQ_PAGE:
        /* Only the newest page is worth loading. */
        if (req->generation != page_generation)
            post_failure(CONNECTOR_REQ_PAGE, req->generation, CONNECTOR_SLOT_NONE);
        else if (open)
            load_page(req);
        else
            post_failure(CONNECTOR_REQ_PAGE, req->generation, CONNECTOR_SLOT_NONE);
        break;

    case HA_REQ_STATE:
        if (req->generation == page_generation)
            answer_state(req);
        break;

    case HA_REQ_COMMAND:
        if (open)
            send_command(req);
        break;

    case HA_REQ_IMAGE:
    default:
        post_failure(CONNECTOR_REQ_IMAGE, CONNECTOR_GENERATION_ALWAYS, req->slot);
        break;
    }
}

/* ----------------------------------------------------------------- the task */

static void ha_task(void *arg)
{
    (void)arg;

    uint64_t retry_at = 0;
    uint64_t ping_at = 0;

    for (;;)
    {
        uint32_t revision;

        xSemaphoreTake(config_lock, portMAX_DELAY);
        revision = config_copy.revision;
        xSemaphoreGive(config_lock);

        if (ha_ws_is_open(&ws) && (flag_link_reset || revision != connected_revision))
        {
            ESP_LOGI(TAG, "reconnecting");
            disconnect();
            retry_at = 0;
        }

        flag_link_reset = false;

        if (ha_ws_is_open(&ws) == false && now_ms() >= retry_at)
        {
            if (connect_and_auth() == false)
            {
                disconnect();
                retry_at = now_ms() + HA_RETRY_MS;
            }

            ping_at = now_ms() + HA_PING_MS;
        }

        struct ha_request_s req;

        if (xQueueReceive(requests, &req, 0) == pdTRUE)
        {
            handle_request(&req);
            continue;
        }

        if (ha_ws_is_open(&ws) == false)
        {
            /* Nothing to read; wait for a request instead. */
            if (xQueueReceive(requests, &req, pdMS_TO_TICKS(250)) == pdTRUE)
                handle_request(&req);
            continue;
        }

        struct message_s m;
        int              rc = next_message(50, &m);

        if (rc > 0)
            dispatch(&m, NULL, 0);
        else if (rc < 0)
            disconnect();

        if (ha_ws_is_open(&ws) && now_ms() >= ping_at)
        {
            if (now_ms() - last_rx_ms > HA_DEAD_MS)
            {
                ESP_LOGW(TAG, "no answer for %u s; reconnecting", (unsigned)(HA_DEAD_MS / 1000));
                disconnect();
                continue;
            }

            JsonDocument doc;

            doc["id"] = next_id++;
            doc["type"] = "ping";
            send_json(doc);
            ping_at = now_ms() + HA_PING_MS;
        }
    }
}

bool connector_setup(void)
{
    if (config_lock == NULL)
        config_lock = xSemaphoreCreateMutex();

    requests = xQueueCreate(HA_REQUEST_QUEUE_DEPTH, sizeof(struct ha_request_s));
    results  = xQueueCreate(HA_RESULT_QUEUE_DEPTH, sizeof(struct connector_result_s));
    events   = xQueueCreate(HA_EVENT_QUEUE_DEPTH, sizeof(struct connector_event_s));

    if (config_lock == NULL || requests == NULL || results == NULL || events == NULL)
    {
        ESP_LOGE(TAG, "no queues for the Home Assistant connector");
        return false;
    }

    if (work_buf_size() < sizeof(struct ha_page_s) * 2)
    {
        ESP_LOGE(TAG, "the work buffer is too small for a page");
        return false;
    }

    ha_ws_init(&ws);

    if (xTaskCreate(ha_task, "ha_client", HA_TASK_STACK_SIZE, NULL, HA_TASK_PRIORITY, NULL)
        != pdPASS)
    {
        ESP_LOGE(TAG, "cannot create the Home Assistant task");
        return false;
    }

    return true;
}

void connector_link_reset(void)
{
    flag_link_reset = true;
}

void connector_loop(void)
{
}

/* ------------------------------------------------------- the UI's requests */

static bool submit(uint8_t type, const char *ref, const char *body, uint8_t slot,
                   uint32_t generation, bool front)
{
    if (requests == NULL)
        return false;

    struct ha_request_s req = {};

    req.type = type;
    req.slot = slot;
    req.generation = generation;
    strlcpy(req.ref, ref ? ref : "", sizeof(req.ref));
    strlcpy(req.body, body ? body : "", sizeof(req.body));

    return (front ? xQueueSendToFront(requests, &req, 0) : xQueueSend(requests, &req, 0))
           == pdTRUE;
}

uint32_t connector_request_page(const char *ref)
{
    uint32_t next = page_generation + 1;

    if (next == CONNECTOR_GENERATION_ALWAYS)
        next++;

    page_generation = next;

    /* To the front, like the openHAB client's page: the polls it overtakes
     * belong to the page being left. */
    if (submit(HA_REQ_PAGE, ref, NULL, CONNECTOR_SLOT_NONE, next, true) == false)
        return 0;

    return next;
}

bool connector_request_icon(const char *ref, uint8_t slot, uint32_t generation)
{
    /* Home Assistant serves no icon pictures: a tile draws the panel's own set
     * (Item::iconUrl() names one of it) or the placeholder. */
    (void)ref;
    (void)slot;
    (void)generation;

    return false;
}

bool connector_request_state(const char *ref, uint8_t slot, uint32_t generation)
{
    return submit(HA_REQ_STATE, ref, NULL, slot, generation, false);
}

bool connector_command(const char *ref, const char *body)
{
    return submit(HA_REQ_COMMAND, ref, body, CONNECTOR_SLOT_NONE, CONNECTOR_GENERATION_ALWAYS,
                  false);
}

bool connector_request_image(const char *ref, uint16_t max_w, uint16_t max_h, uint8_t tag)
{
    (void)max_w;
    (void)max_h;

    return submit(HA_REQ_IMAGE, ref, NULL, tag, CONNECTOR_GENERATION_ALWAYS, false);
}

bool connector_poll(struct connector_result_s *out)
{
    if (results == NULL || xQueueReceive(results, out, 0) != pdTRUE)
        return false;

    /* A payload that rode inside the result points at the sender's copy. */
    if (out->payload_inline)
        out->payload = out->body;

    return true;
}

void connector_result_release(struct connector_result_s *res)
{
    if (res->payload_static && res->payload != NULL)
        work_buf_release(WORK_BUF_HA_RESULT);

    res->payload        = NULL;
    res->payload_len    = 0;
    res->payload_static = false;
    res->payload_inline = false;
}

int connector_page_parse(Sitemap *sitemap, const char *payload, size_t payload_len,
                         char *scratch, size_t scratch_size)
{
    return sitemap->parse(payload, payload_len, scratch, scratch_size);
}

void connector_events_subscribe(const struct connector_subscription_s *sub)
{
    /* The task subscribes to the page's entities itself, when it loads it. */
    (void)sub;
}

bool connector_events_streaming(void)
{
    return flag_streaming;
}

bool connector_events_take_resync(void)
{
    bool v = flag_resync;

    flag_resync = false;

    return v;
}

bool connector_events_take_reload(void)
{
    bool v = flag_reload;

    flag_reload = false;

    return v;
}

bool connector_events_poll(struct connector_event_s *out)
{
    return events != NULL && xQueueReceive(events, out, 0) == pdTRUE;
}

/* --------------------------------------------- the page model's four members */

/* The dashboard of page reference `ref`, as "ha:<dashboard>". */
static void ref_root(const char *ref, char *out, size_t out_size)
{
    const char *p = (strncmp(ref, "ha:", 3) == 0) ? ref + 3 : ref;
    const char *slash = strchr(p, '/');
    size_t      len = (slash != NULL) ? (size_t)(slash - p) : strlen(p);

    snprintf(out, out_size, "ha:%.*s", (int)len, p);
}

static void tile_to_item(const struct ha_tile_s *t, const char *root, Item *item)
{
    item->cleanItem();

    switch (t->kind)
    {
    case HA_TILE_BACK:
        item->setType(type_parent_link);
        item->setLabel(t->name);
        item->setPageLink(root);
        return;

    case HA_TILE_LINK:
    {
        char link[STR_LINK_LEN];

        snprintf(link, sizeof(link), "%s/%s", root, t->view);
        item->setType(type_link);
        item->setLabel(t->name);
        item->setIconName((t->icon[0] && strncmp(t->icon, "mdi:", 4) != 0) ? t->icon : "group");
        item->setPageLink(link);
        return;
    }

    default:
        break;
    }

    item->setType((enum ItemType)t->type);
    item->setLabel(t->label);
    item->setIconName(t->icon_name);
    item->setLink(t->entity);
    item->setReadOnly(t->read_only);
    item->setNumberPattern(t->pattern);
    item->setMinVal(t->min);
    item->setMaxVal(t->max);
    item->setStep(t->step);

    for (size_t i = 0; i < t->option_count; i++)
    {
        item->setSelectionCommand(i, t->options[i]);
        item->setSelectionLabel(i, t->options[i]);
    }

    item->setSelectionCount(t->option_count);
    item->setStateText("");
    item->applyState(t->state, strlen(t->state));
}

int Sitemap::parse(const char *payload, size_t payload_len, char *scratch, size_t scratch_size)
{
    (void)scratch;
    (void)scratch_size;

    if (payload == NULL || payload_len != sizeof(struct ha_page_s))
        return -1;

    const struct ha_page_s *page = (const struct ha_page_s *)payload;
    char                    root[HA_REF_LEN];

    ref_root(page->ref, root, sizeof(root));

    strlcpy(title, page->title, sizeof(title));
    strlcpy(page_id, page->ref, sizeof(page_id));

    /* Every slot, not only the ones this page fills: the UI draws each slot
     * whose type is not type_unknown, so a shorter page would keep the tail
     * of the page before. */
    for (size_t i = 0; i < ITEM_COUNT_MAX; i++)
        item_array[i].cleanItem();

    for (size_t i = 0; i < CLOCK_ITEM_COUNT; i++)
        clock_items[i].cleanItem();

    item_count = page->tile_count;

    for (size_t i = 0; i < item_count; i++)
        tile_to_item(&page->tiles[i], root, &item_array[i]);

    clock_item_count = page->clock_count;

    for (size_t i = 0; i < clock_item_count; i++)
        tile_to_item(&page->clock[i], root, &clock_items[i]);

    /* No widget ids: the Home Assistant connector sends item events only, so
     * nothing asks whether the page knows a widget. "Full" means it knows all
     * of them, which never reloads a page for an unknown one. */
    widget_id_count = 0;
    widget_ids_full = true;

    return 0;
}

bool Item::stateUrl(char *out, size_t out_size) const
{
    if (link[0] == '\0')
        return false;

    return strlcpy(out, link, out_size) < out_size;
}

bool Item::iconUrl(const char *website, char *out, size_t out_size) const
{
    (void)website;

    if (icon_name[0] == '\0')
        return false;

    /* The form icon_set_get_by_url() reads. The state only where the icon set
     * has variants for it -- a switch's on, a dimmer's level, a shutter's
     * position -- since the panel draws from its own set. */
    bool with_state = (type == type_switch || type == type_slider
                       || type == type_rollershutter || type == type_colorpicker);
    int  len;

    if (with_state && state_text[0] != '\0' && strpbrk(state_text, "&? ") == NULL)
        len = snprintf(out, out_size, "ha:/icon/%s?state=%s&format=png", icon_name, state_text);
    else
        len = snprintf(out, out_size, "ha:/icon/%s?format=png", icon_name);

    return (len > 0 && (size_t)len < out_size);
}

bool Item::name(char *out, size_t out_size) const
{
    if (link[0] == '\0')
        return false;

    return strlcpy(out, link, out_size) < out_size;
}
