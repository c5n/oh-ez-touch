#ifndef BLE_BEACON_HPP
#define BLE_BEACON_HPP

/**
 * @file ble_beacon.hpp
 *
 * What one BLE advertisement says about itself.
 *
 * This is the whole of the beacon knowledge in the firmware, and it is
 * deliberately free of everything else: no radio, no LVGL, no network, no
 * Config. It takes the bytes port_ble.h hands up and fills in a struct. That is
 * what lets test/host cover the three formats byte by byte, which matters more
 * here than anywhere else in the project -- these layouts are specified
 * elsewhere and cannot be checked by reading the code that consumes them.
 *
 * Three formats, which is what a beacon in the wild is in practice:
 *
 *   iBeacon        Apple's, and the one most tags ship as. Manufacturer data,
 *                  company 0x004C: a 16 byte proximity UUID plus a major and a
 *                  minor, which together are the identity, and the power
 *                  measured at one metre.
 *   Eddystone-UID  Google's. Service data under UUID 0xFEAA: a 10 byte
 *                  namespace and a 6 byte instance.
 *   Eddystone-URL  the same, carrying a compressed URL instead of an id.
 *
 * and Eddystone-TLM, which is not an identity at all but telemetry -- battery
 * and temperature -- interleaved between the frames that are. A parse of one
 * advertisement therefore fills in *part* of what is known about an advertiser,
 * and ble_scan.cpp merges successive parses into one entry.
 *
 * Anything else -- a phone, a watch, a thermostat -- comes back as
 * BLE_BEACON_NONE with whatever name and transmit power it advertised, which is
 * often enough to be useful and is why those are parsed for every kind.
 */

#include <stddef.h>
#include <stdint.h>

enum ble_beacon_kind_e
{
    BLE_BEACON_NONE = 0, /* a BLE device that is not a beacon */
    BLE_BEACON_IBEACON,
    BLE_BEACON_EDDYSTONE_UID,
    BLE_BEACON_EDDYSTONE_URL,
    BLE_BEACON_KIND_COUNT
};

/** No transmit power was advertised. 127 cannot be a real one: the field is
 * signed dBm and the range is -127..+20. */
#define BLE_BEACON_POWER_UNKNOWN 127

/* The longest identity any of these formats can produce, plus its terminator.
 *
 * That is the iBeacon one: a dashed UUID (36) and the major and minor, each up
 * to five digits, with a dash before each -- 48 characters, as in
 * "f7826da6-4fa2-4e98-8024-bc5b71e0893e-65535-65535". An Eddystone-UID is 20
 * hex digits, a dash and 12 more, so 33. An Eddystone URL can be longer than
 * either and is truncated, which is visible rather than silent: a truncated URL
 * does not resolve. */
#define BLE_BEACON_ID_SIZE 52

/* The longest name worth keeping. A Complete Local Name may be up to 29 bytes
 * in a legacy advertisement, so this truncates nothing that fits in one. */
#define BLE_BEACON_NAME_SIZE 30

/* The UUID list at its longest: the 16-bit list structure can hold fourteen
 * entries -- 28 bytes of the 31 a payload has -- which is 69 characters with
 * the separators, and a 128-bit one a 32 character UUID. Generous rather than
 * exact, because truncation here is harmless and a size that is both the
 * longest and a round number is easier to hold in one's head. */
#define BLE_BEACON_UUIDS_SIZE 96

/* Manufacturer data and service data at their longest: the AD structure can
 * hold 28 bytes of data, so 56 hex characters, and the company or service
 * UUID is inside those. Sized to a round number above that for the same
 * reason as the UUID list. */
#define BLE_BEACON_MFR_SIZE 60

struct ble_beacon_s
{
    enum ble_beacon_kind_e kind;

    /* The beacon's own identifier, or "" for a device that has none. Never the
     * hardware address: an address is what ble_scan.cpp keys its table on, and
     * for half the beacons on the market it is randomised and says nothing. */
    char id[BLE_BEACON_ID_SIZE];

    /* Advertised local name, or "". */
    char name[BLE_BEACON_NAME_SIZE];

    /**
     * AD type 0x0A, the advertiser's transmit power, or
     * BLE_BEACON_POWER_UNKNOWN.
     *
     * Factual, and useless for ranging. It says how loudly the radio speaks,
     * not how loud it is one metre away, and between the two sit the antenna,
     * the case and whatever the device is sitting on. Treating one as the other
     * puts a device three metres away at a hundred and forty, which is not an
     * optimistic estimate but a wrong number wearing the same units as a right
     * one -- hence the separate field below, and hence ble_scan.cpp publishing
     * a distance only when it has that one.
     */
    int8_t tx_power;

    /**
     * The power a beacon format declares at one metre, or
     * BLE_BEACON_POWER_UNKNOWN.
     *
     * iBeacon states it at one metre, which is what ble_beacon_distance()
     * wants. Eddystone states it at zero metres and it is corrected on the way
     * in by the 41 dB the specification gives for the conversion. Only a beacon
     * has one, because only a beacon is calibrated: this is the field that
     * separates a distance worth publishing from a number.
     */
    int8_t ref_power;

    /* Eddystone-TLM, when this advertisement was one. */
    bool     have_telemetry;
    uint16_t battery_mv;
    float    temperature_c;

    /**
     * The service UUIDs the advertisement lists, comma-separated hex, or "".
     *
     * 16-bit UUIDs from the two list structures and 128-bit ones from theirs,
     * in the order the advertisement listed them and each in the big-endian
     * form every other tool prints -- the wire order is little-endian, and
     * 0xAA, 0xFE on the wire is the UUID 0xFEAA. The lists say what a device
     * is -- 0x180F is the battery service, 0xFD6F the exposure notification
     * one -- which is most of the identity a plain device offers. Truncated
     * at the buffer, visibly: a shorter list is a shorter list and not a
     * different one.
     */
    char uuids[BLE_BEACON_UUIDS_SIZE];

    /**
     * Manufacturer-specific data that is not an iBeacon, as hex, or "".
     *
     * Company identifier first, little-endian as it is on the wire, then the
     * payload. Raw on purpose: what a manufacturer puts there is theirs to
     * define and nobody here has a better idea, and hex loses nothing. Apple
     * data that did not parse as an iBeacon lands here too.
     */
    char manufacturer[BLE_BEACON_MFR_SIZE];

    /**
     * Service data under some UUID other than Eddystone's, as hex, or "".
     * The UUID first, little-endian as it is on the wire, then the payload --
     * the same shape as the manufacturer field, and for the same reason.
     */
    char service[BLE_BEACON_MFR_SIZE];
};

/**
 * Parse one advertisement payload.
 *
 * `out` is fully assigned either way, so an advertisement that is truncated,
 * empty or nonsense yields BLE_BEACON_NONE with empty strings rather than
 * whatever the caller had in the struct before.
 *
 * Malformed input is the normal case, not the exception: these bytes come off
 * the air from devices nobody here wrote, and a length field that runs past the
 * end of the payload is something a scanner sees. Every structure is bounds
 * checked against `len` and a bad one ends the walk.
 */
void ble_beacon_parse(const uint8_t *adv, size_t len, struct ble_beacon_s *out);

/** "iBeacon", "Eddystone-UID", "Eddystone-URL" or "device". Never NULL. */
const char *ble_beacon_kind_name(enum ble_beacon_kind_e kind);

/**
 * A distance estimate in metres, or a negative value when there is none.
 *
 * The log-distance path loss model, with the exponent at 2.0 -- free space.
 * Indoors it is nearer 3, so this reads short through walls and furniture; it
 * is published because "about two metres or about twenty" is a useful thing to
 * know and a raw RSSI is not, and it should not be read more precisely than
 * that. Returns -1 when the reference power is unknown, rather than guessing a
 * typical one: a fabricated distance looks exactly like a measured one.
 */
float ble_beacon_distance(int8_t rssi, int8_t power_at_1m);

#endif // BLE_BEACON_HPP
