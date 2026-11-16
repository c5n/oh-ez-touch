/**
 * @file connector_types.h
 *
 * The data types that cross the connector boundary: a finished request, a
 * subscription to pushed changes, and one pushed change. Separate from
 * connector.hpp so that a connector's own parsers -- and the host tests that
 * cover them -- can use them without the Config and page model headers.
 */
#ifndef CONNECTOR_TYPES_H
#define CONNECTOR_TYPES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ------------------------------------------------------------- requests */

/* Every reference a request carries, at one width. Matches STR_URL_LEN in
 * model/page_model.hpp, where the openHAB URLs are built. */
#define CONNECTOR_URL_LEN 256

/* A command body is an item state, which is the widest thing ever sent. */
#define CONNECTOR_BODY_LEN 32

/* The largest page body and the largest icon a connector reads, and the size
 * of the shared work buffer (mem/work_buf.h) main.cpp allocates for them.
 *
 * They are ceilings on one buffer the worker allocates once and keeps, not on
 * an allocation per request. A 12 KB block asked for and given back on every
 * page load is the one allocation on this panel large enough to need a
 * contiguous run of heap it may not get: the WiFi driver's dynamic transmit
 * buffers are held for as long as a frame is being retried, so on a weak link
 * the largest free block is at its smallest exactly when the page fetch that
 * needs it is retrying. The symptom was a panel whose tiles kept updating --
 * a state poll needs 32 bytes -- while every sub page it navigated to failed
 * with SITEMAP ACCESS FAILED and never came back. */
#define CONNECTOR_PAGE_BUFFER_SIZE 12288

/* For a request that belongs to no particular widget. */
#define CONNECTOR_SLOT_NONE 0xFFu

/* The generation that never goes stale. Commands carry it: a command the user
 * asked for is still worth delivering after they have navigated away from the
 * tile they tapped. */
#define CONNECTOR_GENERATION_ALWAYS 0u

enum connector_request_e
{
    CONNECTOR_REQ_PAGE,     /* a page of tiles, as the connector's JSON    */
    CONNECTOR_REQ_ICON,     /* a widget icon, as PNG                       */
    CONNECTOR_REQ_STATE,    /* one item's state, as text                   */
    CONNECTOR_REQ_COMMAND,  /* a command, nothing wanted back              */
    CONNECTOR_REQ_SITEMAPS, /* the list of page sets the server offers     */
    CONNECTOR_REQ_IMAGE,    /* an Image item's picture, decoded to RGB565  */
};

/**
 * A finished request.
 *
 * `payload` is on the heap and belongs to whoever took the result off the
 * queue -- including code that decides to drop it. Release it with
 * connector_result_release() on every path.
 *
 * `ok == true` with `payload == NULL` means "nothing to apply, and that is not
 * an error". A command reports success that way, and so does offline mode for
 * a state poll: the fixtures carry a fixed state per item, so leaving the item
 * alone is what makes a switch toggled locally look like it worked.
 */
struct connector_result_s
{
    enum connector_request_e type;
    uint32_t generation;
    uint8_t  slot;        /* the widget it was issued for, or _SLOT_NONE */
    bool     ok;
    int      status;      /* HTTP status where there was one, else 0 */
    char    *payload;     /* NUL-terminated at [payload_len], or NULL */
    size_t   payload_len;
    /* An item state rides in the result rather than on the heap, and
     * `payload` points at it.
     *
     * Six tiles polled every five seconds is some four thousand malloc/free
     * pairs an hour, each for at most 31 bytes, interleaved with the page and
     * icon bodies that do need the heap -- which is the churn that leaves a
     * long-running panel with plenty of free heap and no large block left in
     * it. The struct already travels through the queue by value, so carrying
     * the state inside it costs nothing but these 32 bytes.
     *
     * It is not a second ownership rule for callers: they still release every
     * result exactly once, and connector_result_release() is where the
     * distinction lives. */
    bool     payload_inline;
    /* A page result points at the worker's own receive buffer rather than at
     * an allocation of its own, and this flag says so -- the other half of the
     * rx_buf design the header comment above describes.
     *
     * The point is what it removes: the per-page malloc of up to twelve
     * kilobytes, which is the one allocation on this panel big enough to be
     * refused by a fragmented heap. Panels that ran all day with BLE and MQTT
     * on ended up with kilobytes free and no four-kilobyte block left, and
     * from then on every page turn failed at that malloc -- SITEMAP ACCESS
     * FAILED, worst on the *main* page, because it is the biggest. The buffer
     * is already held permanently for exactly this body, so the copy out of it
     * bought nothing.
     *
     * What makes one buffer enough is the generation: at most one page result
     * is ever live, because submitting the next page fetch stales the last
     * one, and the UI discards a stale result before it looks at the payload.
     * The worker still waits for the release before it reuses the buffer --
     * the generation rules out a *second* page, not an icon read landing in
     * the same memory while the UI is still parsing. A sitemap list result
     * rides the buffer the same way, and the same wait keeps it apart from a
     * page result. */
    bool     payload_static;
    /* What of rx_buf an icon result does not fill: the consumer may use it as
     * work memory until it releases the result. NULL for every other result,
     * and for a built-in icon, which is not in rx_buf. */
    void    *scratch;
    size_t   scratch_len;
    char     body[CONNECTOR_BODY_LEN];
    /* An image's size; its pixels are the payload, payload_len bytes. */
    uint16_t width;
    uint16_t height;
};

/* --------------------------------------------------------------- events */

/* An item name. A name that does not fit cannot be matched to a tile, so a
 * connector refuses it rather than truncating it into another name. */
#define CONNECTOR_EVENT_NAME_LEN 64

/* The width of a state, and of the two halves of a widget's label. */
#define CONNECTOR_EVENT_TEXT_LEN 32

/* The tiles and the clock items, with room to spare. */
#define CONNECTOR_EVENTS_ITEM_MAX 12

/* The width of an item state; checked against STR_STATE_TEXT_LEN in
 * openhab_ui.cpp. */
#define CONNECTOR_EVENTS_VALUE_LEN 32

/* "http://host:port", which is what openhab_ui.cpp calls the website. */
#define CONNECTOR_EVENTS_WEBSITE_LEN 128

/* A page set name, at the width Config stores it, and a page id. */
#define CONNECTOR_EVENTS_SITEMAP_LEN 32
#define CONNECTOR_EVENTS_PAGE_LEN 64

/* What to listen for. Built by the UI, copied whole by
 * connector_events_subscribe(). */
struct connector_subscription_s
{
    char   website[CONNECTOR_EVENTS_WEBSITE_LEN];
    /* The page on screen. Either one "" means: listen for the items below
     * only. */
    char   sitemap[CONNECTOR_EVENTS_SITEMAP_LEN];
    char   page[CONNECTOR_EVENTS_PAGE_LEN];
    /* The items on screen. */
    size_t count;
    struct
    {
        char name[CONNECTOR_EVENT_NAME_LEN];
        /* The item is a group, whose own state changes a connector may
         * report under a different topic. */
        bool group;
    } item[CONNECTOR_EVENTS_ITEM_MAX];
};

/* One widget as the page would show it now. Widgets and pages are known by
 * a hash of their id -- see connector_id_hash() -- so an Item can carry one in
 * four bytes. */
struct connector_widget_event_s
{
    uint32_t widget;
    uint32_t page;
    /* The item's raw state; "" for a widget with no item, like a Frame. */
    char     state[CONNECTOR_EVENT_TEXT_LEN];
    /* The label before its "[...]", and what is inside it ("" for none). */
    char     caption[CONNECTOR_EVENT_TEXT_LEN];
    char     display[CONNECTOR_EVENT_TEXT_LEN];
    uint32_t label_color;  /* 0xRRGGBB or PAGE_COLOR_NONE */
    uint32_t value_color;
    uint32_t icon_color;
    bool     has_state;
    bool     visible;
};

enum connector_event_kind_e
{
    CONNECTOR_EVENT_KIND_ITEM,   /* `item`: an item's state change       */
    CONNECTOR_EVENT_KIND_WIDGET, /* `widget`: a widget of the page changed */
};

struct connector_event_s
{
    enum connector_event_kind_e kind;

    union
    {
        struct
        {
            char name[CONNECTOR_EVENT_NAME_LEN];
            char value[CONNECTOR_EVENTS_VALUE_LEN];
        } item;

        struct connector_widget_event_s widget;
    };
};

/* ----------------------------------------------------------------- ids */

/* FNV-1a over a widget or page id -- for openHAB "1_0", "1_50", or on a
 * group's generated page the member's item name. 0 is kept for "none", so
 * an id that hashes to it is moved to 1 -- one more collision, out of four
 * billion. */
static inline uint32_t connector_id_hash(const char *id)
{
    uint32_t hash = 2166136261u;

    if (id == NULL || id[0] == '\0')
        return 0;

    for (; *id != '\0'; id++)
    {
        hash ^= (uint8_t)*id;
        hash *= 16777619u;
    }

    return (hash == 0) ? 1 : hash;
}

#endif /* CONNECTOR_TYPES_H */
