/**
 * @file linux/port_ble.c
 *
 * No Bluetooth controller on a desktop that this could drive.
 *
 * BlueZ is a D-Bus service, not an HCI transport a NimBLE host can sit on, so
 * there is nothing here to port: the honest answer is that there is no
 * Bluetooth, and that is what port_ble_init() says.
 *
 * Except when asked otherwise. `OHEZ_BLE_FIXTURE=1` serves the handful of
 * advertisements below instead, which is the same bargain OHEZ_OFFLINE strikes
 * with the sitemap: a stable, reproducible input, on request, so that
 * everything above the radio -- the beacon parsers, the table, the eviction,
 * the topics -- can be developed and watched on a desktop. It is off by
 * default for the reason port_bme280.c gives for inventing nothing: these
 * readings are published to a broker, and a simulator that quietly writes
 * fiction into someone's presence history would be worse than one that does
 * nothing.
 */

#include "port_ble.h"

#include "port_sys.h"

#include <stdlib.h>
#include <string.h>

#include "esp_log.h"

static const char *TAG = "port_ble";

/* ------------------------------------------------------------- fixtures */

/* Four advertisers, written out as the bytes a controller would report, so
 * that they go through the very same parser a real advertisement does -- the
 * point of the fixture is to exercise that parser, not to bypass it.
 *
 * The AD structures are `length, type, data...`; `02 01 06` at the front of
 * each is the flags structure every advertiser sends.
 */

/* An iBeacon. 1A FF is a 26 byte manufacturer-specific structure: company
 * 004C (Apple, little-endian), subtype 02, subtype length 15, then the 16 byte
 * proximity UUID, major and minor big-endian, and the power measured at one
 * metre. The UUID is Estimote's published default, which makes it recognisable
 * in a log. */
static const uint8_t fixture_ibeacon[] = {
    0x02, 0x01, 0x06,
    0x1A, 0xFF, 0x4C, 0x00, 0x02, 0x15,
    0xF7, 0x82, 0x6D, 0xA6, 0x4F, 0xA2, 0x4E, 0x98,
    0x80, 0x24, 0xBC, 0x5B, 0x71, 0xE0, 0x89, 0x3E,
    0x03, 0xE8,  /* major 1000 */
    0x00, 0x2A,  /* minor 42   */
    0xC5         /* -59 dBm at 1 m */
};

/* An Eddystone-UID frame: the 0xFEAA service UUID in the 16-bit UUID list, and
 * the frame itself as service data. 0x00 is the UID frame type, then the
 * ranging power, a 10 byte namespace and a 6 byte instance. */
static const uint8_t fixture_eddystone_uid[] = {
    0x02, 0x01, 0x06,
    0x03, 0x03, 0xAA, 0xFE,
    0x17, 0x16, 0xAA, 0xFE, 0x00,
    0xEB,  /* -21 dBm at 0 m */
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
    0x00, 0x00
};

/* An Eddystone-TLM frame from the *same* advertiser as the UID one above.
 * Eddystone interleaves its frame types, so identity and telemetry arrive in
 * different advertisements -- which is exactly the merging the beacon table has
 * to do, and the reason this fixture exists. */
static const uint8_t fixture_eddystone_tlm[] = {
    0x02, 0x01, 0x06,
    0x03, 0x03, 0xAA, 0xFE,
    0x11, 0x16, 0xAA, 0xFE, 0x20,
    0x00,        /* TLM version 0        */
    0x0B, 0xB8,  /* 3000 mV              */
    0x15, 0x80,  /* 21.5 C, 8.8 fixed    */
    0x00, 0x00, 0x0A, 0xBC, /* advertisements sent */
    0x00, 0x00, 0x27, 0x10  /* 1000 s of uptime, in 0.1 s */
};

/* Not a beacon at all: something with a name and a transmit power, which is
 * what most of a room looks like. Only published when the settings ask for
 * non-beacon devices. */
static const uint8_t fixture_device[] = {
    0x02, 0x01, 0x06,
    0x09, 0x09, 'L', 'i', 'v', 'i', 'n', 'g', 'T', 'V',
    0x02, 0x0A, 0xF4  /* -12 dBm */
};

struct fixture_s
{
    const uint8_t *adv;
    size_t         adv_len;
    uint8_t        addr[6];
    uint8_t        addr_type;
    int8_t         rssi;
};

static const struct fixture_s fixtures[] = {
    { fixture_ibeacon, sizeof(fixture_ibeacon),
      { 0xC1, 0x37, 0x1F, 0x0A, 0x22, 0x81 }, PORT_BLE_ADDR_RANDOM, -67 },
    { fixture_eddystone_uid, sizeof(fixture_eddystone_uid),
      { 0xF4, 0xB8, 0x5E, 0x12, 0x34, 0x56 }, PORT_BLE_ADDR_PUBLIC, -78 },
    { fixture_eddystone_tlm, sizeof(fixture_eddystone_tlm),
      { 0xF4, 0xB8, 0x5E, 0x12, 0x34, 0x56 }, PORT_BLE_ADDR_PUBLIC, -79 },
    { fixture_device, sizeof(fixture_device),
      { 0x5C, 0x31, 0x7B, 0xAA, 0xBB, 0xCC }, PORT_BLE_ADDR_PUBLIC, -55 },
};

#define FIXTURE_COUNT ((int)(sizeof(fixtures) / sizeof(fixtures[0])))

/* Reports per advertiser per window. More than one so that the averaging in
 * ble_scan.cpp has something to average, which is the part of it that would
 * otherwise never run here. */
#define FIXTURE_REPEATS 4

/* --------------------------------------------------------------- the port */

static bool     ble_enabled = false;
static uint64_t ble_scan_until = 0;
static int      ble_next = 0;
static int      ble_pending = 0;
static uint32_t ble_window = 0;

bool port_ble_init(void)
{
    const char *fixture = getenv("OHEZ_BLE_FIXTURE");

    if (fixture == NULL || strcmp(fixture, "0") == 0)
    {
        ESP_LOGI(TAG, "no Bluetooth on this target "
                      "(set OHEZ_BLE_FIXTURE=1 for the compiled-in beacons)");
        return false;
    }

    ESP_LOGI(TAG, "fixture mode: serving %d compiled-in advertisers", FIXTURE_COUNT);

    ble_enabled = true;

    return true;
}

bool port_ble_scan_start(uint32_t duration_ms)
{
    if (ble_enabled == false)
        return false;

    if (port_millis() < ble_scan_until)
        return false;

    ble_scan_until = port_millis() + duration_ms;
    ble_pending = FIXTURE_COUNT * FIXTURE_REPEATS;
    ble_next = 0;
    ble_window++;

    return true;
}

void port_ble_scan_stop(void)
{
    ble_scan_until = 0;
    ble_pending = 0;
}

bool port_ble_scanning(void)
{
    return ble_enabled && port_millis() < ble_scan_until;
}

bool port_ble_adv_next(port_ble_adv_t *out)
{
    if (ble_pending <= 0)
        return false;

    const struct fixture_s *f = &fixtures[ble_next % FIXTURE_COUNT];

    memset(out, 0, sizeof(*out));
    memcpy(out->addr, f->addr, sizeof(out->addr));
    out->addr_type = f->addr_type;
    out->adv_len = (uint8_t)f->adv_len;
    memcpy(out->adv, f->adv, f->adv_len);

    /* Jittered, and differently each window, so that the mean, the distance
     * estimate and the topics that carry them visibly move -- a fixture whose
     * RSSI never changes cannot show that the averaging works. Deterministic,
     * because a reproducible screen is the point. */
    int jitter = (int)((ble_window + (uint32_t)ble_next) % 7) - 3;

    out->rssi = (int8_t)(f->rssi + jitter);

    ble_next++;
    ble_pending--;

    return true;
}

uint32_t port_ble_dropped(void)
{
    /* The fixtures are generated on demand by the caller's own task, so there
     * is no queue between two tasks to overflow. */
    return 0;
}
