#ifndef WLAN_HPP
#define WLAN_HPP

#include "config.hpp"

#include <stddef.h>
#include <stdint.h>

/* Sized from AutoConnect's station_config_t, which is itself sized from the
 * SDK: uint8_t ssid[32] and password[64], each needing room for a terminator
 * this project's char arrays have to provide. A WPA2 passphrase is at most 63
 * characters, so the 64 is the SDK's own headroom, not ours to shrink. */
#define WLAN_SSID_SIZE 33
#define WLAN_PSK_SIZE  65

/* Where the radio currently is. WLAN_PORTAL is the pristine-device case: no
 * credentials are stored at all, so there is nothing to retry and the setup
 * access point stays up until someone provisions the device. */
enum wlan_state_e
{
    WLAN_IDLE = 0,
    WLAN_CONNECTING,
    WLAN_ONLINE,
    WLAN_RETRY_WAIT,
    WLAN_PORTAL
};

void wlan_setup(Config *config);
void wlan_loop(void);

/* Start another connection attempt now, whatever the retry timer says. */
void wlan_reconnect(void);

/* Adopt new credentials and connect with them. Keeps the access point up if
 * it was: the browser that submitted them is on it. */
bool wlan_set_credentials(const char *ssid, const char *psk);

enum wlan_state_e wlan_state(void);

/* The setup access point, or NULL when it is down. The address is returned
 * raw so that this header does not need IPAddress. */
const char *wlan_ap_ssid(void);
uint32_t    wlan_ap_ip(void);

/* What we are connected to, or trying to connect to. */
const char *wlan_sta_ssid(void);

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
