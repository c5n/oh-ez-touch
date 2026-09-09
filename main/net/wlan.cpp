/**
 * @file wlan.cpp
 *
 * WLAN credentials. The radio itself is wlan_radio.cpp.
 *
 * This firmware used to leave the radio and the credentials entirely to
 * AutoConnect. It keeps them itself now, in NVS rather than in config.json --
 * the config file is overwritten whenever the filesystem image is flashed,
 * which would silently unprovision every device it is written to, while NVS
 * survives both that and an OTA.
 *
 * Nothing here is device-only. NVS is emulated on the host, so the credential
 * store, the AutoConnect migration and its blob parser all run and can be
 * exercised there -- which matters, because the parser walks a format this
 * project cannot produce any more.
 */

#include "wlan.hpp"

#include "debug.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "port/port_kv.h"

/* Our own store. */
#define WLAN_NVS_NAMESPACE "oheztouch"
#define WLAN_NVS_KEY_SSID  "wlan_ssid"
#define WLAN_NVS_KEY_PSK   "wlan_psk"

/* AutoConnect's store. Its AC_IDENTIFIER served as both the namespace and the
 * key, so the repetition below is not a mistake. */
#define WLAN_AC_NVS_NAMESPACE "AC_CREDT"
#define WLAN_AC_NVS_KEY       "AC_CREDT"

/* The blob length comes out of a store that can report anything when it is
 * corrupt, and it is used to size a malloc, so it needs a bound. Real blobs
 * run to a few hundred bytes: each entry is an SSID, a passphrase, a BSSID and
 * a flag. */
#define WLAN_AC_CREDT_MAX 1024

bool wlan_credentials_get(char *ssid, size_t ssid_size, char *psk, size_t psk_size)
{
    ssid[0] = '\0';
    psk[0] = '\0';

    /* Either read may fail with ESP_ERR_NVS_NOT_FOUND, which is simply a device
     * this firmware has not provisioned yet. port_kv clears the buffer on
     * failure, so there is nothing to check but the result below. */
    port_kv_get_str(WLAN_NVS_NAMESPACE, WLAN_NVS_KEY_SSID, ssid, ssid_size);
    port_kv_get_str(WLAN_NVS_NAMESPACE, WLAN_NVS_KEY_PSK, psk, psk_size);

    return (ssid[0] != '\0');
}

bool wlan_credentials_set(const char *ssid, const char *psk)
{
    if (port_kv_set_str(WLAN_NVS_NAMESPACE, WLAN_NVS_KEY_SSID, ssid) != ESP_OK)
    {
#if CONFIG_OHEZ_DEBUG_WLAN
        printf("wlan: cannot write " WLAN_NVS_NAMESPACE "/" WLAN_NVS_KEY_SSID "\r\n");
#endif
        return false;
    }

    /* An open network has an empty passphrase, so a zero-length write is the
     * expected outcome there and must not read as a failure. */
    port_kv_set_str(WLAN_NVS_NAMESPACE, WLAN_NVS_KEY_PSK, (psk != NULL) ? psk : "");

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
 * carries no recency information whatsoever. That is exactly why the SDK's own
 * station config, in wlan_radio.cpp, is consulted first.
 *
 * The static-IP fields are skipped. This project has only ever been a DHCP
 * client, and adopting a static configuration from a store we are about to
 * stop using would be a surprising thing to inherit.
 */
bool wlan_credentials_import_blob(char *ssid, size_t ssid_size, char *psk, size_t psk_size)
{
    ssize_t size = port_kv_blob_size(WLAN_AC_NVS_NAMESPACE, WLAN_AC_NVS_KEY);

    if (size < 4 || size > WLAN_AC_CREDT_MAX)
    {
#if CONFIG_OHEZ_DEBUG_WLAN
        if (size > 0)
            printf("wlan: implausible AC_CREDT blob of %u bytes\r\n", (unsigned)size);
#endif
        return false;
    }

    uint8_t *blob = (uint8_t *)malloc((size_t)size);

    if (blob == NULL)
        return false;

    if (port_kv_get_blob(WLAN_AC_NVS_NAMESPACE, WLAN_AC_NVS_KEY, blob, (size_t)size) != size)
    {
        free(blob);
        return false;
    }

    uint8_t entries = blob[0];
    size_t  dp      = 3;
    bool    found   = false;

    for (uint8_t i = 0; i < entries && dp + 1 < (size_t)size; i++)
    {
        const char *entry_ssid = (const char *)&blob[dp];
        size_t      ssid_len   = strnlen(entry_ssid, (size_t)size - dp);

        if (dp + ssid_len + 1 >= (size_t)size) /* unterminated: the blob is corrupt */
            break;

        dp += ssid_len + 1;

        const char *entry_psk = (const char *)&blob[dp];
        size_t      psk_len   = strnlen(entry_psk, (size_t)size - dp);

        if (dp + psk_len + 1 + 6 + 1 > (size_t)size)
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
