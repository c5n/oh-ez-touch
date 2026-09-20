/**
 * @file esp32/port_ble.c
 *
 * NimBLE in observer role: the smallest Bluetooth stack that can hear an
 * advertisement.
 *
 * NimBLE rather than Bluedroid because the roles can be compiled out
 * individually -- sdkconfig.defaults.esp32 turns off central, peripheral and
 * broadcaster and leaves observer -- which is the difference between a stack
 * that fits next to LVGL and its icon buffers and one that does not.
 *
 * Two things about sharing the radio with WiFi are worth knowing here. The
 * software coexistence arbiter is on (it is the default once both are enabled)
 * and it interleaves them, so neither stops working; what suffers is
 * throughput, in both directions. That is why the scan is duty-cycled by
 * ble/ble_scan.cpp rather than left running, and why the default window is five
 * seconds in thirty rather than thirty in thirty.
 *
 * Everything below runs on the NimBLE host task except port_ble_adv_next() and
 * port_ble_dropped(). The queue in between is the whole of the interface
 * between the two, deliberately: nothing here reaches into the beacon table,
 * the settings or LVGL.
 */

#include "port_ble.h"

#include <string.h>

#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include "host/ble_gap.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"

static const char *TAG = "port_ble";

/**
 * Reports the application may fall behind by.
 *
 * A passive scan with duplicate filtering off reports every advertisement from
 * every device in range -- in a busy room that is a few hundred a second -- and
 * the application drains this from a cooperative loop that also runs LVGL. 24
 * is about a tenth of a second of the worst case, which is far more slack than
 * a 5 ms loop needs, and 1.7 KB of RAM.
 */
#define PORT_BLE_QUEUE_DEPTH 24

static QueueHandle_t ble_queue = NULL;
static uint32_t      ble_dropped = 0;

/* Written on the host task, read on the application's. */
static volatile bool ble_ready = false;
static volatile bool ble_scanning = false;

/* Whether a scan was asked for before the stack finished synchronising. The
 * host is not usable until sync_cb has run, which is some tens of milliseconds
 * after nimble_port_init() returns, so the first scan_start() would otherwise
 * be the one that fails. */
static volatile bool     ble_scan_wanted = false;
static volatile uint32_t ble_scan_wanted_ms = 0;

static bool scan_begin(uint32_t duration_ms);

/* ---------------------------------------------------------------- reporting */

static void adv_enqueue(const struct ble_gap_disc_desc *disc)
{
    port_ble_adv_t adv;
    uint8_t        len = disc->length_data;

    if (len > PORT_BLE_ADV_MAX)
        len = PORT_BLE_ADV_MAX;

    /* NimBLE's ble_addr_t holds the address least significant byte first; the
     * port's contract is the other way round, so it is turned exactly once,
     * here. */
    for (int i = 0; i < 6; i++)
        adv.addr[i] = disc->addr.val[5 - i];

    switch (disc->addr.type)
    {
    case BLE_ADDR_PUBLIC:
    case BLE_ADDR_PUBLIC_ID:
        adv.addr_type = PORT_BLE_ADDR_PUBLIC;
        break;
    case BLE_ADDR_RANDOM:
    case BLE_ADDR_RANDOM_ID:
        adv.addr_type = PORT_BLE_ADDR_RANDOM;
        break;
    default:
        adv.addr_type = PORT_BLE_ADDR_UNKNOWN;
        break;
    }

    adv.rssi = disc->rssi;
    adv.adv_len = len;

    if (len > 0)
        memcpy(adv.adv, disc->data, len);

    /* Zeroed rather than left as whatever the last report was: the parser is
     * given adv_len and stops there, but a queue entry that is copied around
     * with stale tail bytes is a nuisance to read in a debugger. */
    if (len < PORT_BLE_ADV_MAX)
        memset(adv.adv + len, 0, (size_t)(PORT_BLE_ADV_MAX - len));

    /* Never blocks: this is the host task, and stalling it would stop the very
     * stack that is feeding the queue. */
    if (xQueueSend(ble_queue, &adv, 0) != pdTRUE)
        ble_dropped++;
}

static int ble_gap_event(struct ble_gap_event *event, void *arg)
{
    (void)arg;

    switch (event->type)
    {
    case BLE_GAP_EVENT_DISC:
        adv_enqueue(&event->disc);
        break;

    case BLE_GAP_EVENT_DISC_COMPLETE:
        ble_scanning = false;
        break;

    default:
        break;
    }

    return 0;
}

/* ------------------------------------------------------------- the stack */

static void ble_on_sync(void)
{
    /* Makes sure the controller has an identity address before anything asks
     * it to use one. */
    int rc = ble_hs_util_ensure_addr(0);

    if (rc != 0)
    {
        ESP_LOGE(TAG, "no usable address; rc=%d", rc);
        return;
    }

    ble_ready = true;

    ESP_LOGI(TAG, "stack ready");

    if (ble_scan_wanted == true)
    {
        ble_scan_wanted = false;
        scan_begin(ble_scan_wanted_ms);
    }
}

static void ble_on_reset(int reason)
{
    /* The controller restarted underneath us -- NimBLE calls sync_cb again
     * afterwards, so there is nothing to do but stop claiming to be scanning. */
    ESP_LOGW(TAG, "controller reset, reason %d", reason);

    ble_ready = false;
    ble_scanning = false;
}

static void ble_host_task(void *param)
{
    (void)param;

    /* Returns when nimble_port_stop() is called, which nothing here does. */
    nimble_port_run();

    nimble_port_freertos_deinit();
}

bool port_ble_init(void)
{
    if (ble_queue == NULL)
        ble_queue = xQueueCreate(PORT_BLE_QUEUE_DEPTH, sizeof(port_ble_adv_t));

    if (ble_queue == NULL)
    {
        ESP_LOGE(TAG, "no report queue");
        return false;
    }

    /* Brings up the controller and the host both. NVS has already been
     * initialised by port_kv_init(), which NimBLE needs even with bonding
     * turned off. */
    esp_err_t err = nimble_port_init();

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "cannot start the stack: %s", esp_err_to_name(err));
        return false;
    }

    ble_hs_cfg.sync_cb = ble_on_sync;
    ble_hs_cfg.reset_cb = ble_on_reset;

    nimble_port_freertos_init(ble_host_task);

    /* True means "the stack is coming up", not "it is up": ble_on_sync() runs
     * a few tens of milliseconds from now, and port_ble_scan_start() before
     * then is remembered rather than refused. */
    return true;
}

/* --------------------------------------------------------------- scanning */

static bool scan_begin(uint32_t duration_ms)
{
    struct ble_gap_disc_params params = {0};
    uint8_t                    own_addr_type = BLE_OWN_ADDR_PUBLIC;
    int                        rc;

    rc = ble_hs_id_infer_auto(0, &own_addr_type);

    if (rc != 0)
    {
        ESP_LOGE(TAG, "cannot determine an address type; rc=%d", rc);
        return false;
    }

    /* Passive: a beacon is a broadcaster and has nothing more to say if asked,
     * so a scan request would spend transmit time and radio share for
     * nothing.
     *
     * filter_duplicates off, which is the decision that makes this a beacon
     * scanner rather than a device finder. With it on the controller reports
     * each advertiser once per scan and the RSSI stops arriving, and RSSI is
     * what is being measured. The cost is a great many more reports, which is
     * what the queue above is sized for. */
    params.passive = 1;
    params.filter_duplicates = 0;
    params.limited = 0;
    params.filter_policy = BLE_HCI_SCAN_FILT_NO_WL;

    /* Defaults for the window and interval: NimBLE picks a continuous scan
     * within the window this call is given, and the coexistence arbiter is
     * what actually decides how much of the radio it gets. */
    params.itvl = 0;
    params.window = 0;

    rc = ble_gap_disc(own_addr_type, duration_ms, &params, ble_gap_event, NULL);

    if (rc != 0)
    {
        ESP_LOGE(TAG, "cannot start a scan; rc=%d", rc);
        return false;
    }

    ble_scanning = true;

    return true;
}

bool port_ble_scan_start(uint32_t duration_ms)
{
    if (ble_queue == NULL)
        return false;

    if (ble_scanning == true)
        return false;

    if (ble_ready == false)
    {
        /* Deferred to ble_on_sync() rather than failed. The caller is a duty
         * cycle on a timer, so a refusal here would simply mean no beacons
         * until the next tick -- and at boot that is the tick that matters,
         * because it is the one the user is watching. */
        ble_scan_wanted = true;
        ble_scan_wanted_ms = duration_ms;
        return true;
    }

    return scan_begin(duration_ms);
}

void port_ble_scan_stop(void)
{
    ble_scan_wanted = false;

    if (ble_scanning == false)
        return;

    /* Returns BLE_HS_EALREADY if the scan finished between the test above and
     * here, which is not a problem worth reporting. */
    ble_gap_disc_cancel();

    ble_scanning = false;
}

bool port_ble_scanning(void)
{
    return ble_scanning || ble_scan_wanted;
}

bool port_ble_adv_next(port_ble_adv_t *out)
{
    if (ble_queue == NULL)
        return false;

    return xQueueReceive(ble_queue, out, 0) == pdTRUE;
}

uint32_t port_ble_dropped(void)
{
    return ble_dropped;
}
