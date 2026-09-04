#ifndef WLAN_HPP
#define WLAN_HPP

#include <stddef.h>

/* Sized from AutoConnect's station_config_t, which is itself sized from the
 * SDK: uint8_t ssid[32] and password[64], each needing room for a terminator
 * this project's char arrays have to provide. A WPA2 passphrase is at most 63
 * characters, so the 64 is the SDK's own headroom, not ours to shrink. */
#define WLAN_SSID_SIZE 33
#define WLAN_PSK_SIZE  65

/* Credentials this firmware stored itself. False when none have been stored
 * yet, which is the pristine-device case and not an error. */
bool wlan_credentials_get(char *ssid, size_t ssid_size, char *psk, size_t psk_size);

bool wlan_credentials_set(const char *ssid, const char *psk);

/* Credentials left behind by AutoConnect, so that a device provisioned through
 * its portal does not have to be provisioned again. Reads only -- the caller
 * decides whether to adopt the result. Requires the WiFi driver to be up
 * (WiFi.mode() having been called), because the first of the two sources it
 * consults is the SDK's own station config. */
bool wlan_credentials_import(char *ssid, size_t ssid_size, char *psk, size_t psk_size);

#endif // WLAN_HPP
