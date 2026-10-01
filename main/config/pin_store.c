/**
 * @file pin_store.c
 *
 * See pin_store.h.
 */
#include "pin_store.h"

#include <stdio.h>
#include <string.h>

#include "esp_random.h"
#include "pin_code.h"
#include "port/port_kv.h"

#define PIN_NVS_NAMESPACE "ohez"

struct pin_slot_s
{
    const char *name;
    const char *salt_key;
    const char *hash_key;
    bool        loaded;
    bool        set;
    uint8_t     salt[PIN_CODE_SALT_LEN];
    uint8_t     hash[PIN_CODE_HASH_LEN];
};

/* Read from NVS once, on first use, and kept: is_set() is asked by every
 * status line and every report, and NVS is flash. */
static struct pin_slot_s slots[PIN_SCOPE_COUNT] = {
    {"system", "sys_salt", "sys_hash", false, false, {0}, {0}},
    {"item", "item_salt", "item_hash", false, false, {0}, {0}},
};

static struct pin_slot_s *slot_get(enum pin_scope_e scope)
{
    struct pin_slot_s *s;
    char               hex[2 * PIN_CODE_HASH_LEN + 1];

    if ((unsigned)scope >= PIN_SCOPE_COUNT)
        return NULL;

    s = &slots[scope];

    if (s->loaded == false)
    {
        s->loaded = true;
        s->set = (port_kv_get_str(PIN_NVS_NAMESPACE, s->salt_key, hex, sizeof(hex)) == ESP_OK)
                 && pin_code_from_hex(hex, s->salt, sizeof(s->salt))
                 && (port_kv_get_str(PIN_NVS_NAMESPACE, s->hash_key, hex, sizeof(hex)) == ESP_OK)
                 && pin_code_from_hex(hex, s->hash, sizeof(s->hash));
    }

    return s;
}

static bool slot_matches(const struct pin_slot_s *s, const char *pin)
{
    uint8_t hash[PIN_CODE_HASH_LEN];
    bool    ok;

    if (s->set == false || pin_code_valid(pin) == false)
        return false;

    pin_code_hash(pin, s->salt, hash);
    ok = pin_code_equal(hash, s->hash, sizeof(hash));
    memset(hash, 0, sizeof(hash));
    return ok;
}

bool pin_store_is_set(enum pin_scope_e scope)
{
    struct pin_slot_s *s = slot_get(scope);

    return (s != NULL) && s->set;
}

bool pin_store_check(enum pin_scope_e scope, const char *pin)
{
    struct pin_slot_s *s = slot_get(scope);

    return (s != NULL) && slot_matches(s, pin);
}

enum pin_set_result_e pin_store_set(enum pin_scope_e scope, const char *pin)
{
    struct pin_slot_s *s = slot_get(scope);
    char               hex[2 * PIN_CODE_HASH_LEN + 1];
    uint8_t            salt[PIN_CODE_SALT_LEN];
    uint8_t            hash[PIN_CODE_HASH_LEN];
    esp_err_t          err;

    if (s == NULL || pin_code_valid(pin) == false)
        return PIN_SET_INVALID;

    for (int other = 0; other < PIN_SCOPE_COUNT; other++)
    {
        if (other != (int)scope && slot_matches(slot_get((enum pin_scope_e)other), pin))
            return PIN_SET_SAME_AS_OTHER;
    }

    /* A fresh salt every time, so the same PIN set twice is stored twice
     * differently. */
    esp_fill_random(salt, sizeof(salt));
    pin_code_hash(pin, salt, hash);

    pin_code_to_hex(salt, sizeof(salt), hex);
    err = port_kv_set_str(PIN_NVS_NAMESPACE, s->salt_key, hex);

    if (err == ESP_OK)
    {
        pin_code_to_hex(hash, sizeof(hash), hex);
        err = port_kv_set_str(PIN_NVS_NAMESPACE, s->hash_key, hex);
    }

    if (err != ESP_OK)
    {
        printf("pin_store: %s PIN not saved (%s)\r\n", s->name, esp_err_to_name(err));
        /* Whatever half made it to flash, the next boot must not read it. */
        port_kv_erase(PIN_NVS_NAMESPACE, s->salt_key);
        s->loaded = false;
        return PIN_SET_IO;
    }

    memcpy(s->salt, salt, sizeof(salt));
    memcpy(s->hash, hash, sizeof(hash));
    s->set = true;
    printf("pin_store: %s PIN set\r\n", s->name);
    return PIN_SET_OK;
}

void pin_store_clear(enum pin_scope_e scope)
{
    struct pin_slot_s *s = slot_get(scope);

    if (s == NULL)
        return;

    port_kv_erase(PIN_NVS_NAMESPACE, s->salt_key);
    port_kv_erase(PIN_NVS_NAMESPACE, s->hash_key);
    memset(s->salt, 0, sizeof(s->salt));
    memset(s->hash, 0, sizeof(s->hash));
    s->set = false;
    printf("pin_store: %s PIN cleared\r\n", s->name);
}

const char *pin_scope_name(enum pin_scope_e scope)
{
    return ((unsigned)scope < PIN_SCOPE_COUNT) ? slots[scope].name : NULL;
}

bool pin_scope_from_name(const char *name, enum pin_scope_e *scope)
{
    for (int i = 0; name != NULL && i < PIN_SCOPE_COUNT; i++)
    {
        if (strcmp(name, slots[i].name) == 0)
        {
            *scope = (enum pin_scope_e)i;
            return true;
        }
    }

    return false;
}
