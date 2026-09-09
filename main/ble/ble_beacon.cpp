/**
 * @file ble_beacon.cpp
 *
 * See ble_beacon.hpp.
 */

#include "ble_beacon.hpp"

#include <math.h>
#include <stdio.h>
#include <string.h>

/* Advertising data types, from the Bluetooth assigned numbers. Only the four
 * that carry something worth having. */
#define AD_TYPE_UUID16_COMPLETE 0x03
#define AD_TYPE_NAME_SHORT      0x08
#define AD_TYPE_NAME_COMPLETE   0x09
#define AD_TYPE_TX_POWER        0x0A
#define AD_TYPE_SERVICE_DATA16  0x16
#define AD_TYPE_MANUFACTURER    0xFF

#define COMPANY_APPLE     0x004Cu
#define IBEACON_SUBTYPE   0x02
#define IBEACON_SUBLEN    0x15

#define UUID_EDDYSTONE    0xFEAAu

#define EDDYSTONE_FRAME_UID 0x00
#define EDDYSTONE_FRAME_URL 0x10
#define EDDYSTONE_FRAME_TLM 0x20

/* Eddystone reports the power at 0 m where iBeacon reports it at 1 m, and the
 * specification's own advice for converting between them is to subtract 41 dB.
 * Done on the way in so that ble_beacon_s::power means one thing. */
#define EDDYSTONE_POWER_OFFSET_DB 41

static const char *const kind_names[BLE_BEACON_KIND_COUNT] = {
    "device", "iBeacon", "Eddystone-UID", "Eddystone-URL"};

const char *ble_beacon_kind_name(enum ble_beacon_kind_e kind)
{
    if ((unsigned)kind >= BLE_BEACON_KIND_COUNT)
        return kind_names[BLE_BEACON_NONE];

    return kind_names[kind];
}

/* ------------------------------------------------------------------ pieces */

static uint16_t be16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] << 8 | p[1]);
}

static uint16_t le16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[1] << 8 | p[0]);
}

static void copy_name(struct ble_beacon_s *out, const uint8_t *d, uint8_t dl)
{
    size_t n = dl;

    if (n >= sizeof(out->name))
        n = sizeof(out->name) - 1;

    /* A name is UTF-8 and may legally contain anything, including the
     * characters that would end a topic segment early. Whatever arrives is
     * copied verbatim here and made safe where it is used -- see
     * ble_scan.cpp -- because this file has no idea what it will be used
     * for. */
    memcpy(out->name, d, n);
    out->name[n] = '\0';
}

static void parse_ibeacon(struct ble_beacon_s *out, const uint8_t *d, uint8_t dl)
{
    /* company(2) subtype(1) sublen(1) uuid(16) major(2) minor(2) power(1) */
    if (dl < 25)
        return;

    if (d[2] != IBEACON_SUBTYPE || d[3] != IBEACON_SUBLEN)
        return;

    const uint8_t *uuid = &d[4];

    out->kind = BLE_BEACON_IBEACON;

    snprintf(out->id, sizeof(out->id),
             "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-"
             "%02x%02x%02x%02x%02x%02x-%u-%u",
             uuid[0], uuid[1], uuid[2], uuid[3], uuid[4], uuid[5], uuid[6], uuid[7],
             uuid[8], uuid[9], uuid[10], uuid[11], uuid[12], uuid[13], uuid[14], uuid[15],
             (unsigned)be16(&d[20]), (unsigned)be16(&d[22]));

    /* The last byte is the power measured at one metre, which is what
     * ble_beacon_distance() wants and the only reference power in any of these
     * formats that needs no correction. */
    out->ref_power = (int8_t)d[24];
}

/* 0x00 to 0x03 of an Eddystone URL, expanded. The specification has four. */
static const char *const url_schemes[] = {
    "http://www.", "https://www.", "http://", "https://"};

/* 0x00 to 0x0D inside one, likewise. Everything from 0x0E to 0x20 is reserved
 * and everything from 0x7F up is, so those are dropped rather than guessed
 * at. */
static const char *const url_expansions[] = {
    ".com/", ".org/", ".edu/", ".net/", ".info/", ".biz/", ".gov/",
    ".com",  ".org",  ".edu",  ".net",  ".info",  ".biz",  ".gov"};

static void append(char *dst, size_t size, size_t *used, const char *s)
{
    while (*s != '\0' && *used + 1 < size)
        dst[(*used)++] = *s++;

    dst[*used] = '\0';
}

static void parse_eddystone_url(struct ble_beacon_s *out, const uint8_t *d, uint8_t dl)
{
    /* uuid(2) frame(1) power(1) scheme(1) then at least one byte of URL */
    if (dl < 6)
        return;

    uint8_t scheme = d[4];

    if (scheme >= (uint8_t)(sizeof(url_schemes) / sizeof(url_schemes[0])))
        return;

    out->kind = BLE_BEACON_EDDYSTONE_URL;
    out->ref_power = (int8_t)((int)(int8_t)d[3] - EDDYSTONE_POWER_OFFSET_DB);

    size_t used = 0;

    out->id[0] = '\0';
    append(out->id, sizeof(out->id), &used, url_schemes[scheme]);

    for (uint8_t i = 5; i < dl; i++)
    {
        uint8_t c = d[i];

        if (c < sizeof(url_expansions) / sizeof(url_expansions[0]))
        {
            append(out->id, sizeof(out->id), &used, url_expansions[c]);
        }
        else if (c >= 0x21 && c <= 0x7E)
        {
            char one[2] = {(char)c, '\0'};

            append(out->id, sizeof(out->id), &used, one);
        }
        /* Everything else -- 0x0E to 0x20 and 0x7F upwards -- is reserved by
         * the specification and is skipped. Guessing at one would put a
         * plausible but wrong URL into the topic, and a wrong URL resolves. */
    }
}

static void parse_eddystone(struct ble_beacon_s *out, const uint8_t *d, uint8_t dl)
{
    /* uuid(2) frame(1) ... */
    if (dl < 3)
        return;

    switch (d[2])
    {
    case EDDYSTONE_FRAME_UID:
        /* uuid(2) frame(1) ranging(1) namespace(10) instance(6), and two
         * reserved bytes that real beacons sometimes omit -- hence 20 and not
         * 22. */
        if (dl < 20)
            return;

        out->kind = BLE_BEACON_EDDYSTONE_UID;
        out->ref_power = (int8_t)((int)(int8_t)d[3] - EDDYSTONE_POWER_OFFSET_DB);

        snprintf(out->id, sizeof(out->id),
                 "%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x-%02x%02x%02x%02x%02x%02x",
                 d[4], d[5], d[6], d[7], d[8], d[9], d[10], d[11], d[12], d[13],
                 d[14], d[15], d[16], d[17], d[18], d[19]);
        break;

    case EDDYSTONE_FRAME_URL:
        parse_eddystone_url(out, d, dl);
        break;

    case EDDYSTONE_FRAME_TLM:
        /* uuid(2) frame(1) version(1) battery(2) temperature(2) count(4)
         * uptime(4). Only version 0 is unencrypted; version 1 is the encrypted
         * variant and its fields are not these. */
        if (dl < 12 || d[3] != 0x00)
            return;

        /* The kind is left alone: a TLM frame carries no identity, and the
         * advertiser's identity came in a different advertisement. Overwriting
         * it here is exactly the bug the merge in ble_scan.cpp exists to
         * avoid. */
        out->have_telemetry = true;
        out->battery_mv = be16(&d[4]);

        /* 8.8 fixed point, signed. 0x8000 means "not supported", which is
         * -128.0 read naively and would be published as a temperature. */
        if (be16(&d[6]) != 0x8000u)
            out->temperature_c = (float)(int16_t)be16(&d[6]) / 256.0f;
        break;

    default:
        break;
    }
}

/* -------------------------------------------------------------------- walk */

void ble_beacon_parse(const uint8_t *adv, size_t len, struct ble_beacon_s *out)
{
    memset(out, 0, sizeof(*out));

    out->kind = BLE_BEACON_NONE;
    out->tx_power = BLE_BEACON_POWER_UNKNOWN;
    out->ref_power = BLE_BEACON_POWER_UNKNOWN;

    if (adv == NULL)
        return;

    size_t i = 0;

    while (i < len)
    {
        uint8_t field_len = adv[i];

        /* A zero length is the specified early terminator for the padding at
         * the end of a 31 byte payload. */
        if (field_len == 0)
            break;

        /* The length counts the type byte, so a structure needs field_len + 1
         * bytes in total and at least one of them has to be the type. Either
         * of these failing means the payload is truncated or malformed, and
         * there is nothing sensible past it. */
        if (field_len < 1 || i + 1 + field_len > len)
            break;

        uint8_t        type = adv[i + 1];
        const uint8_t *d = &adv[i + 2];
        uint8_t        dl = (uint8_t)(field_len - 1);

        switch (type)
        {
        case AD_TYPE_NAME_COMPLETE:
            copy_name(out, d, dl);
            break;

        case AD_TYPE_NAME_SHORT:
            /* Only if nothing better arrived: an advertiser may send both, and
             * the complete one wins whichever order they come in. */
            if (out->name[0] == '\0')
                copy_name(out, d, dl);
            break;

        case AD_TYPE_TX_POWER:
            if (dl >= 1)
                out->tx_power = (int8_t)d[0];
            break;

        case AD_TYPE_MANUFACTURER:
            if (dl >= 2 && le16(d) == COMPANY_APPLE)
                parse_ibeacon(out, d, dl);
            break;

        case AD_TYPE_SERVICE_DATA16:
            if (dl >= 2 && le16(d) == UUID_EDDYSTONE)
                parse_eddystone(out, d, dl);
            break;

        case AD_TYPE_UUID16_COMPLETE:
            /* Eddystone advertises 0xFEAA here as well as in its service data.
             * Nothing is taken from it: the service data is what carries the
             * frame, and a device may list the UUID without sending one. */
            break;

        default:
            break;
        }

        i += 1u + field_len;
    }
}

float ble_beacon_distance(int8_t rssi, int8_t power_at_1m)
{
    if (power_at_1m == BLE_BEACON_POWER_UNKNOWN || rssi == 127)
        return -1.0f;

    /* d = 10 ^ ((P1 - RSSI) / (10 n)), n = 2. At RSSI == P1 this is exactly
     * one metre, which is the definition of the reference power. */
    return powf(10.0f, (float)((int)power_at_1m - (int)rssi) / 20.0f);
}
