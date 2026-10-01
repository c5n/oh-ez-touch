/**
 * @file pin_code.h
 *
 * The arithmetic behind the panel's PINs: what counts as one, how it is
 * hashed before it is stored, and how long a run of wrong guesses locks the
 * pad. Nothing here knows where a PIN is kept or what it protects -- that is
 * pin_store and ui_pin -- which is what lets the host tests reach all of it.
 *
 * The SHA-256 is our own, eighty lines of FIPS 180-4. IDF 6.1 ships mbedtls 4,
 * which no longer exports mbedtls_sha256(); its replacement is the PSA
 * crypto API, with an init call and a driver behind it, and neither is in the
 * host test build. Hashing eight digits once per tap does not need either.
 *
 * Includes nothing but libc.
 */
#ifndef PIN_CODE_H
#define PIN_CODE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PIN_CODE_MIN_LEN  4
#define PIN_CODE_MAX_LEN  8
#define PIN_CODE_SALT_LEN 16
#define PIN_CODE_HASH_LEN 32

/* A run of this many wrong entries starts a lockout; every further run of as
 * many doubles it, up to the cap. */
#define PIN_LOCKOUT_TRIES  5
#define PIN_LOCKOUT_MS     30000u
#define PIN_LOCKOUT_MAX_MS 300000u

/** 4 to 8 characters, every one of them a decimal digit. */
bool pin_code_valid(const char *pin);

/** SHA-256 of `len` bytes. Exposed for the test vectors. */
void pin_code_sha256(const void *data, size_t len, uint8_t out[PIN_CODE_HASH_LEN]);

/** SHA-256 of the salt followed by the PIN's digits. */
void pin_code_hash(const char *pin, const uint8_t salt[PIN_CODE_SALT_LEN],
                   uint8_t out[PIN_CODE_HASH_LEN]);

/** Compares in time independent of where the first difference is. */
bool pin_code_equal(const uint8_t *a, const uint8_t *b, size_t len);

/** Lower-case hex of `len` bytes into `out`, which takes 2 * len + 1. */
void pin_code_to_hex(const uint8_t *bytes, size_t len, char *out);

/** The reverse; false unless `hex` is exactly 2 * len hex digits. */
bool pin_code_from_hex(const char *hex, uint8_t *bytes, size_t len);

/* Wrong guesses for one PIN, against a millisecond clock the caller passes in
 * -- port_millis() on the panel, a number the test moves by hand. Zeroed is
 * "no failures, not locked". Comparisons are on unsigned differences, so the
 * 32-bit clock may roll over inside a lockout. */
struct pin_lockout_s
{
    uint16_t failures; /* consecutive, since the last correct entry */
    bool     active;
    uint32_t since_ms;
    uint32_t length_ms;
};

void pin_lockout_fail(struct pin_lockout_s *lo, uint32_t now_ms);
void pin_lockout_success(struct pin_lockout_s *lo);

/** true while entries are refused; `remaining_ms` (may be NULL) says how long. */
bool pin_lockout_active(struct pin_lockout_s *lo, uint32_t now_ms, uint32_t *remaining_ms);

#ifdef __cplusplus
}
#endif

#endif /* PIN_CODE_H */
