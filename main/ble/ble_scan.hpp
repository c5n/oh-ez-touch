#ifndef BLE_SCAN_HPP
#define BLE_SCAN_HPP

/**
 * @file ble_scan.hpp
 *
 * The beacon scanner: a duty cycle, a table of what is in range, and the
 * `ble/` half of the MQTT topic tree.
 *
 * The topic layout, the averaging and the eviction policy are all in
 * ble_scan.cpp.
 */

#include "config/config.hpp"

/**
 * Bring up the radio if the settings ask for it.
 *
 * Reads ble.enabled once and never again, which is why that setting carries
 * SETTINGS_F_RESTART: the Bluetooth controller costs tens of kilobytes of RAM
 * that cannot be handed back, so a panel that is not scanning must never have
 * started it.
 */
void ble_scan_setup(Config &config);

/**
 * Run the duty cycle: start a window when one is due, drain what the last one
 * heard, and publish.
 *
 * Everything here happens on the caller's task. The stack's own task only
 * queues advertisements; see port_ble.h.
 */
void ble_scan_loop(Config &config);

#endif // BLE_SCAN_HPP
