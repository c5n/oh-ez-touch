/**
 * @file icon_set.hpp
 *
 * The openHAB icon set, compiled into the firmware.
 *
 * Every widget icon used to be an HTTP GET: one per tile on every page load,
 * and another whenever an item's state changed the icon it resolves to. They
 * are the same few hundred small images on every panel in the world, so they
 * can simply be here instead -- which takes the icons off the critical path of
 * a page load entirely, and leaves the tiles drawn on a panel whose openHAB is
 * slow, busy, or momentarily unreachable.
 *
 * The artwork is not in this repository. tools/build_icon_set.py fetches it,
 * quantizes each icon to 16 colours, stores it as an LVGL indexed image and
 * writes
 *
 *     main/icons/icon_set_data.h
 *
 * which is gitignored: the classic icon set is EPL-2.0 and this project is
 * GPL-3.0, and the two cannot be distributed together. Until that script has
 * been run every lookup here finds nothing, and the firmware fetches icons over
 * HTTP exactly as it did before -- so the set is an optimisation the build
 * picks up when it is there, never something the panel needs.
 *
 * Indexed rather than a PNG of the same thing, because the panel is the
 * opposite of flash-poor: a decoded icon is a 4096-byte ARGB8888 in RAM plus
 * the output buffer and inflate state the decode wanted, per tile, on a heap
 * that also feeds BLE and MQTT. An LVGL indexed image needs none of that --
 * the renderer reads the palette and the packed pixels straight from the
 * record, so the whole cost of an icon is the 576 bytes below, and the days
 * of "the tiles draw but the icons are placeholders" end with it.
 *
 * openHAB stays authoritative for anything this set does not have. A custom
 * icon dropped into $OPENHAB_CONF/icons/classic/ exists only on that server,
 * and a miss here is a normal HTTP fetch -- see openhab_client.cpp, which is
 * where the precedence actually lives.
 */
#ifndef ICON_SET_HPP
#define ICON_SET_HPP

#include <stdbool.h>
#include <stddef.h>

/* Wide enough for "<icon name>-<state>" at the widths the sitemap carries them
 * -- STR_ICON_NAME_LEN and STR_STATE_TEXT_LEN, both 32 in openhab_connector.hpp.
 * Not taken from there by including it: this file knows about icons, not about
 * items, and openhab_connector.hpp drags in ArduinoJson. */
#define ICON_SET_NAME_MAX 72

/* The on-flash record for one icon, as the generator writes it and as the
 * image descriptor in openhab_ui.cpp is built from it: sixteen palette
 * entries in LVGL's lv_color32_t order -- {blue, green, red, alpha} -- then
 * the pixels packed two per byte, high nibble first, which is LVGL's I4
 * order. The record and the lv_image_dsc that points at it have the same
 * layout on purpose: the renderer's indexed-image path reads both without a
 * conversion step. */
#ifndef ICON_SET_PIXEL_SIZE
#define ICON_SET_PIXEL_SIZE 32
#endif

#define ICON_SET_PALETTE_BYTES (16 * 4)
#define ICON_SET_PIXEL_BYTES (ICON_SET_PIXEL_SIZE * ICON_SET_PIXEL_SIZE / 2)
#define ICON_SET_RECORD_SIZE (ICON_SET_PALETTE_BYTES + ICON_SET_PIXEL_BYTES)
#define ICON_SET_STRIDE (ICON_SET_PIXEL_SIZE / 2)

/**
 * Look up a built-in icon, the way openHAB resolves one.
 *
 * Three rules, in the order the server applies them:
 *
 *   1. the state-specific icon, "light-on" for "light" in state "ON";
 *   2. for a numeric state, the nearest variant at or below it, so a dimmer at
 *      40 gets "light-30" when 30 is the highest step defined;
 *   3. the plain icon.
 *
 * Rule 2 is not decoration. Once the built-in set answers first, its rules are
 * the ones the panel sees, and a set that only knew rules 1 and 3 would draw a
 * dimmer's icon at full brightness whatever the dimmer was doing -- which the
 * server had been getting right.
 *
 * @param name  icon name from the sitemap, e.g. "light"
 * @param state current item state, e.g. "ON" or "40"; may be NULL or empty
 * @param size  out: size of the returned PNG in bytes, 0 if none was found
 * @return pointer to PNG data in flash, or NULL if the set has no such icon
 */
const unsigned char *icon_set_get(const char *name, const char *state, size_t *size);

/**
 * The same lookup, keyed on the URL the firmware would otherwise have fetched.
 *
 * The client task carries URLs and not items -- that is the whole point of it
 * -- so this is the form the interception in openhab_client.cpp needs.
 *
 * @param url   the icon URL, as Item::iconUrl() builds it
 * @param size  out: size of the returned PNG in bytes, 0 if none was found
 * @return pointer to PNG data in flash, or NULL if the URL is not an icon URL
 *         or the set has no such icon
 */
const unsigned char *icon_set_get_by_url(const char *url, size_t *size);

/** How many icons are compiled in, and how many bytes of flash they take. Both
 *  are 0 when the set was never generated; openhab_client_setup() logs them, so
 *  that a build either says what it has or says it has none. */
size_t icon_set_count(void);
size_t icon_set_bytes(void);

/**
 * The whole set by index, for the one place that shows it rather than looks
 * something up in it: the settings screen's Icons page.
 *
 * Both take the icon's state variants as separate entries -- "light" and
 * "light-40" are two -- because that is what the set holds; what a widget
 * resolves to for a given state is icon_set_get()'s question, not this one's.
 *
 * @param index 0 .. icon_set_count() - 1, in the table's sorted order
 * @return the icon's name, or NULL when index is out of range or the set was
 *         never generated
 */
const char *icon_set_name(size_t index);

/** icon_set_name()'s companion: the indexed-image record -- palette plus
 *  packed pixels, laid out as an lv_image_dsc_t wants them -- for that index.
 *  See openhab_ui.cpp's widget_icon_decode_and_show() for the descriptor.
 *
 * @param size  out: the record's size in bytes, 0 if index is out of range
 * @return pointer to the record in flash, or NULL when index is out of range
 *         or the set was never generated
 */
const unsigned char *icon_set_entry(size_t index, size_t *size);

/**
 * Split "<website>/icon/<name>?state=<state>&format=png" into its two parts.
 *
 * Here rather than beside either caller because both the built-in set and the
 * simulator's offline fixture have to take a URL apart the same way, and a
 * second copy of this is a second place for the query string handling to drift.
 *
 * Returns false when the URL has no "/icon/" segment, or when either part is
 * too wide for the buffer given. A missing state is not a failure: openHAB
 * omits the query for an item that has none, and both lookups take an empty
 * state.
 */
bool icon_url_split(const char *url, char *name, size_t name_size,
                    char *state, size_t state_size);

/**
 * Build the lowercased "<name>-<state>" that a state-specific icon is called.
 *
 * Returns false for an empty state, or when the result does not fit -- never a
 * truncated name, which would look up a different icon.
 */
bool icon_name_qualify(const char *name, const char *state, char *out, size_t out_size);

#endif /* ICON_SET_HPP */
