/**
 * @file pin_store.h
 *
 * Where the panel's PINs are kept: one per scope, each a salted SHA-256 in
 * NVS through port_kv, never the digits themselves.
 *
 * NVS and not config.json, for the reason the WLAN credentials are there: it
 * survives an `idf.py flash` and an OTA. And it keeps the PINs out of
 * everything the field table feeds -- /api/config, the web form, MQTT's
 * config/ topics and the settings rows -- none of which has any business
 * showing one, even hashed.
 *
 * The two scopes are independent on purpose: who may switch a protected
 * light is not who may reconfigure the panel. pin_store_set() refuses a PIN
 * that would open the other scope too, so that knowing one never means
 * knowing both.
 */
#ifndef PIN_STORE_H
#define PIN_STORE_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The openHAB item tag that puts a sitemap tile behind the Item PIN. Exact and
 * case-sensitive, the way openHAB compares tags. */
#define PIN_ITEM_TAG "ohez-pin"

enum pin_scope_e
{
    PIN_SCOPE_SYSTEM, /* the System part of the settings */
    PIN_SCOPE_ITEM,   /* sitemap items tagged PIN_ITEM_TAG */
    PIN_SCOPE_COUNT
};

enum pin_set_result_e
{
    PIN_SET_OK,
    PIN_SET_INVALID,       /* not 4 to 8 digits */
    PIN_SET_SAME_AS_OTHER, /* would also open the other scope */
    PIN_SET_IO             /* NVS refused the write */
};

bool pin_store_is_set(enum pin_scope_e scope);
bool pin_store_check(enum pin_scope_e scope, const char *pin);
enum pin_set_result_e pin_store_set(enum pin_scope_e scope, const char *pin);
void pin_store_clear(enum pin_scope_e scope);

/* "system" and "item": the names REST, the control interface and the
 * simulator's environment use. NULL for anything else. */
const char *pin_scope_name(enum pin_scope_e scope);

/* The reverse; false for a name that is neither. */
bool pin_scope_from_name(const char *name, enum pin_scope_e *scope);

#ifdef __cplusplus
}
#endif

#endif /* PIN_STORE_H */
