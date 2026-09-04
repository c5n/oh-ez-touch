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
