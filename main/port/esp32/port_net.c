/**
 * @file esp32/port_net.c
 *
 * What the station interface is doing, from esp_wifi and esp_netif.
 *
 * Every call here tolerates a WiFi driver that has not been started: the Info
 * tab and the web status page are reachable before the radio is up -- on a
 * pristine device that is the *only* time they are reachable -- and an empty
 * field is the right answer then.
 */
#include "port_net.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_event.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_wifi.h"

static void format_ip(char *dst, size_t dst_size, esp_ip4_addr_t addr)
{
    snprintf(dst, dst_size, IPSTR, IP2STR(&addr));
}

static void format_mac(char *dst, size_t dst_size, const uint8_t mac[6])
{
    snprintf(dst, dst_size, "%02x:%02x:%02x:%02x:%02x:%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

void port_net_info(port_net_info_t *out)
{
    memset(out, 0, sizeof(*out));

    /* Not PORT_NET_RSSI_WIRED: this is a radio that happens to be off or out of
     * range, which is a different thing from having no radio, and -100 dBm is
     * what the quality scale already reads as nothing. */
    out->rssi = -100;

    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");

    if (netif != NULL)
    {
        const char *hostname = NULL;

        if (esp_netif_get_hostname(netif, &hostname) == ESP_OK && hostname != NULL)
            snprintf(out->hostname, sizeof(out->hostname), "%s", hostname);

        esp_netif_ip_info_t ip = {};

        if (esp_netif_get_ip_info(netif, &ip) == ESP_OK)
        {
            format_ip(out->ip, sizeof(out->ip), ip.ip);
            format_ip(out->netmask, sizeof(out->netmask), ip.netmask);
            format_ip(out->gateway, sizeof(out->gateway), ip.gw);

            out->connected = (ip.ip.addr != 0);
        }

        esp_netif_dns_info_t dns = {};

        if (   esp_netif_get_dns_info(netif, ESP_NETIF_DNS_MAIN, &dns) == ESP_OK
            && dns.ip.type == ESP_IPADDR_TYPE_V4)
        {
            format_ip(out->dns, sizeof(out->dns), dns.ip.u_addr.ip4);
        }
    }

    uint8_t mac[6] = {};

    if (esp_wifi_get_mac(WIFI_IF_STA, mac) == ESP_OK)
        format_mac(out->mac, sizeof(out->mac), mac);

    wifi_ap_record_t ap = {};

    /* ESP_ERR_WIFI_NOT_CONNECT while the station is down, which is not an error
     * here -- it just means there is no SSID, BSSID or RSSI to report. */
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK)
    {
        snprintf(out->ssid, sizeof(out->ssid), "%s", (const char *)ap.ssid);
        format_mac(out->bssid, sizeof(out->bssid), ap.bssid);
        out->rssi = ap.rssi;
    }
}

static uint16_t scan_count;
static bool scan_running;
static bool scan_failed;

/* esp_wifi keeps the results until they are fetched or the next scan starts, so
 * there is nothing to copy here -- but the count has to be read once and the
 * records are fetched all at once, so port_net_scan_result() reads from this. */
static wifi_ap_record_t *scan_records;

static void scan_done_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    (void)id;

    const wifi_event_sta_scan_done_t *done = (const wifi_event_sta_scan_done_t *)data;

    scan_running = false;

    if (done == NULL || done->status != 0)
    {
        scan_failed = true;
        return;
    }

    uint16_t found = 0;

    if (esp_wifi_scan_get_ap_num(&found) != ESP_OK || found == 0)
    {
        scan_count = 0;
        return;
    }

    scan_records = calloc(found, sizeof(*scan_records));

    if (scan_records == NULL)
    {
        scan_failed = true;
        esp_wifi_clear_ap_list();
        return;
    }

    if (esp_wifi_scan_get_ap_records(&found, scan_records) != ESP_OK)
    {
        free(scan_records);
        scan_records = NULL;
        scan_failed = true;
        return;
    }

    scan_count = found;
}

bool port_net_scan_start(void)
{
    if (scan_running == true)
        return true;

    port_net_scan_free();

    static bool handler_registered;

    if (handler_registered == false)
    {
        if (esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_SCAN_DONE,
                                       scan_done_handler, NULL) != ESP_OK)
        {
            return false;
        }

        handler_registered = true;
    }

    scan_failed = false;

    /* block = false: the scan takes seconds, and lv_timer_handler() does not
     * run during them. WIFI_EVENT_SCAN_DONE reports the result. */
    if (esp_wifi_scan_start(NULL, false) != ESP_OK)
        return false;

    scan_running = true;

    return true;
}

int port_net_scan_poll(void)
{
    if (scan_running == true)
        return PORT_NET_SCAN_RUNNING;

    if (scan_failed == true)
        return -1;

    return scan_count;
}

bool port_net_scan_result(int index, port_net_ap_t *out)
{
    if (scan_records == NULL || index < 0 || index >= (int)scan_count)
        return false;

    snprintf(out->ssid, sizeof(out->ssid), "%s", (const char *)scan_records[index].ssid);
    out->rssi = scan_records[index].rssi;
    out->encrypted = (scan_records[index].authmode != WIFI_AUTH_OPEN);

    return true;
}

void port_net_scan_free(void)
{
    free(scan_records);
    scan_records = NULL;
    scan_count = 0;
    scan_failed = false;
}
