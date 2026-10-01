/**
 * @file pin_code.c
 *
 * See pin_code.h.
 */
#include "pin_code.h"

#include <string.h>

bool pin_code_valid(const char *pin)
{
    size_t len = 0;

    if (pin == NULL)
        return false;

    for (; pin[len] != '\0'; len++)
    {
        if (pin[len] < '0' || pin[len] > '9' || len >= PIN_CODE_MAX_LEN)
            return false;
    }

    return len >= PIN_CODE_MIN_LEN;
}

/* --- SHA-256, FIPS 180-4 --------------------------------------------------- */

static const uint32_t K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4,
    0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe,
    0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f,
    0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
    0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc,
    0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
    0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116,
    0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7,
    0xc67178f2};

#define ROR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))

static void sha256_block(uint32_t h[8], const uint8_t block[64])
{
    uint32_t w[64];
    uint32_t a, b, c, d, e, f, g, k;

    for (int i = 0; i < 16; i++)
        w[i] = ((uint32_t)block[4 * i] << 24) | ((uint32_t)block[4 * i + 1] << 16)
               | ((uint32_t)block[4 * i + 2] << 8) | (uint32_t)block[4 * i + 3];

    for (int i = 16; i < 64; i++)
    {
        uint32_t s0 = ROR(w[i - 15], 7) ^ ROR(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = ROR(w[i - 2], 17) ^ ROR(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], k = h[7];

    for (int i = 0; i < 64; i++)
    {
        uint32_t t1 = k + (ROR(e, 6) ^ ROR(e, 11) ^ ROR(e, 25)) + ((e & f) ^ (~e & g)) + K[i]
                      + w[i];
        uint32_t t2 = (ROR(a, 2) ^ ROR(a, 13) ^ ROR(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
        k = g, g = f, f = e, e = d + t1, d = c, c = b, b = a, a = t1 + t2;
    }

    h[0] += a, h[1] += b, h[2] += c, h[3] += d, h[4] += e, h[5] += f, h[6] += g, h[7] += k;
}

void pin_code_sha256(const void *data, size_t len, uint8_t out[PIN_CODE_HASH_LEN])
{
    uint32_t       h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                           0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    const uint8_t *p = (const uint8_t *)data;
    uint8_t        block[64];
    size_t         rest = len;
    uint64_t       bits = (uint64_t)len * 8u;

    for (; rest >= 64; rest -= 64, p += 64)
        sha256_block(h, p);

    /* The tail, the 0x80 marker and the length: one block, or two when the
     * tail leaves no room for the eight length bytes. */
    memset(block, 0, sizeof(block));
    memcpy(block, p, rest);
    block[rest] = 0x80;

    if (rest >= 56)
    {
        sha256_block(h, block);
        memset(block, 0, sizeof(block));
    }

    for (int i = 0; i < 8; i++)
        block[63 - i] = (uint8_t)(bits >> (8 * i));

    sha256_block(h, block);

    for (int i = 0; i < 8; i++)
    {
        out[4 * i] = (uint8_t)(h[i] >> 24);
        out[4 * i + 1] = (uint8_t)(h[i] >> 16);
        out[4 * i + 2] = (uint8_t)(h[i] >> 8);
        out[4 * i + 3] = (uint8_t)h[i];
    }
}

void pin_code_hash(const char *pin, const uint8_t salt[PIN_CODE_SALT_LEN],
                   uint8_t out[PIN_CODE_HASH_LEN])
{
    uint8_t buf[PIN_CODE_SALT_LEN + PIN_CODE_MAX_LEN];
    size_t  len = strnlen(pin, PIN_CODE_MAX_LEN);

    memcpy(buf, salt, PIN_CODE_SALT_LEN);
    memcpy(buf + PIN_CODE_SALT_LEN, pin, len);
    pin_code_sha256(buf, PIN_CODE_SALT_LEN + len, out);
    memset(buf, 0, sizeof(buf));
}

bool pin_code_equal(const uint8_t *a, const uint8_t *b, size_t len)
{
    uint8_t diff = 0;

    for (size_t i = 0; i < len; i++)
        diff |= (uint8_t)(a[i] ^ b[i]);

    return diff == 0;
}

void pin_code_to_hex(const uint8_t *bytes, size_t len, char *out)
{
    static const char digits[] = "0123456789abcdef";

    for (size_t i = 0; i < len; i++)
    {
        out[2 * i] = digits[bytes[i] >> 4];
        out[2 * i + 1] = digits[bytes[i] & 0x0f];
    }

    out[2 * len] = '\0';
}

static int hex_nibble(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

bool pin_code_from_hex(const char *hex, uint8_t *bytes, size_t len)
{
    if (hex == NULL || strlen(hex) != 2 * len)
        return false;

    for (size_t i = 0; i < len; i++)
    {
        int hi = hex_nibble(hex[2 * i]);
        int lo = hex_nibble(hex[2 * i + 1]);

        if (hi < 0 || lo < 0)
            return false;

        bytes[i] = (uint8_t)((hi << 4) | lo);
    }

    return true;
}

/* --- Lockout ---------------------------------------------------------------- */

void pin_lockout_fail(struct pin_lockout_s *lo, uint32_t now_ms)
{
    uint32_t length = PIN_LOCKOUT_MS;

    if (lo->failures < UINT16_MAX)
        lo->failures++;

    if (lo->failures % PIN_LOCKOUT_TRIES != 0)
        return;

    /* The first run of five waits 30 s, the second 60 s, and so on to 5 min. */
    for (uint16_t runs = lo->failures / PIN_LOCKOUT_TRIES; runs > 1 && length < PIN_LOCKOUT_MAX_MS;
         runs--)
        length *= 2;

    if (length > PIN_LOCKOUT_MAX_MS)
        length = PIN_LOCKOUT_MAX_MS;

    lo->active = true;
    lo->since_ms = now_ms;
    lo->length_ms = length;
}

void pin_lockout_success(struct pin_lockout_s *lo)
{
    memset(lo, 0, sizeof(*lo));
}

bool pin_lockout_active(struct pin_lockout_s *lo, uint32_t now_ms, uint32_t *remaining_ms)
{
    uint32_t elapsed;

    if (lo->active == true)
    {
        elapsed = now_ms - lo->since_ms;

        if (elapsed < lo->length_ms)
        {
            if (remaining_ms != NULL)
                *remaining_ms = lo->length_ms - elapsed;
            return true;
        }

        lo->active = false;
    }

    if (remaining_ms != NULL)
        *remaining_ms = 0;
    return false;
}
