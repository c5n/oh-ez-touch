/**
 * @file wlan_radio.cpp
 *
 * The station, the setup access point and the state machine that decides
 * between them. wlan.cpp holds the credentials they use.
 *
 * The device half is esp_wifi and esp_event, where Arduino's WiFi object was.
 * The host half is a dozen lines: a desktop is already on a network and has
 * nothing to provision, so it reports WLAN_ONLINE and there is no access point
 * to raise. That is a real difference rather than a stub -- the states below
 * exist to get a radio from "no credentials" to "connected", and there is no
 * radio here.
 */

#include "sdkconfig.h"

#include "wlan.hpp"

#include <stdio.h>
#include <string.h>

#include "port/port_sys.h"

static Config           *wlan_config = NULL;
static enum wlan_state_e wlan_current = WLAN_IDLE;

static char wlan_sta_ssid_buf[WLAN_SSID_SIZE];
static char wlan_sta_psk_buf[WLAN_PSK_SIZE];

enum wlan_state_e wlan_state(void)
{
    return wlan_current;
}

const char *wlan_sta_ssid(void)
{
    return wlan_sta_ssid_buf;
}

#if !CONFIG_IDF_TARGET_LINUX

#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"

#ifndef WLAN_CONNECT_TIMEOUT
#define WLAN_CONNECT_TIMEOUT (20 * 1000)
#endif

#ifndef WLAN_RETRY_INTERVAL
#define WLAN_RETRY_INTERVAL (60 * 1000)
#endif

/* How long the access point stays up after the station connects, so that the
 * browser which just submitted the credentials gets its response. */
#ifndef WLAN_AP_LINGER
#define WLAN_AP_LINGER (5 * 1000)
#endif

/* How long the access point stays up per offline episode. It is an open
 * network and /update is unauthenticated, so it is not left up indefinitely
 * on a device that merely lost its WLAN -- that device keeps retrying
 * quietly instead. A device with no credentials at all is the exception: it
 * would otherwise be unreachable forever. */
#ifndef WLAN_AP_TIMEOUT
#define WLAN_AP_TIMEOUT (10 * 60 * 1000)
#endif

static esp_netif_t *sta_netif = NULL;
static esp_netif_t *ap_netif = NULL;

static bool wlan_ap_is_up = false;

/* One access-point window per offline episode, armed again once the station
 * has been back online. */
static bool wlan_ap_spent = false;

/* Set from the event handler, read from the loop. Both run on tasks other than
 * each other's, so this is the whole of the shared state and it is one bool:
 * IP_EVENT_STA_GOT_IP sets it, WIFI_EVENT_STA_DISCONNECTED clears it. It
 * replaces the WiFi.status() == WL_CONNECTED test, which asked the driver the
 * same question. */
static volatile bool sta_has_ip = false;

static uint64_t wlan_connect_deadline = 0;
static uint64_t wlan_retry_deadline = 0;
static uint64_t wlan_ap_deadline = 0;
static uint64_t wlan_ap_linger_deadline = 0;

/* Fixed-width SDK fields are not guaranteed to be terminated when the value
 * fills them exactly, so they cannot be handed to strlcpy(). */
static void wlan_copy_fixed(char *dst, size_t dst_size, const uint8_t *src, size_t src_size)
{
    size_t len = strnlen((const char *)src, src_size);

    if (len >= dst_size)
        len = dst_size - 1;

    memcpy(dst, src, len);
    dst[len] = '\0';
}

/* The SDK's own station config. AutoConnect called WiFi.persistent(true) and
 * its first connection attempt was a bare WiFi.begin(), so this holds the
 * credential the device actually last used -- which is the one thing its own
 * blob cannot tell us. */
static bool wlan_import_sdk(char *ssid, size_t ssid_size, char *psk, size_t psk_size)
{
    wifi_config_t conf;

    if (esp_wifi_get_config(WIFI_IF_STA, &conf) != ESP_OK)
        return false;

    if (conf.sta.ssid[0] == '\0')
        return false;

    wlan_copy_fixed(ssid, ssid_size, conf.sta.ssid, sizeof(conf.sta.ssid));
    wlan_copy_fixed(psk, psk_size, conf.sta.password, sizeof(conf.sta.password));

    return true;
}

bool wlan_credentials_import(char *ssid, size_t ssid_size, char *psk, size_t psk_size)
{
    ssid[0] = '\0';
    psk[0] = '\0';

    if (wlan_import_sdk(ssid, ssid_size, psk, psk_size) == true)
    {
#if CONFIG_OHEZ_DEBUG_WLAN
        printf("wlan: imported '%s' from the SDK station config\r\n", ssid);
#endif
        return true;
    }

    if (wlan_credentials_import_blob(ssid, ssid_size, psk, psk_size) == true)
    {
#if CONFIG_OHEZ_DEBUG_WLAN
        printf("wlan: imported '%s' from the AutoConnect blob\r\n", ssid);
#endif
        return true;
    }

    return false;
}

static void wlan_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)data;

    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP)
        sta_has_ip = true;
    else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED)
        sta_has_ip = false;
}

static void wlan_ap_raise(void)
{
    if (wlan_ap_is_up == true)
        return;

    /* APSTA rather than AP: a connection attempt may well be in flight, and
     * dropping the station would abandon it. */
    esp_wifi_set_mode(WIFI_MODE_APSTA);

    /* Open, as AutoConnect's was (-DAUTOCONNECT_PSK='""'), and named after the
     * host so that a rack of these is tellable apart. No address configuration:
     * IDF's default of 192.168.4.1 is what a user expects, where AutoConnect
     * used 172.217.28.1 -- a Google address picked to bait captive-portal
     * detection, which is pointless without the DNS hijack we deliberately do
     * not do. */
    wifi_config_t conf = {};

    strlcpy((char *)conf.ap.ssid, wlan_config->item.general.hostname, sizeof(conf.ap.ssid));
    conf.ap.ssid_len = (uint8_t)strlen((const char *)conf.ap.ssid);
    conf.ap.channel = 1;
    conf.ap.max_connection = 4;
    conf.ap.authmode = WIFI_AUTH_OPEN;

    esp_wifi_set_config(WIFI_IF_AP, &conf);

    wlan_ap_is_up = true;
    wlan_ap_deadline = port_millis() + WLAN_AP_TIMEOUT;

#if CONFIG_OHEZ_DEBUG_WLAN
    esp_netif_ip_info_t ip = {};
    esp_netif_get_ip_info(ap_netif, &ip);
    printf("wlan: AP '%s' up at " IPSTR "\r\n",
           wlan_config->item.general.hostname, IP2STR(&ip.ip));
#endif
}

static void wlan_ap_drop(void)
{
    if (wlan_ap_is_up == false)
        return;

    esp_wifi_set_mode(WIFI_MODE_STA);
    wlan_ap_is_up = false;

#if CONFIG_OHEZ_DEBUG_WLAN
    printf("wlan: AP down\r\n");
#endif
}

static void wlan_connect_begin(void)
{
    wifi_config_t conf = {};

    strlcpy((char *)conf.sta.ssid, wlan_sta_ssid_buf, sizeof(conf.sta.ssid));
    strlcpy((char *)conf.sta.password, wlan_sta_psk_buf, sizeof(conf.sta.password));

    esp_wifi_set_config(WIFI_IF_STA, &conf);

    /* Not an error worth reporting: it fails while a previous attempt is still
     * in flight, and the deadline below covers that. */
    esp_wifi_connect();

    sta_has_ip = false;
    wlan_current = WLAN_CONNECTING;
    wlan_connect_deadline = port_millis() + WLAN_CONNECT_TIMEOUT;

#if CONFIG_OHEZ_DEBUG_WLAN
    printf("wlan: connecting to '%s'\r\n", wlan_sta_ssid_buf);
#endif
}

/* Entering an offline episode: start the retry timer, and open the access
 * point once so that a device with wrong credentials can be corrected. */
static void wlan_go_offline(void)
{
    wlan_current = WLAN_RETRY_WAIT;
    wlan_retry_deadline = port_millis() + WLAN_RETRY_INTERVAL;

    if (wlan_ap_spent == false)
    {
        wlan_ap_raise();
        wlan_ap_spent = true;
    }
}

void wlan_setup(Config *config)
{
    wlan_config = config;

    ESP_ERROR_CHECK(esp_netif_init());

    sta_netif = esp_netif_create_default_wifi_sta();
    ap_netif = esp_netif_create_default_wifi_ap();

    /* On the netif, and before the interface comes up: the name is what
     * tools/batchupdate.sh resolves devices by, and DHCP sends it with the
     * lease request. */
    esp_netif_set_hostname(sta_netif, config->item.general.hostname);

    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init));

    /* We own the credentials now, so the SDK need not write its own copy on
     * every connect. This also leaves AutoConnect's copy untouched, which is
     * what lets a downgrade still find its credentials. Was
     * WiFi.persistent(false). */
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wlan_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wlan_event, NULL));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    /* A panel on mains power has nothing to save it for, and modem sleep adds
     * latency to every openHAB poll. Was WiFi.setSleep(false). */
    esp_wifi_set_ps(WIFI_PS_NONE);

    bool have = wlan_credentials_get(wlan_sta_ssid_buf, sizeof(wlan_sta_ssid_buf),
                                     wlan_sta_psk_buf, sizeof(wlan_sta_psk_buf));

    /* Nothing of ours stored: adopt whatever AutoConnect left behind, so that
     * a device provisioned through its portal does not have to be provisioned
     * again. Deliberately not gated behind an "already imported" marker -- the
     * import only runs when our own store is empty anyway, and leaving it
     * unconditional means a device that gets downgraded and re-provisioned
     * still migrates cleanly on the way back up. */
    if (have == false)
    {
        have = wlan_credentials_import(wlan_sta_ssid_buf, sizeof(wlan_sta_ssid_buf),
                                       wlan_sta_psk_buf, sizeof(wlan_sta_psk_buf));

        if (have == true)
            wlan_credentials_set(wlan_sta_ssid_buf, wlan_sta_psk_buf);
    }

    if (have == true)
    {
        wlan_connect_begin();
        return;
    }

    /* No credentials at all. Nothing to retry, so the access point stays up
     * without a deadline and the screen shows how to reach it. */
    wlan_ap_raise();
    wlan_ap_spent = true;
    wlan_current = WLAN_PORTAL;

#if CONFIG_OHEZ_DEBUG_WLAN
    printf("wlan: no credentials, waiting to be provisioned\r\n");
#endif
}

void wlan_loop(void)
{
    switch (wlan_current)
    {
    case WLAN_CONNECTING:
        if (sta_has_ip == true)
        {
            wlan_current = WLAN_ONLINE;
            wlan_ap_linger_deadline = port_millis() + WLAN_AP_LINGER;
            wlan_ap_spent = false;

#if CONFIG_OHEZ_DEBUG_WLAN
            esp_netif_ip_info_t ip = {};
            esp_netif_get_ip_info(sta_netif, &ip);
            printf("wlan: online as " IPSTR "\r\n", IP2STR(&ip.ip));
#endif
        }
        else if (port_millis() >= wlan_connect_deadline)
        {
            esp_wifi_disconnect();
            wlan_go_offline();
        }
        break;

    case WLAN_ONLINE:
        if (sta_has_ip == false)
            wlan_go_offline();
        else if (wlan_ap_is_up == true && port_millis() >= wlan_ap_linger_deadline)
            wlan_ap_drop();
        break;

    case WLAN_RETRY_WAIT:
        if (wlan_ap_is_up == true && port_millis() >= wlan_ap_deadline)
            wlan_ap_drop();

        if (port_millis() >= wlan_retry_deadline)
            wlan_connect_begin();
        break;

    case WLAN_PORTAL:
    case WLAN_IDLE:
    default:
        break;
    }
}

void wlan_reconnect(void)
{
    if (wlan_sta_ssid_buf[0] == '\0')
        return;

    wlan_connect_begin();
}

bool wlan_set_credentials(const char *ssid, const char *psk)
{
    if (ssid == NULL || ssid[0] == '\0')
        return false;

    strlcpy(wlan_sta_ssid_buf, ssid, sizeof(wlan_sta_ssid_buf));
    strlcpy(wlan_sta_psk_buf, (psk != NULL) ? psk : "", sizeof(wlan_sta_psk_buf));

    if (wlan_credentials_set(wlan_sta_ssid_buf, wlan_sta_psk_buf) == false)
        return false;

    /* The access point must survive this call: the browser that submitted the
     * credentials is connected to it and still needs its response. It goes
     * away on the linger timer once the station is up. */
    wlan_ap_spent = true;
    wlan_connect_begin();

    return true;
}

const char *wlan_ap_ssid(void)
{
    if (wlan_ap_is_up == false)
        return NULL;

    return wlan_config->item.general.hostname;
}

uint32_t wlan_ap_ip(void)
{
    if (wlan_ap_is_up == false)
        return 0;

    esp_netif_ip_info_t ip = {};

    esp_netif_get_ip_info(ap_netif, &ip);

    return ip.ip.addr;
}

#else /* CONFIG_IDF_TARGET_LINUX */

/* A desktop is on a network already, and nothing here can change that. The
 * credential store is still real -- wlan.cpp writes it to the emulated NVS --
 * so the settings tab can be worked on; only the radio is missing. */

void wlan_setup(Config *config)
{
    wlan_config = config;

    wlan_credentials_get(wlan_sta_ssid_buf, sizeof(wlan_sta_ssid_buf),
                         wlan_sta_psk_buf, sizeof(wlan_sta_psk_buf));

    wlan_current = WLAN_ONLINE;
}

void wlan_loop(void)
{
}

void wlan_reconnect(void)
{
}

bool wlan_set_credentials(const char *ssid, const char *psk)
{
    if (ssid == NULL || ssid[0] == '\0')
        return false;

    strlcpy(wlan_sta_ssid_buf, ssid, sizeof(wlan_sta_ssid_buf));
    strlcpy(wlan_sta_psk_buf, (psk != NULL) ? psk : "", sizeof(wlan_sta_psk_buf));

    /* Stored for real, and that is the whole of it: this host will not use
     * them, and the state stays WLAN_ONLINE rather than pretending to
     * reconnect. */
    return wlan_credentials_set(wlan_sta_ssid_buf, wlan_sta_psk_buf);
}

bool wlan_credentials_import(char *ssid, size_t ssid_size, char *psk, size_t psk_size)
{
    /* No SDK station config to read, so only AutoConnect's blob -- which is
     * exactly the half that can be exercised here, given an emulated NVS
     * seeded with one. */
    ssid[0] = '\0';
    psk[0] = '\0';

    return wlan_credentials_import_blob(ssid, ssid_size, psk, psk_size);
}

const char *wlan_ap_ssid(void)
{
    return NULL;
}

uint32_t wlan_ap_ip(void)
{
    return 0;
}

#endif /* CONFIG_IDF_TARGET_LINUX */
