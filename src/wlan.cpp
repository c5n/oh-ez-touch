/**
 * WLAN credentials.
 *
 * This firmware used to leave the radio and the credentials entirely to
 * AutoConnect. It keeps them itself now, in NVS rather than in
 * /config.json -- a config file is overwritten by `pio run -t uploadfs`,
 * which would silently unprovision every device it is uploaded to, while NVS
 * survives both that and an OTA.
 */

#include "wlan.hpp"

#include <Arduino.h>
#include <Preferences.h>
#include <string.h>
#include <WiFi.h>
#include <esp_wifi.h>

#ifndef DEBUG_WLAN
#define DEBUG_WLAN 0
#endif

/* Our own store. */
#define WLAN_NVS_NAMESPACE "oheztouch"
#define WLAN_NVS_KEY_SSID  "wlan_ssid"
#define WLAN_NVS_KEY_PSK   "wlan_psk"

/* AutoConnect's store. Its AC_IDENTIFIER served as both the namespace and the
 * key, so the repetition below is not a mistake. */
#define WLAN_AC_NVS_NAMESPACE "AC_CREDT"
#define WLAN_AC_NVS_KEY       "AC_CREDT"

/* The blob length comes out of a filesystem that can report anything when it
 * is corrupt, and it is used to size a malloc, so it needs a bound. Real
 * blobs run to a few hundred bytes: each entry is an SSID, a passphrase, a
 * BSSID and a flag. */
#define WLAN_AC_CREDT_MAX 1024

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

bool wlan_credentials_get(char *ssid, size_t ssid_size, char *psk, size_t psk_size)
{
    Preferences pref;

    ssid[0] = '\0';
    psk[0] = '\0';

    /* A read-only begin() fails when the namespace has never been written,
     * which is simply a device that has not been provisioned by us yet. */
    if (pref.begin(WLAN_NVS_NAMESPACE, true) == false)
        return false;

    pref.getString(WLAN_NVS_KEY_SSID, ssid, ssid_size);
    pref.getString(WLAN_NVS_KEY_PSK, psk, psk_size);
    pref.end();

    return (ssid[0] != '\0');
}

bool wlan_credentials_set(const char *ssid, const char *psk)
{
    Preferences pref;

    if (pref.begin(WLAN_NVS_NAMESPACE, false) == false)
    {
#if DEBUG_WLAN
        Serial.println("wlan: cannot open " WLAN_NVS_NAMESPACE " for writing");
#endif
        return false;
    }

    bool ok = (pref.putString(WLAN_NVS_KEY_SSID, ssid) > 0);

    /* An open network has an empty passphrase, so a zero-length write is the
     * expected outcome there and must not read as a failure. */
    pref.putString(WLAN_NVS_KEY_PSK, psk);
    pref.end();

    return ok;
}

/* The SDK's own station config. AutoConnect calls WiFi.persistent(true) and
 * its first connection attempt is a bare WiFi.begin(), so this holds the
 * credential the device actually last used -- which is the one thing its own
 * blob cannot tell us (see below). */
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

/* AutoConnect's own credential blob, as the fallback for a device whose SDK
 * store was cleared. Layout, from AutoConnectCredential.cpp's Preferences
 * variant:
 *
 *   byte 0      number of entries
 *   bytes 1..2  container size, little endian; not needed here
 *   per entry   SSID '\0' PASSPHRASE '\0' BSSID[6] DHCP
 *               and, only when DHCP == 1 (STA_STATIC), five big-endian
 *               uint32: ip, gateway, netmask, dns1, dns2
 *   trailing    one '\0'
 *
 * There are no address bytes at all in the DHCP case, so the entry stride is
 * not constant and the blob has to be walked rather than indexed.
 *
 * Only the first entry is adopted, and it is not necessarily the newest:
 * AutoConnect held its credentials in a std::map keyed by SSID and
 * re-serialised them in map order, so the blob is ordered alphabetically and
 * carries no recency information whatsoever. That is exactly why the SDK
 * config above is consulted first.
 *
 * The static-IP fields are skipped. This project has only ever been a DHCP
 * client, and adopting a static configuration from a store we are about to
 * stop using would be a surprising thing to inherit.
 */
static bool wlan_import_blob(char *ssid, size_t ssid_size, char *psk, size_t psk_size)
{
    Preferences pref;

    if (pref.begin(WLAN_AC_NVS_NAMESPACE, true) == false)
        return false;

    size_t size = pref.getBytesLength(WLAN_AC_NVS_KEY);

    if (size < 4 || size > WLAN_AC_CREDT_MAX)
    {
#if DEBUG_WLAN
        if (size != 0)
            Serial.printf("wlan: implausible AC_CREDT blob of %u bytes\r\n", (unsigned)size);
#endif
        pref.end();
        return false;
    }

    uint8_t *blob = (uint8_t *)malloc(size);

    if (blob == NULL)
    {
        pref.end();
        return false;
    }

    pref.getBytes(WLAN_AC_NVS_KEY, blob, size);
    pref.end();

    uint8_t entries = blob[0];
    size_t  dp      = 3;
    bool    found   = false;

    for (uint8_t i = 0; i < entries && dp + 1 < size; i++)
    {
        const char *entry_ssid = (const char *)&blob[dp];
        size_t      ssid_len   = strnlen(entry_ssid, size - dp);

        if (dp + ssid_len + 1 >= size) /* unterminated: the blob is corrupt */
            break;

        dp += ssid_len + 1;

        const char *entry_psk = (const char *)&blob[dp];
        size_t      psk_len   = strnlen(entry_psk, size - dp);

        if (dp + psk_len + 1 + 6 + 1 > size)
            break;

        dp += psk_len + 1 + 6; /* passphrase, then the BSSID */

        uint8_t dhcp = blob[dp++];

        if (dhcp == 1)
            dp += 5 * sizeof(uint32_t);

        /* The first well-formed entry wins. Its own terminators were checked
         * above, so corruption in a later entry does not make it any less
         * usable -- and refusing it on that basis would unprovision a device
         * that has a perfectly good credential sitting in front. */
        if (found == false && ssid_len > 0 && ssid_len < ssid_size && psk_len < psk_size)
        {
            memcpy(ssid, entry_ssid, ssid_len);
            ssid[ssid_len] = '\0';
            memcpy(psk, entry_psk, psk_len);
            psk[psk_len] = '\0';
            found = true;
        }
    }

    free(blob);

    return found;
}

bool wlan_credentials_import(char *ssid, size_t ssid_size, char *psk, size_t psk_size)
{
    ssid[0] = '\0';
    psk[0] = '\0';

    if (wlan_import_sdk(ssid, ssid_size, psk, psk_size) == true)
    {
#if DEBUG_WLAN
        Serial.printf("wlan: imported '%s' from the SDK station config\r\n", ssid);
#endif
        return true;
    }

    if (wlan_import_blob(ssid, ssid_size, psk, psk_size) == true)
    {
#if DEBUG_WLAN
        Serial.printf("wlan: imported '%s' from the AutoConnect blob\r\n", ssid);
#endif
        return true;
    }

    return false;
}

/* ----------------------------------------------------------- the state machine */

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

static Config           *wlan_config = NULL;
static enum wlan_state_e wlan_current = WLAN_IDLE;

static char wlan_sta_ssid_buf[WLAN_SSID_SIZE];
static char wlan_sta_psk_buf[WLAN_PSK_SIZE];

static bool wlan_ap_is_up = false;

/* One access-point window per offline episode, armed again once the station
 * has been back online. */
static bool wlan_ap_spent = false;

static unsigned long wlan_connect_deadline = 0;
static unsigned long wlan_retry_deadline = 0;
static unsigned long wlan_ap_deadline = 0;
static unsigned long wlan_ap_linger_deadline = 0;

/* Deadlines are compared as a signed difference so that they survive the
 * millis() rollover, which is the idiom the rest of this project uses. */
static bool wlan_due(unsigned long deadline)
{
    return ((long)(millis() - deadline) >= 0);
}

static void wlan_ap_raise(void)
{
    if (wlan_ap_is_up == true)
        return;

    /* AP_STA rather than AP: a connection attempt may well be in flight, and
     * dropping the station would abandon it. */
    WiFi.mode(WIFI_AP_STA);

    /* Open, as AutoConnect's was (-DAUTOCONNECT_PSK='""'), and named after
     * the host so that a rack of these is tellable apart. No softAPConfig():
     * the core default of 192.168.4.1 is what a user expects, where
     * AutoConnect used 172.217.28.1 -- a Google address picked to bait
     * captive-portal detection, which is pointless without the DNS hijack we
     * deliberately do not do. */
    WiFi.softAP(wlan_config->item.general.hostname);

    wlan_ap_is_up = true;
    wlan_ap_deadline = millis() + WLAN_AP_TIMEOUT;

#if DEBUG_WLAN
    Serial.printf("wlan: AP '%s' up at %s\r\n",
                  wlan_config->item.general.hostname,
                  WiFi.softAPIP().toString().c_str());
#endif
}

static void wlan_ap_drop(void)
{
    if (wlan_ap_is_up == false)
        return;

    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
    wlan_ap_is_up = false;

#if DEBUG_WLAN
    Serial.println("wlan: AP down");
#endif
}

static void wlan_connect_begin(void)
{
    WiFi.begin(wlan_sta_ssid_buf, wlan_sta_psk_buf);

    wlan_current = WLAN_CONNECTING;
    wlan_connect_deadline = millis() + WLAN_CONNECT_TIMEOUT;

#if DEBUG_WLAN
    Serial.printf("wlan: connecting to '%s'\r\n", wlan_sta_ssid_buf);
#endif
}

/* Entering an offline episode: start the retry timer, and open the access
 * point once so that a device with wrong credentials can be corrected. */
static void wlan_go_offline(void)
{
    wlan_current = WLAN_RETRY_WAIT;
    wlan_retry_deadline = millis() + WLAN_RETRY_INTERVAL;

    if (wlan_ap_spent == false)
    {
        wlan_ap_raise();
        wlan_ap_spent = true;
    }
}

void wlan_setup(Config *config)
{
    wlan_config = config;

    /* We own the credentials now, so the SDK need not write its own copy on
     * every begin(). This also leaves AutoConnect's copy untouched, which is
     * what lets a downgrade still find its credentials. */
    WiFi.persistent(false);

    /* Before mode(): setHostname() only records the name, which is applied
     * when the station interface is created. AutoConnect had these the other
     * way round, and the hostname is what tools/batchupdate.sh resolves
     * devices by. */
    WiFi.setHostname(config->item.general.hostname);

    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    WiFi.setAutoReconnect(true);

    bool have = wlan_credentials_get(wlan_sta_ssid_buf, sizeof(wlan_sta_ssid_buf),
                                     wlan_sta_psk_buf, sizeof(wlan_sta_psk_buf));

    /* Nothing of ours stored: adopt whatever AutoConnect left behind, so that
     * a device provisioned through its portal does not have to be provisioned
     * again. Deliberately not gated behind a "already imported" marker -- the
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

#if DEBUG_WLAN
    Serial.println("wlan: no credentials, waiting to be provisioned");
#endif
}

void wlan_loop(void)
{
    switch (wlan_current)
    {
    case WLAN_CONNECTING:
        if (WiFi.status() == WL_CONNECTED)
        {
            wlan_current = WLAN_ONLINE;
            wlan_ap_linger_deadline = millis() + WLAN_AP_LINGER;
            wlan_ap_spent = false;

#if DEBUG_WLAN
            Serial.printf("wlan: online as %s\r\n", WiFi.localIP().toString().c_str());
#endif
        }
        else if (wlan_due(wlan_connect_deadline) == true)
        {
            WiFi.disconnect(false);
            wlan_go_offline();
        }
        break;

    case WLAN_ONLINE:
        if (WiFi.status() != WL_CONNECTED)
            wlan_go_offline();
        else if (wlan_ap_is_up == true && wlan_due(wlan_ap_linger_deadline) == true)
            wlan_ap_drop();
        break;

    case WLAN_RETRY_WAIT:
        if (wlan_ap_is_up == true && wlan_due(wlan_ap_deadline) == true)
            wlan_ap_drop();

        if (wlan_due(wlan_retry_deadline) == true)
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

enum wlan_state_e wlan_state(void)
{
    return wlan_current;
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

    return (uint32_t)WiFi.softAPIP();
}

const char *wlan_sta_ssid(void)
{
    return wlan_sta_ssid_buf;
}
