/**
 * @file ha_model.hpp
 *
 * What the Home Assistant connector knows about a dashboard and its entities,
 * with no socket in it: the host tests cover all of this.
 *
 * - A Lovelace view becomes a page of at most ITEM_COUNT_MAX tiles
 *   (ha_view_build()). Cards are flattened in order: an entity card is one
 *   tile, an entities or glance card is one tile per row, a stack or grid is
 *   its cards, a button that navigates is a link tile. On the root view, a
 *   card titled like the clock frame gives the clock items instead.
 * - An entity's state and attributes, as subscribe_entities sends them,
 *   become a tile type and a state in the openHAB words the item screens
 *   use ("ON", "40", "h,s,v", "PLAY"; ha_entity_classify(),
 *   ha_entity_panel_state()).
 * - A command from an item screen becomes a call_service message
 *   (ha_command_message()).
 *
 * The page is built in a plain struct (ha_page_s), which is what the
 * connector hands to the UI task as a page result. Sitemap::parse() in
 * connector_ha.cpp copies it into the page model.
 */
#ifndef HA_MODEL_HPP
#define HA_MODEL_HPP

#include <ArduinoJson.h>

#include "connector/connector_types.h"
#include "model/page_model.hpp"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* An entity id: "light.living_room". The event and subscription width. */
#define HA_ENTITY_LEN CONNECTOR_EVENT_NAME_LEN

/* A view path or index ("media", "#2") as a link tile carries it. */
#define HA_VIEW_LEN 32

/* How many views a dashboard may have for the root page's link tiles. More
 * are still reachable through a navigate button. */
#define HA_VIEW_MAX 8

/* A page reference: "<dashboard>" for the root view, "<dashboard>/<view>"
 * below it, with the prefix "ha:". */
#define HA_REF_LEN 72

enum ha_tile_kind_e
{
    HA_TILE_ENTITY, /* one entity                         */
    HA_TILE_LINK,   /* navigates to another view          */
    HA_TILE_BACK,   /* navigates back, drawn as the arrow */
};

/* One tile: first what the dashboard says about it, then what the entity's
 * state made of it. */
struct ha_tile_s
{
    uint8_t kind;              /* enum ha_tile_kind_e */
    char    entity[HA_ENTITY_LEN];
    char    name[STR_LABEL_LEN];    /* the card's name, "" for the entity's */
    char    icon[STR_ICON_NAME_LEN]; /* the card's icon ("mdi:..."), or "" */
    char    view[HA_VIEW_LEN];      /* link and back tiles: the target view */
    char    card[16];               /* the card type, a hint for the tile type */

    /* Filled by ha_tile_apply_state(). */
    uint8_t type;                   /* enum ItemType */
    bool    read_only;
    char    label[STR_LABEL_LEN];
    char    icon_name[STR_ICON_NAME_LEN]; /* a name of the panel's icon set */
    char    state[STR_STATE_TEXT_LEN];
    char    pattern[STR_PATTERN_LEN];
    float   min;
    float   max;
    float   step;
    uint8_t option_count;
    char    options[ITEM_SELECTION_COUNT_MAX][ITEM_SELECTION_COMMAND_LEN_MAX];
};

struct ha_view_s
{
    char title[24];
    char target[HA_VIEW_LEN]; /* path, or "#<index>" for a view without one */
    bool subview;
};

/* A page, as the connector hands it over to the UI task. */
struct ha_page_s
{
    char     title[STR_TITLE_LEN];
    char     ref[HA_REF_LEN];
    bool     root;
    uint8_t  tile_count;
    struct ha_tile_s tiles[ITEM_COUNT_MAX];
    uint8_t  clock_count;
    struct ha_tile_s clock[CLOCK_ITEM_COUNT];
};

/* ------------------------------------------------------------------ views */

/**
 * The filter for one view of lovelace/config: what ha_view_build() reads,
 * two levels of nested cards deep. Built once into `filter`.
 */
void ha_view_filter(JsonDocument &filter);

/** The filter for a view the page does not show: its title and path. */
void ha_view_summary_filter(JsonDocument &filter);

/**
 * The view a page reference names: its path, or "#<index>"; "" for the root
 * view. Writes the part after "<dashboard>/" of `ref` to `view`.
 */
void ha_ref_view(const char *ref, char *view, size_t view_size);

/** Whether view number `index` with path `path` is the one `want` names. */
bool ha_view_matches(const char *want, size_t index, const char *path);

/** A view's title, path and subview flag, for the root page's link tiles. */
void ha_view_summary(JsonVariantConst view, size_t index, struct ha_view_s *out);

/**
 * Fill `page` with the tiles of `view`.
 *
 * @param clock_frame  the card title that marks the clock items, root view
 *                     only; "" for none
 * @param views        every view of the dashboard, for the link tiles on the
 *                     root page and the back tile below it
 */
void ha_view_build(JsonVariantConst view, bool root, const char *clock_frame,
                   const struct ha_view_s *views, size_t view_count,
                   struct ha_page_s *page);

/**
 * Add link tiles to the root page for the views in `views` (but the first,
 * which is the root, and subviews), as long as there is room and no tile
 * links to the view already. ha_view_build() calls it with the views it
 * knows; the connector calls it again when it has seen all of them.
 */
void ha_page_add_view_links(struct ha_page_s *page, const struct ha_view_s *views,
                            size_t view_count);

/* --------------------------------------------------------------- entities */

/* What the connector keeps of one subscribed entity, to turn a change -- which
 * names only what changed -- into a whole panel state. */
struct ha_entity_s
{
    char    id[HA_ENTITY_LEN];
    uint8_t type;            /* enum ItemType, as the tile was classified */
    char    state[STR_STATE_TEXT_LEN]; /* Home Assistant's own state */
    float   brightness;      /* 0..255, or -1 for none */
    float   position;        /* cover position 0..100 (100 = open), or -1 */
    float   temperature;     /* climate set point, or NAN */
    float   value;           /* number / input_number: the state as a number */
    float   hue;             /* hs_color, or -1 */
    float   saturation;
    /* How a binary_sensor's on and off read: 0 "On"/"Off", 1 "Open"/"Closed",
     * 2 "Detected"/"Clear". From its device_class. */
    uint8_t binary_words;
};

/** Forget everything about `e` and give it id `id`, which must not point
 * into `e`. */
void ha_entity_reset(struct ha_entity_s *e, const char *id);

/**
 * Fold one entity's state object of a subscribe_entities message into `e`:
 * "s" and "a" of an addition, or the "+" of a change. `removed` is the
 * change's "-" (attributes that went away), null for an addition.
 */
void ha_entity_update(struct ha_entity_s *e, JsonVariantConst state, JsonVariantConst removed);

/**
 * Decide the tile type from the entity's domain, its attributes and the card
 * it sits in, and fill the rest of the tile from the state object: label,
 * icon, range, options, unit.
 *
 * @param state the entity's object from the "a" of a subscribe_entities
 *              message; null for an entity the server does not have
 */
void ha_tile_apply_state(struct ha_tile_s *tile, JsonVariantConst state, struct ha_entity_s *e);

/** The panel state of `e` for its tile type: "ON", "40", "21.5", "h,s,v"... */
void ha_entity_panel_state(const struct ha_entity_s *e, char *out, size_t out_size);

/**
 * A call_service message for `command` sent to entity `e`, with message id
 * `id`.
 *
 * @return the message length, or 0 for a command the entity has no service
 *         for (the caller drops it)
 */
size_t ha_command_message(const struct ha_entity_s *e, const char *command, uint32_t id,
                          char *out, size_t out_size);

#endif /* HA_MODEL_HPP */
