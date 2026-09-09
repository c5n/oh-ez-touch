/**
 * @file port_ble.h
 *
 * Bluetooth LE, as much of it as a beacon scanner needs: turn the radio on,
 * listen for a while, and hand up what was heard.
 *
 * What is deliberately *not* here is any notion of what a beacon is. The port
 * yields raw advertisement bytes and the address they came from; deciding that
 * a particular 25 bytes of manufacturer data is an iBeacon is pure byte work
 * with no radio in it, so it lives above this in ble/ble_beacon.cpp where the
 * host tests can reach it. That split is the reason there is a test for the
 * beacon formats at all.
 *
 * The device speaks NimBLE in observer role. A desktop has no controller this
 * can drive -- BlueZ is a D-Bus service and a world away from an HCI port --
 * so the simulator reports that there is none, and serves a handful of
 * compiled-in advertisements when asked to, the way OHEZ_OFFLINE serves a
 * sitemap. See port/linux/port_ble.c.
 */
#ifndef PORT_BLE_H
#define PORT_BLE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * The advertisement payload, at its largest.
 *
 * 31 bytes is all a legacy advertisement may carry, and a scan response may
 * add 31 more. A passive scan never asks for the second, but the buffer is
 * sized for both so that turning the scan active stays a parameter rather than
 * a rewrite.
 */
#define PORT_BLE_ADV_MAX 62

/** No RSSI came with the report. The value is the one the controller itself
 * uses for "unavailable", and it cannot be confused with a real reading:
 * RSSI is negative dBm. */
#define PORT_BLE_RSSI_UNKNOWN 127

/* How the advertiser's address should be read. Worth reporting because it is
 * the difference between an address that identifies a device and one that is
 * rotated every fifteen minutes to stop it doing so -- which is why an iPhone
 * cannot be tracked by its address and an iBeacon can. */
enum port_ble_addr_type_e
{
    PORT_BLE_ADDR_PUBLIC = 0,
    PORT_BLE_ADDR_RANDOM,
    PORT_BLE_ADDR_UNKNOWN
};

typedef struct
{
    /* Most significant byte first, i.e. the order an address is written in:
     * addr[0] is the "AA" of AA:BB:CC:DD:EE:FF. The controller hands it over
     * the other way round and the port turns it, so that nothing above here
     * has to know which end a particular stack starts at. */
    uint8_t addr[6];
    uint8_t addr_type; /* enum port_ble_addr_type_e */
    int8_t  rssi;      /* dBm, or PORT_BLE_RSSI_UNKNOWN */
    uint8_t adv_len;
    uint8_t adv[PORT_BLE_ADV_MAX];
} port_ble_adv_t;

/**
 * Bring up the controller and the host stack.
 *
 * Costs tens of kilobytes of RAM that cannot be fully reclaimed, so this is
 * called once, at start-up, and only when the setting asks for it -- which is
 * why that setting needs a restart to take effect.
 *
 * @return false where there is no Bluetooth at all, which is every host, and
 *   on the device if the controller refuses to start.
 */
bool port_ble_init(void);

/**
 * Listen for `duration_ms`, then stop by itself.
 *
 * The controller owns the timing, so nothing above has to hold a deadline for
 * it; port_ble_scanning() is how the caller learns that the window closed.
 * Duplicate filtering is off on purpose: a beacon's RSSI is the point of
 * listening to it, and a filtered scan reports each advertiser once and then
 * goes quiet.
 *
 * @return false if a scan could not be started, including when one is already
 *   running.
 */
bool port_ble_scan_start(uint32_t duration_ms);

/** Stop early. Harmless when no scan is running. */
void port_ble_scan_stop(void);

/** Whether a scan window is still open. */
bool port_ble_scanning(void);

/**
 * Take the next advertisement off the queue.
 *
 * The queue is what separates the stack's task from the application's: reports
 * arrive on the host task, which must not touch the beacon table, the settings
 * or the display. Drain it from the task that owns those.
 *
 * @return false when there is nothing waiting.
 */
bool port_ble_adv_next(port_ble_adv_t *out);

/**
 * Reports dropped because the queue was full, since boot.
 *
 * Worth having rather than silently losing them: in a crowded room a passive
 * scan with duplicate filtering off can report faster than a cooperative loop
 * drains, and a beacon that never appears is otherwise indistinguishable from
 * one that is out of range.
 */
uint32_t port_ble_dropped(void);

#ifdef __cplusplus
}
#endif

#endif /* PORT_BLE_H */
