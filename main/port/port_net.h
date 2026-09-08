/**
 * @file port_net.h
 *
 * What the network is doing, for the Info tab and the web status page, and the
 * access-point scan the WLAN settings tab offers.
 *
 * Reporting only: bringing the link up is wlan.hpp's job on the device and the
 * operating system's on the host. Every field is a string because every one of
 * them is going straight into a label or a table cell -- formatting an IP
 * address is exactly the kind of thing that should happen once.
 */
#ifndef PORT_NET_H
#define PORT_NET_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Reported as the signal strength of a link that has none.
 *
 * A wired host has no RSSI, and pretending otherwise would put a plausible
 * percentage in the header. Chosen above every real reading: RSSI is negative
 * dBm, so no radio can produce it.
 */
#define PORT_NET_RSSI_WIRED 1

/** Longest SSID plus a terminator, from the 802.11 32-byte limit. */
#define PORT_NET_SSID_SIZE 33

typedef struct
{
    char hostname[33];
    char ssid[PORT_NET_SSID_SIZE]; /* the interface name where there is no SSID */
    char bssid[18];                /* "aa:bb:cc:dd:ee:ff" */
    char mac[18];
    char ip[16];
    char netmask[16];
    char gateway[16];
    char dns[16];
    int8_t rssi;                   /* dBm, or PORT_NET_RSSI_WIRED */
    bool connected;
} port_net_info_t;

/**
 * Fill in what is known. Unknown fields come back as the empty string rather
 * than stale, so a caller can render them without checking `connected` first.
 */
void port_net_info(port_net_info_t *out);

typedef struct
{
    char ssid[PORT_NET_SSID_SIZE];
    int8_t rssi;
    bool encrypted;
} port_net_ap_t;

/** port_net_scan_poll() while a scan is still running. */
#define PORT_NET_SCAN_RUNNING (-2)

/**
 * Start an asynchronous scan. False if one could not be started.
 *
 * Asynchronous because a blocking scan takes seconds, and lv_timer_handler()
 * does not run during them: the panel would look dead. The station connection
 * is briefly interrupted either way and comes back by itself.
 */
bool port_net_scan_start(void);

/**
 * PORT_NET_SCAN_RUNNING, the number of access points found, or -1 if the scan
 * failed. Call port_net_scan_result() for each, then port_net_scan_free().
 */
int port_net_scan_poll(void);

/** One result of the last completed scan. */
bool port_net_scan_result(int index, port_net_ap_t *out);

/** Release what the scan collected. */
void port_net_scan_free(void);

#ifdef __cplusplus
}
#endif

#endif /* PORT_NET_H */
