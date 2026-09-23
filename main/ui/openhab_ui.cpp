#include "sdkconfig.h"

#include "openhab_ui.hpp"
#include "icons/icon_set.hpp"
#include "openhab/openhab_client.hpp"
#include "openhab/openhab_connector.hpp"
#include "openhab/openhab_sitemaps.hpp"
#include "ui_messagebox.hpp"
#include "ui_beep.hpp"
#include "ui_settings.hpp"
#include "frames/ui_frame.hpp"
#include "items/item_screen.hpp"
#include "ui_screen.hpp"
#include "ui_motion.hpp"
#include "ui_style.hpp"
#include "ui_widgets.hpp"

#include "lodepng/lodepng.h"
#include "time.h"
#include "version.h"
#include "debug.h"
#include "port/port_net.h"
#include "port/port_ntp.h"
#include "port/port_sys.h"

#include "esp_log.h"

#include <lvgl.h>
/* lv_image_cache_drop() is not reachable through lvgl.h. free_icon() needs it:
 * LVGL v9 caches decoded images by source pointer, and this UI frees the pixel
 * data behind a descriptor it then reuses. */
#include <misc/cache/instance/lv_image_cache.h>

#ifndef WIDGET_COUNT_MAX
#define WIDGET_COUNT_MAX 6
#endif

#ifndef ITEM_UPDATE_INTERVAL
#define ITEM_UPDATE_INTERVAL 5000
#endif

#ifndef GET_SITEMAP_RETRY_INTERVAL
#define GET_SITEMAP_RETRY_INTERVAL 5000
#endif

/* How long a submitted page fetch is waited for before it is written off and
 * asked for again.
 *
 * Six times the client's own five second timeout, and the arithmetic is the
 * point. A single GET can take twice that timeout, because openhab_http_get()
 * gives a request that failed on a reused connection one more go on a fresh
 * one; and a page submitted while another request is already in flight waits
 * for that one first, because the worker is one task and a socket read cannot
 * be cancelled. So twenty seconds is reachable on a link that is merely slow,
 * and the fifteen this used to be was less than that.
 *
 * Firing early is not a harmless retry, which is why the margin is generous.
 * page_request() leads to a resubmit, the resubmit bumps the generation, and
 * the answer to the *first* fetch -- which may be a perfectly good page, and
 * on a weak link usually is -- is then dropped as stale. The panel spent
 * twenty seconds getting the page it wanted and threw it away, put up SITEMAP
 * ACCESS FAILED, and started again.
 *
 * The client task answers every request it does not drop, so this should still
 * never fire. It exists because the one way the asynchronous shape can fail
 * that the synchronous one could not is by waiting forever -- and a page that
 * never loads and never retries is a blank screen with nothing to show for
 * itself: no failure counted, and no box to raise the fault or offer a
 * restart. */
#ifndef GET_SITEMAP_ANSWER_TIMEOUT
#define GET_SITEMAP_ANSWER_TIMEOUT 30000
#endif

#ifndef NTP_TIME_UPDATE_INTERVAL
#define NTP_TIME_UPDATE_INTERVAL (60 * 60 * 1000)
#endif

/* How long the poll counters are gathered for before they start again. It
 * used to be the watchdog's window -- the period over which more failures than
 * successes rebooted the panel -- and is now only what the statistics line
 * under CONFIG_OHEZ_DEBUG_OPENHAB_UI reports over. */
#ifndef STATISTICS_WINDOW_S
#define STATISTICS_WINDOW_S 180
#endif

#define HEADER_SIGNAL_UPDATE_INTERVAL 5000

/* The breathing room inside a tile. Shared by the caption and the value so
 * that the two cannot drift apart. */
#define TILE_PAD 4

/* How often the automatic night schedule is compared against the clock. The
 * comparison is cheap and openhab_ui_request_theme() drops a request that
 * changes nothing, so this only has to be fine enough that the switch looks
 * prompt at the boundary. */
#define NIGHT_CHECK_INTERVAL (30 * 1000)


// Holds "http://<hostname>:<port>/rest/sitemaps/<sitemap>/<sitemap>?type=json".
// With the 32 byte hostname and sitemap fields of Config that needs 133 bytes,
// which did not fit in the previous 128 and silently truncated the URL.
#define STR_PAGE_LEN        256
#define STR_WEBSITE_LEN     128

extern void lodepng_free(void* ptr);

Messagebox openhab_ui_messagebox;

static Config *current_config;

static lv_obj_t *content = nullptr;

/* What the frame is told about the link. Kept because the RSSI poll and the
 * WLAN state change arrive separately and each has to redraw both halves. */
static bool wifi_online;

struct widget_context_s
{
    uint64_t update_timestamp = 0;
    bool refresh_request = false;
    /* An icon request for this tile is in flight. Stops a second one going out
     * for a state that changes faster than openHAB answers, and -- unlike
     * refresh_request and update_timestamp -- is deliberately *not* cleared by
     * widget_destroy(): a theme change destroys and recreates every tile, and
     * clearing it there would duplicate a request that is still outstanding. */
    bool icon_pending = false;
    /* And the same for a state poll. This one also decides the poll interval's
     * meaning: update_timestamp now marks when a poll was *submitted*, so
     * without this a server slower than ITEM_UPDATE_INTERVAL would put another
     * request in the queue every five seconds for a tile that already has one
     * outstanding. */
    bool state_pending = false;
    lv_obj_t *container = NULL;
    lv_obj_t *label = NULL;
    lv_obj_t *img_obj = NULL;
    lv_image_dsc_t img_dsc;
    lv_obj_t *state_widget = NULL;
    /* The open control's widgets used to hang off here as five more pointers.
     * They live in the item screen now -- see main/ui/items/. */
    Item *item = NULL;
};

struct statistics_s
{
    uint32_t update_success_cnt = 0;
    uint32_t update_fail_cnt = 0;
    uint32_t sitemap_success_cnt = 0;
    uint32_t sitemap_fail_cnt = 0;
};

Sitemap sitemap;

struct widget_context_s widget_context[WIDGET_COUNT_MAX];

struct statistics_s statistics;

void update_state_widget(struct widget_context_s *ctx);
static void page_request(uint64_t delay_ms);
static void widget_icon_request(size_t slot);

/* Send an item's local state to openHAB.
 *
 * Every caller is an LVGL event handler, which is to say every caller runs
 * inside lv_timer_handler(). Item::publish() used to do this with a blocking
 * POST, so a tap held up the very redraw that was supposed to acknowledge it.
 *
 * Fire and forget, as it always effectively was: publish()'s return value was
 * ignored at all five call sites, and the tile's own state has already been
 * set locally -- the poll that follows is what reconciles it with the server.
 */
static void item_publish(struct widget_context_s *ctx)
{
    if (openhab_client_command(ctx->item->getLink(), ctx->item->getStateText()) == true)
        return;

    /* The queue would not take it, which on a slow link is reachable: six
     * tiles with an icon and a state outstanding is exactly the queue's depth.
     * The tile has already been flipped locally, so the tap looks delivered
     * and the next poll quietly puts it back -- a light that did not come on
     * and nothing anywhere saying why.
     *
     * Counted and said. There is nothing better to do with it from here: a tap
     * is not worth queueing behind a five second socket read, and re-flipping
     * the tile under the user's finger would be its own surprise. */
    printf("openhab_ui: command queue full; \"%s\" not sent to %s\r\n",
           ctx->item->getStateText(), ctx->item->getLink());

    statistics.update_fail_cnt++;
}

/* show() pairs widget_context[i] with sitemap.getItem(i), so there must not be
 * more widgets than the sitemap holds items. */
static_assert(WIDGET_COUNT_MAX <= ITEM_COUNT_MAX, "WIDGET_COUNT_MAX exceeds ITEM_COUNT_MAX");

/* Where the tile page is in the cycle of asking for a sitemap and getting one.
 *
 * PAGE_READY is what "the tile page currently reflects a sitemap" used to be
 * told by sitemap_ok, and it is still what a theme change tests before
 * rebuilding tiles there is no sitemap behind. What is new is the state
 * between wanting a page and having one: the fetch no longer happens inside a
 * single call, so there has to be somewhere to be while it is outstanding.
 *
 *   PAGE_IDLE     nothing wanted -- before the first openhab_ui_connect()
 *   PAGE_REQUEST  wanted, and due to be submitted at refresh_retry_timeout
 *   PAGE_WAITING  submitted; a result is expected
 *   PAGE_READY    parsed, and the tiles on screen are its
 */
enum page_state_e
{
    PAGE_IDLE,
    PAGE_REQUEST,
    PAGE_WAITING,
    PAGE_READY,
};

static enum page_state_e page_state;
/* Stamped on every icon and state request, so that the answers can be told
 * apart from the answers to a page that has since been navigated away from.
 * Handed out by openhab_client_request_page(). */
static uint32_t page_generation;
/* When a submitted page fetch is written off. The worker answers every request
 * it does not drop, so reaching this means something is wrong that a retry has
 * a better chance with than waiting does. */
static uint64_t page_request_deadline;
/* When the next submit is due. A file static rather than a local of the loop,
 * because page_request() is called from a tile's event handler as well. */
static uint64_t page_retry_timeout;
/* A requested variant, applied from openhab_ui_loop(). */
static bool theme_pending;
static bool connect_pending;
static char connect_pending_host[32];
static uint16_t connect_pending_port;
static char connect_pending_sitemap[32];

/* What openhab_ui_connect() was last called with, so a request naming the same
 * server again can be recognised as the no-op it is. */
static char connect_current_host[32];
static uint16_t connect_current_port;
static char connect_current_sitemap[32];
static enum ui_theme_family_e theme_pending_family;
static bool theme_pending_night;
static char current_page[STR_PAGE_LEN];
static char last_page[STR_PAGE_LEN];
static char current_website[STR_WEBSITE_LEN];

/* ------------------------------------------------------------ what is shown */

/* Read-only views of the state above, for the simulator's control interface.
 * They are here rather than in testif/ because every one of them reads a file
 * static that has no business leaving this file any other way. */

const char *openhab_ui_page_title(void)
{
    return sitemap.getPageName();
}

const char *openhab_ui_page_state_name(void)
{
    switch (page_state)
    {
    case PAGE_IDLE:    return "idle";
    case PAGE_REQUEST: return "request";
    case PAGE_WAITING: return "waiting";
    case PAGE_READY:   return "ready";
    }

    return "unknown";
}

uint32_t openhab_ui_page_generation(void)
{
    return page_generation;
}

size_t openhab_ui_tile_count(void)
{
    /* What is built, not what the sitemap holds: a page with more items than
     * WIDGET_COUNT_MAX shows the first few, and a script should be told about
     * the tiles it can actually touch. */
    size_t count = 0;

    while (count < WIDGET_COUNT_MAX && widget_context[count].container != NULL)
        count++;

    return count;
}

bool openhab_ui_tile_info(size_t index, struct openhab_ui_tile_s *out)
{
    if (index >= openhab_ui_tile_count())
        return false;

    struct widget_context_s *ctx = &widget_context[index];

    if (ctx->item == NULL)
        return false;

    out->label = ctx->item->getLabel();
    out->state = ctx->item->getStateText();
    out->type  = ctx->item->getType();

    /* Coordinates as laid out, not as the grid solver computed them: a frame
     * is free to place a tile where it likes, and what a script needs is where
     * the thing actually is. */
    lv_area_t area;

    lv_obj_get_coords(ctx->container, &area);

    out->x = area.x1;
    out->y = area.y1;
    out->w = lv_area_get_width(&area);
    out->h = lv_area_get_height(&area);

    return true;
}

uint8_t openhab_ui_signal_quality(int8_t rssi)
{
    if (rssi < -100)
        return 0;
    else if (rssi > -50)
        return 100;
    else
        return 2 * (rssi + 100);
}

static void event_handler(lv_event_t *e)
{
#if CONFIG_OHEZ_DEBUG_OPENHAB_UI
    printf("event_handler: LV_EVENT_CLICKED\r\n");
#endif
    struct widget_context_s *ctx = (struct widget_context_s *)lv_event_get_user_data(e);

    if (ctx == nullptr || ctx->item == nullptr)
        return;

    switch (ctx->item->getType())
    {
    case ItemType::type_string:
    case ItemType::type_number:
        /* Nothing to open: the state is already on the tile. */
        break;

    case ItemType::type_parent_link:
    case ItemType::type_link:
    case ItemType::type_group:
#if CONFIG_OHEZ_DEBUG_OPENHAB_UI
        printf("LinkedPage Link: %s\r\n", ctx->item->getPageLink());
#endif
        strlcpy(last_page, current_page, sizeof(last_page));
        strlcpy(current_page, ctx->item->getPageLink(), sizeof(current_page));
        page_request(0);

        if (ctx->item->getType() == ItemType::type_parent_link)
            BEEPER_EVENT_LINK_BACK();
        else
            BEEPER_EVENT_LINK();
        break;

    case ItemType::type_switch:
#if CONFIG_OHEZ_DEBUG_OPENHAB_UI
        printf("Link: %s ... Posting update\r\n", ctx->item->getLink());
#endif
        /* The one control that needs no screen of its own: a switch has two
         * states and the tile is already big enough to be the button. */
        if (strncmp(ctx->item->getStateText(), "OFF", 3) == 0)
        {
            ctx->item->setStateText("ON");
            item_publish(ctx);
            /* Which way it went, not merely that it went: a switch is the one
             * control whose whole state is audible in one sound, and telling
             * the two apart is what lets somebody flip a light from across the
             * room without looking. */
            BEEPER_EVENT_TOGGLE_ON();
        }
        else
        {
            ctx->item->setStateText("OFF");
            item_publish(ctx);
            BEEPER_EVENT_TOGGLE_OFF();
        }

        ctx->refresh_request = true;
        break;

    default:
        /* Everything else is a screen. item_screen_open() looks the type up in
         * its own registry and does nothing for one it has no entry for, which
         * is what the switch's default arm used to say with a beep. */
        if (item_screen_find(ctx->item->getType()) != NULL)
            item_screen_open(ctx->item, (uint8_t)(ctx - widget_context));
        else
            BEEPER_EVENT_ERROR();
        break;
    }
}

/* Show the mapping label that matches the item's current command. openHAB
 * sends these for Switch and Selection items ("ON=Text1, OFF=Text2"); without
 * a matching mapping the raw state is the best that can be shown. */
static void set_label_from_mapping(lv_obj_t *label, Item *item)
{
    for (size_t index = 0; index < item->getSelectionCount(); index++)
    {
        if (strcmp(item->getSelectionCommand(index), item->getStateText()) == 0)
        {
            ui_reading_set_text(label, item->getSelectionLabel(index));
            return;
        }
    }

    ui_reading_set_text(label, item->getStateText());
}

void update_state_widget(struct widget_context_s *ctx)
{
    if (ctx->state_widget == NULL)
        return;

    switch (ctx->item->getType())
    {
    case ItemType::type_string:
        /* sizeof() on the returned pointer used to be tested here, which is
         * always non-zero, so the transformed text was used even when openHAB
         * had not sent one. */
        if (strlen(ctx->item->getTransformedStateText()) > 0)
            ui_reading_set_text(ctx->state_widget, ctx->item->getTransformedStateText());
        else
            ui_reading_set_text(ctx->state_widget, ctx->item->getStateText());
        break;

    case ItemType::type_group:
        // A group aggregates its members, which may be a number or a text.
        if (ctx->item->stateIsNumber() == true)
            ui_reading_set_pattern(ctx->state_widget, ctx->item->getNumberPattern(),
                                   ctx->item->getStateNumber());
        else
            ui_reading_set_text(ctx->state_widget, ctx->item->getStateText());
        break;

    case ItemType::type_number:
    case ItemType::type_setpoint:
    case ItemType::type_slider:
    case ItemType::type_rollershutter:
        ui_reading_set_pattern(ctx->state_widget, ctx->item->getNumberPattern(),
                               ctx->item->getStateNumber());
        break;

    case ItemType::type_switch:
    case ItemType::type_selection:
        set_label_from_mapping(ctx->state_widget, ctx->item);
        break;

    case ItemType::type_player:
        ui_reading_set_text(ctx->state_widget, ctx->item->getStateText());
        break;

    case ItemType::type_colorpicker:
    {
        /* A state that is not "h,s,v" leaves the swatch black, which is what
         * getStateHsv() sets its outputs to when it refuses. */
        uint16_t h;
        uint8_t  s;
        uint8_t  v;

        ctx->item->getStateHsv(&h, &s, &v);
        lv_obj_set_style_bg_color(ctx->state_widget, lv_color_hsv_to_rgb(h, s, v), 0);
        return;
    }

    default:
        ESP_LOGW("openhab_ui", "update_state_widget: unknown or unsupported item type id: %d",
                 (int)ctx->item->getType());
        return;
    }

    /* Where a family draws a gauge, it needs the value's place in its range
     * rather than the value. Only JARVIS has one, so this is a direct call
     * rather than another entry in the frame interface -- the linker drops it
     * for a build that never selects that family. */
    if (ui_style_family() == UI_THEME_JARVIS && ctx->item->getMaxVal() > ctx->item->getMinVal())
    {
        float span = ctx->item->getMaxVal() - ctx->item->getMinVal();
        float here = ctx->item->getStateNumber() - ctx->item->getMinVal();
        int   pct  = (int)((here * 100.0f) / span);

        ui_frame_jarvis_set_gauge((uint8_t)(ctx - widget_context),
                                  (uint8_t)((pct < 0) ? 0 : (pct > 100) ? 100 : pct));
    }
}

/**
 * If the display is not in 32 bit format (ARGB888) then covert the image to the current color depth
 * @param img the ARGB888 image
 * @param px_cnt number of pixels in `img`
 */
/* lodepng decodes to R,G,B,A byte order; LVGL v9's LV_COLOR_FORMAT_ARGB8888 is
 * B,G,R,A in memory. Under v7 this function also had to pack the pixels down to
 * the display's colour depth, which is why it was named this way -- v9 keeps
 * images in a real colour format and converts them when drawing, so all that is
 * left is the red/blue swap. */
static void convert_color_depth(uint8_t *img, uint32_t px_cnt)
{
    for (uint32_t i = 0; i < px_cnt; i++)
    {
        uint8_t red = img[i * 4 + 0];

        img[i * 4 + 0] = img[i * 4 + 2];
        img[i * 4 + 2] = red;
    }
}

/* Release a tile's decoded pixels.
 *
 * The sweep at the end is not paranoia: two tiles showing the same icon share
 * one decoded block, so a descriptor that still points at it has to be blanked
 * or the next redraw walks freed memory.
 *
 * Icons are fetched asynchronously now, so this can run while a request for
 * the page being torn down is still in flight. What makes that safe is not
 * anything here -- it is results_apply_one() dropping a result whose
 * generation has been superseded before it touches widget_context[] at all. A
 * page rebuild is always preceded by a new generation.
 */
void free_icon(lv_image_dsc_t *pdsc)
{
    if (pdsc->data == NULL)
        return;
    const uint8_t *pref = pdsc->data;

    /* LVGL v9 caches decoded images by source pointer, so the cache has to let
     * go before the pixels do -- otherwise a redraw walks freed memory. This
     * hazard does not exist in v7, which had no such cache. */
    lv_image_cache_drop(pdsc);

    free((void *)pdsc->data);

    // clear all cache references
    for (size_t ref = 0; ref < WIDGET_COUNT_MAX; ref++)
    {
        if (widget_context[ref].img_dsc.data == pref)
        {
            widget_context[ref].img_dsc.data = NULL;
            widget_context[ref].img_dsc.data_size = 0;
        }
    }
}

/* The placeholder a tile shows while it has no icon.
 *
 * This used to mean an icon that had failed. It is now also every tile for the
 * moment between the page appearing and its icons arriving, so it has to be
 * something a bitmap can cleanly replace -- which is why the two halves are
 * paired functions rather than a branch inside widget_create().
 *
 * ui_style_label_large and the local text_opa are what make a symbol font
 * render at icon size. text_opa is set locally, not shared: it is not
 * inheritable and the two fallbacks want different values. A theme change
 * recreates the tiles, so these follow it that way rather than by a refresh.
 */
static void widget_icon_set_fallback(struct widget_context_s *wctx)
{
    bool back = (wctx->item->getType() == ItemType::type_parent_link);

    lv_obj_add_style(wctx->img_obj, &ui_style_label_large, LV_PART_MAIN);
    lv_image_set_src(wctx->img_obj, back ? LV_SYMBOL_NEW_LINE : LV_SYMBOL_EYE_OPEN);
    lv_obj_set_style_text_opa(wctx->img_obj,
                              back ? ui_style_theme()->symbol_opa
                                   : ui_style_theme()->symbol_dim_opa, 0);
}

/* Put the decoded pixels on the tile, and undo what the placeholder left.
 *
 * Taking the style and the local opacity back off is not housekeeping. They
 * are inert for an image source only for as long as nothing consults them, and
 * every tile now passes through the placeholder on its way to an icon rather
 * than only the ones whose icon failed -- so "should be inert" would be
 * load-bearing on every page instead of never.
 */
static void widget_icon_set_bitmap(struct widget_context_s *wctx)
{
    lv_obj_remove_style(wctx->img_obj, &ui_style_label_large, LV_PART_MAIN);
    lv_obj_remove_local_style_prop(wctx->img_obj, LV_STYLE_TEXT_OPA, 0);

    /* Clearing the source first is not redundant: LVGL compares the src
     * pointer and would skip the update, since the descriptor is reused with
     * fresh pixels behind it. */
    lv_image_set_src(wctx->img_obj, NULL);
    lv_image_set_src(wctx->img_obj, &wctx->img_dsc);
}

/* Ask the client task for this tile's icon.
 *
 * Submitting, not fetching: the answer arrives at results_apply_one() some
 * frames later and lands in widget_icon_decode_and_show(). This is what used
 * to be load_icon()'s blocking half, and calling it six times in a row is what
 * used to hold the screen for the length of six HTTP requests.
 */
static void widget_icon_request(size_t slot)
{
    struct widget_context_s *wctx = &widget_context[slot];
    char url[STR_URL_LEN];

    if (wctx->item == NULL)
        return;

    /* The back tile draws a symbol and never an icon: a sitemap's parent link
     * carries no icon name of its own. page_rebuild() made the same exemption
     * around load_icon(). */
    if (wctx->item->getType() == ItemType::type_parent_link)
        return;

    if (wctx->icon_pending == true)
        return;

    if (wctx->item->iconUrl(current_website, url, sizeof(url)) == false)
        return;

    if (openhab_client_request_icon(url, (uint8_t)slot, page_generation) == true)
        wctx->icon_pending = true;
}

/* Halve an ARGB8888 image in both directions. The tiles show every icon at
 * the built-in set's 32 px, but a custom $OPENHAB_CONF icon can be 64 or 128,
 * and next to the built-ins it has to be 32 too. Averaged in premultiplied
 * space: a straight RGBA average of line art on transparency smears the
 * transparent pixels' colour into the strokes, weighing each pixel's colour
 * by its own alpha does not. Returns the halved image, or NULL -- the caller
 * keeps the full-size one then. */
static unsigned char *icon_downscale_2x(const unsigned char *src, unsigned width,
                                        unsigned height)
{
    /* width * height is exactly (width/2) * (height/2) * 4. */
    unsigned char *dst = (unsigned char *)malloc(width * height);

    if (dst == NULL)
        return NULL;

    for (unsigned y = 0; y < height / 2; y++)
    {
        for (unsigned x = 0; x < width / 2; x++)
        {
            const unsigned char *p = src + (2 * y * width + 2 * x) * 4;
            uint32_t c0 = 0, c1 = 0, c2 = 0, a = 0;

            for (unsigned k = 0; k < 4; k++)
            {
                const unsigned char *q = p + (k & 1) * 4 + (k >> 1) * width * 4;
                uint32_t alpha = q[3];

                a  += alpha;
                c0 += q[0] * alpha;
                c1 += q[1] * alpha;
                c2 += q[2] * alpha;
            }

            unsigned char *d = dst + (y * (width / 2) + x) * 4;

            if (a == 0)
            {
                d[0] = d[1] = d[2] = d[3] = 0;
            }
            else
            {
                d[0] = (unsigned char)(c0 / a);
                d[1] = (unsigned char)(c1 / a);
                d[2] = (unsigned char)(c2 / a);
                d[3] = (unsigned char)(a / 4);
            }
        }
    }

    return dst;
}

/* Put an arrived icon on its tile. load_icon()'s other half.
 *
 * Two kinds arrive here, and they could not be treated more differently.
 *
 * An icon from the firmware's own set is an LVGL indexed image straight from
 * the generator: sixteen palette entries and the packed pixels, laid out
 * exactly as an lv_image_dsc wants them. It is copied, not decoded -- that is
 * the whole point of the set's format: no lodepng, no 4096-byte ARGB8888, no
 * output buffer or inflate state to ask of a heap that has been up for days.
 *
 * Anything else is a PNG that came from the server, and that one does get the
 * decode -- and, if the server serves an icon set larger than the firmware's
 * own, a downscale back to the set's size afterwards, so a tile never draws
 * a big custom icon next to small built-in ones. The decode stays on this
 * task deliberately. It is single-digit milliseconds against the hundreds the
 * fetch can take, it keeps what is in flight down to the size of the PNG
 * rather than the size of the pixels, and it keeps the malloc and the free of
 * a block LVGL will hold a pointer to on one task.
 */
static void widget_icon_decode_and_show(struct widget_context_s *wctx,
                                        struct openhab_result_s *res)
{
    const void *png = res->payload;
    size_t png_len = res->payload_len;
    unsigned char *pixels;
    uint32_t width, height, stride, data_size;
    lv_color_format_t cf;

#if CONFIG_OHEZ_DEBUG_OPENHAB_UI
    printf("widget_icon: %s ", wctx->item != NULL ? wctx->item->getIconName() : "?");
#endif

    if (png_len == ICON_SET_RECORD_SIZE && ((const unsigned char *)png)[0] != 0x89)
    {
        /* The built-in set's record. It is not a PNG -- every PNG begins with
         * 0x89 -- and it has exactly the record's shape, which no valid
         * server icon has. Copied out of the result, which the caller then
         * releases as usual -- adoption would be the one copy fewer, at the
         * price of a second ownership rule on the result. */
        pixels = (unsigned char *)malloc(ICON_SET_RECORD_SIZE);

        if (pixels == NULL)
        {
            printf("no %u bytes for a built-in icon\n", (unsigned)ICON_SET_RECORD_SIZE);
            return;
        }

        memcpy(pixels, png, ICON_SET_RECORD_SIZE);

        cf = LV_COLOR_FORMAT_I4;
        width = height = ICON_SET_PIXEL_SIZE;
        stride = ICON_SET_STRIDE;
        data_size = ICON_SET_RECORD_SIZE;
    }
    else
    {
        /* unsigned, not uint32_t: lodepng_decode32() takes unsigned *, and
         * the two are the same type on the host and different ones on the
         * device. */
        unsigned png_width, png_height;

        // Decode the loaded image in ARGB8888
        unsigned int error = lodepng_decode32(&pixels, &png_width, &png_height,
                                              (const unsigned char *)png, png_len);

        /* A memory failure with an icon already on the tile deserves a second
         * attempt without it: the old pixels are part of the very heap the
         * decode is asking for, and holding them to the bitter end is what
         * made a refresh never fit next to them. The fallback takes the tile
         * if the retry fails too -- an honest placeholder over a stale icon.
         * Only error 83 earns the retry; a corrupt PNG decodes no better
         * with more heap. */
        if (error == 83 && wctx->img_dsc.data != NULL)
        {
            lv_image_cache_drop(&wctx->img_dsc);
            free((void *)wctx->img_dsc.data);
            wctx->img_dsc.data = NULL;

            error = lodepng_decode32(&pixels, &png_width, &png_height,
                                     (const unsigned char *)png, png_len);
        }

        if (error)
        {
            printf("widget_icon: %s PNG decode error %u: %s (len %u, heap %u free, %u largest)\n",
                   wctx->item != NULL ? wctx->item->getIconName() : "?",
                   error, lodepng_error_text(error), (unsigned)png_len,
                   (unsigned)port_free_heap(), (unsigned)port_largest_free_block());
            return;
        }

        convert_color_depth(pixels, png_width * png_height);

        /* Server icons larger than the built-in set go down to its size: a
         * 64 px custom icon next to the 32 px built-ins is exactly the pixel
         * soup scaling the built-ins up used to be, only mirrored. Halved in
         * both directions until it fits -- never below the set's size. The
         * halved copy is a fresh small block and the decode buffer goes back
         * whole, so the next decode's working set still finds the block this
         * one just used. */
        while (png_width % 2 == 0 && png_height % 2 == 0
               && png_width / 2 >= ICON_SET_PIXEL_SIZE
               && png_height / 2 >= ICON_SET_PIXEL_SIZE)
        {
            unsigned char *halved = icon_downscale_2x(pixels, png_width, png_height);

            if (halved == NULL)
                break;

            free(pixels);
            pixels = halved;
            png_width /= 2;
            png_height /= 2;
        }

        cf = LV_COLOR_FORMAT_ARGB8888;
        width = png_width;
        height = png_height;
        stride = png_width * 4;
        data_size = png_width * png_height * 4;
    }

    /* The old pixels go only now that there are new ones to put in their
     * place. load_icon() freed them up front and so left the tile blank when
     * the fetch or the decode then failed; a refresh that does not arrive now
     * leaves the icon that is already there. */
    if (wctx->img_dsc.data != NULL)
    {
        lv_image_cache_drop(&wctx->img_dsc);
        free((void *)wctx->img_dsc.data);
    }

    // Initialize an image descriptor for LVGL with the image
    wctx->img_dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    wctx->img_dsc.header.cf = cf;
    wctx->img_dsc.header.flags = 0;
    wctx->img_dsc.header.w = (uint16_t)width;
    wctx->img_dsc.header.h = (uint16_t)height;
    wctx->img_dsc.header.stride = stride;
    wctx->img_dsc.data_size = data_size;
    wctx->img_dsc.data = pixels;

#if CONFIG_OHEZ_DEBUG_OPENHAB_UI
    printf("size: %u x %u, data_size %u\n", (unsigned)wctx->img_dsc.header.w,
           (unsigned)wctx->img_dsc.header.h, (unsigned)wctx->img_dsc.data_size);
#endif

    if (wctx->img_obj != NULL)
        widget_icon_set_bitmap(wctx);
}

/* The page's chrome comes from the theme's frame now, so what is left here is
 * only the container the tiles live in -- positioned wherever that family says
 * the grid may go. */
static void chrome_create(void)
{
    const struct ui_frame_ops_s *frame = ui_style_theme()->frame;

    frame->build(ui_screen_root());

    lv_area_t a = frame->content_area();

    content = ui_plain_container(ui_screen_root());
    lv_obj_set_pos(content, a.x1, a.y1);
    lv_obj_set_size(content, lv_area_get_width(&a), lv_area_get_height(&a));

    /* No layout: the tiles are placed from the family's grid rather than
     * flowed, because three families want three different margins and gutters
     * and a wrapping flex can express only one of them. */
    lv_obj_update_layout(content);

    /* A frame is objects, so the one just built knows nothing about a message
     * box that was already up -- and on a panel with no link yet, one is:
     * main.cpp raises "WLAN / Connecting..." before this first runs, and a
     * live theme change comes through here as well. */
    Messagebox::refresh_notice();

    /* And the box itself, for the half of what it wears that a style refresh
     * cannot reach. Same call site, same reason. */
    Messagebox::restyle_all();
}

static void chrome_destroy(void)
{
    if (content != NULL)
    {
        lv_obj_delete(content);
        content = NULL;
    }

    ui_style_theme()->frame->destroy();
}

static void header_set_title(const char *text)
{
    ui_style_theme()->frame->set_title(text);
}

static void header_update(void)
{
    static int last_second;
    struct tm  timeinfo;

    /* Not device-only: port_localtime() answers from the host clock in the
     * simulator, so it shows the real time here and the night schedule can be
     * watched crossing its boundary. On the device it fails until NTP has
     * answered, and the clock label simply stays as it was. */
    if (port_localtime(&timeinfo))
    {
        if (timeinfo.tm_sec != last_second)
        {
            char text[8];

            last_second = timeinfo.tm_sec;

            /* The blinking colon is the caller's business rather than the
             * frame's: it is a matter of taste and the families differ on it.
             * A frame that declines it calls ui_frame_clock_steady() -- it
             * must not simply pass the string through, or its minutes shift
             * by the difference between its face's colon and its space. */
            lv_snprintf(text, sizeof(text), (timeinfo.tm_sec % 2 == 0) ? "%02d:%02d" : "%02d %02d",
                        timeinfo.tm_hour, timeinfo.tm_min);
            ui_style_theme()->frame->set_clock(text);
        }
    }

    static uint64_t signal_last_update;

    if (port_millis() - signal_last_update >= HEADER_SIGNAL_UPDATE_INTERVAL)
    {
        port_net_info_t net;

        signal_last_update = port_millis();
        port_net_info(&net);

        ui_style_theme()->frame->set_link(wifi_online,
                                          (net.rssi == PORT_NET_RSSI_WIRED)
                                              ? -1
                                              : openhab_ui_signal_quality(net.rssi));
    }
}

void widget_destroy(lv_obj_t *parent, struct widget_context_s *wctx)
{
    if (wctx->container != NULL)
    {
        /* child objects (label, img_obj and state_widget) will be deleted as well */
        lv_obj_delete(wctx->container);
    }

    wctx->container = NULL;
    wctx->label = NULL;
    wctx->img_obj = NULL;
    wctx->state_widget = NULL;
    wctx->item = NULL;

    wctx->update_timestamp = 0;
    wctx->refresh_request = false;
}

/* The state line along the bottom edge of a widget button. Every item type
 * that shows one uses the same reading; only the button border differs.
 *
 * One face for every reading on every page, and the caption a size below it.
 * The value used to be measured against the tile and given the largest of the
 * three faces it fitted in, which meant a page showed "OFF" at 36 px next to
 * "3.5 °C" at 22 px -- the same kind of reading in two sizes, and the size
 * moving under a value as it changed. A type hierarchy that is not kept is
 * not a hierarchy, so the sizes are fixed: font_normal for the reading,
 * font_small for the caption above it, everywhere.
 *
 * The unit is the one exception, and deliberately so: it is context, not
 * content, so it drops to font_small beside a value that keeps its face.
 *
 * The cost is the wide reading that no longer gets a face of its own: it is
 * dotted instead. font_normal is chosen over font_large for exactly that
 * reason -- at 22 px the readings a panel actually shows fit, and the ones
 * that do not were already being shrunk. */

static lv_obj_t *state_label_create(struct widget_context_s *wctx)
{
    /* One line, and dotted if even the smallest face cannot hold it. Wrapping
     * is what used to put a two-line caption through the middle of a reading.
     * The height is pinned for the same reason: left to size itself the line
     * grows upward out of a bottom-aligned widget and back through the
     * caption. */
    lv_obj_t *state_label = ui_reading_create(wctx->container, &ui_style_label_state,
                                              &ui_style_label,
                                              lv_font_get_line_height(ui_style_theme()->font_normal));

    lv_obj_move_foreground(state_label);
    lv_obj_align(state_label, LV_ALIGN_BOTTOM_MID, 0, -TILE_PAD);

    return state_label;
}

void widget_create(lv_obj_t *parent, struct widget_context_s *wctx, uint8_t slot)
{
#if CONFIG_OHEZ_DEBUG_OPENHAB_UI
    printf("widget_create: type=%u\r\n", wctx->item->getType());
#endif

    /* Create widget button.
     *
     * The v7 version gave the tile LV_LAYOUT_COLUMN_MID and then had all three
     * children opt out of it again with LV_PROTECT_POS | LV_PROTECT_FOLLOW.
     * That is a layout being fought rather than used, so the tile now has no
     * layout at all and the three alignments below stand on their own -- in v9
     * lv_obj_align() is sticky and re-applies whenever the tile is resized. */
    /* Placed from the family's grid rather than flowed. The size used to be
     * `lv_obj_get_width(parent) / 3 - 2`, which is fine while there is one
     * layout; there are three now and they differ in margin and gutter as well
     * as in where their content rectangle starts. */
    struct ui_geom_rect_s cell;

    if (ui_grid_cell(ui_style_grid(), (int16_t)lv_obj_get_width(parent),
                     (int16_t)lv_obj_get_height(parent), slot, &cell) == false)
        return;

    wctx->container = lv_obj_create(parent);
    lv_obj_remove_flag(wctx->container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(wctx->container, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(wctx->container, event_handler, LV_EVENT_CLICKED, wctx);
    lv_obj_set_pos(wctx->container, cell.x, cell.y);
    lv_obj_set_size(wctx->container, cell.w, cell.h);

    lv_obj_add_style(wctx->container, &ui_style_tile, LV_PART_MAIN);
    lv_obj_add_style(wctx->container, &ui_style_tile_pressed, ui_style_selector(LV_PART_MAIN, LV_STATE_PRESSED));

    /* The tile already changes colour when pressed; this is what makes the
     * change take time and gives the plate its deformation. Added after the
     * pressed style, so the transition governs the properties that style sets. */
    ui_motion_pressable(wctx->container);

    // Create top label object
    wctx->label = lv_label_create(wctx->container);
    lv_obj_add_style(wctx->label, &ui_style_label, LV_PART_MAIN);
    /* One line. A wrapping caption is what put "Outside Temperatur/e" through
     * the middle of "3.5 °C": at 84 to 104 px a two-line caption and a value
     * cannot both have the tile. The name is context, the reading is the
     * point, so the name is the one that gets truncated. */
    lv_label_set_long_mode(wctx->label, LV_LABEL_LONG_DOT);
    lv_label_set_text(wctx->label, wctx->item->getLabel());
    lv_obj_set_width(wctx->label, lv_pct(100));
    lv_obj_set_height(wctx->label, lv_font_get_line_height(ui_style_theme()->font_small));
    lv_obj_set_style_text_align(wctx->label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_move_foreground(wctx->label);
    lv_obj_align(wctx->label, LV_ALIGN_TOP_MID, 0, TILE_PAD);

    // Create center image object
    wctx->img_obj = lv_image_create(wctx->container);

    /* The watermark's opacity and recolour are the theme's: a dark theme has to
     * lift these dark line-art PNGs off the tile without drowning the caption
     * and state line that sit in front of them. */
    lv_obj_add_style(wctx->img_obj, &ui_style_icon, LV_PART_MAIN);

    /* A tile is created before its icon is asked for, so the placeholder is
     * what almost every tile starts as -- and, for a theme change, which keeps
     * the decoded pixels, almost none of them. */
    if (wctx->img_dsc.data_size > 0)
        widget_icon_set_bitmap(wctx);
    else
        widget_icon_set_fallback(wctx);

    lv_obj_move_background(wctx->img_obj);
    lv_obj_align(wctx->img_obj, LV_ALIGN_CENTER, 0, 0);

    // Define bottom label and button border style
    if (   wctx->item->getType() == ItemType::type_parent_link
        || wctx->item->getType() == ItemType::type_link)
    {
        lv_obj_add_style(wctx->container, &ui_style_tile_link, LV_PART_MAIN);
        lv_obj_add_style(wctx->label, &ui_style_label_state, LV_PART_MAIN);
        /* A navigation tile has no reading to protect, so its name is welcome
         * to take the middle of the tile and two lines of it. */
        lv_label_set_long_mode(wctx->label, LV_LABEL_LONG_WRAP);
        lv_obj_set_height(wctx->label, LV_SIZE_CONTENT);
        lv_obj_align(wctx->label, LV_ALIGN_CENTER, 0, 0);
    }
    else if (   wctx->item->getType() == ItemType::type_string
             || wctx->item->getType() == ItemType::type_number)
    {
        wctx->state_widget = state_label_create(wctx);
    }
    else if (wctx->item->getType() == ItemType::type_group)
    {
        lv_obj_add_style(wctx->container, &ui_style_tile_link, LV_PART_MAIN);
        wctx->state_widget = state_label_create(wctx);
    }
    else if (   wctx->item->getType() == ItemType::type_switch
             || wctx->item->getType() == ItemType::type_setpoint
             || wctx->item->getType() == ItemType::type_slider
             || wctx->item->getType() == ItemType::type_selection
             || wctx->item->getType() == ItemType::type_rollershutter
             || wctx->item->getType() == ItemType::type_player)
    {
        lv_obj_add_style(wctx->container, &ui_style_tile_active, LV_PART_MAIN);
        wctx->state_widget = state_label_create(wctx);
    }
    else if (wctx->item->getType() == ItemType::type_colorpicker)
    {
        lv_obj_add_style(wctx->container, &ui_style_tile_active, LV_PART_MAIN);

        /* A swatch rather than a label: update_state_widget() paints it with
         * the item's current colour. */
        lv_obj_t *state_obj = lv_obj_create(wctx->container);
        lv_obj_remove_flag(state_obj, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_move_foreground(state_obj);
        /* A percentage, not lv_obj_get_width(container) / 3: v9 defers layout,
         * so the container the swatch was just added to still reports zero. */
        lv_obj_set_size(state_obj, lv_pct(33), 22);
        lv_obj_add_style(state_obj, &ui_style_swatch, LV_PART_MAIN);
        lv_obj_align(state_obj, LV_ALIGN_BOTTOM_MID, 0, -6);

        wctx->state_widget = state_obj;
    }
}

/* Rebuild the tile page from the sitemap already parsed into memory.
 *
 * reload_icons tells the two callers apart. A new page has to free the old
 * icons and fetch the new ones; a theme change only needs the tiles recreated,
 * and the decoded pixels in wctx->img_dsc are still the right ones -- fetching
 * them again would be six HTTP requests for no new pixels. widget_destroy()
 * leaves img_dsc alone, which is what makes that safe. */
static void page_rebuild(lv_obj_t *parent, bool reload_icons)
{
    // cleanup page
    for (size_t i = 0; i < WIDGET_COUNT_MAX; i++)
    {
        if (reload_icons == true)
            free_icon(&widget_context[i].img_dsc);

        widget_destroy(parent, &widget_context[i]);
    }

    header_set_title(sitemap.getPageName());

    for (size_t i = 0; i < WIDGET_COUNT_MAX; i++)
    {
        widget_context[i].item = sitemap.getItem(i);

        if (widget_context[i].item->getType() == ItemType::type_unknown)
            continue;

        /* The tile goes up first and its icon lands whenever it lands. That
         * inversion is the whole of what this looks like from the outside: the
         * six icon GETs that used to happen here, one after another and before
         * anything was drawn, are six submissions that cost nothing. */
        widget_create(parent, &widget_context[i], (uint8_t)i);

        /* Whatever the family adds that a style cannot reach -- before the
         * state, not after: a decoration that tracks a value has to exist
         * before the value is written to it, or the write lands on nothing and
         * the decoration starts out empty. */
        if (ui_style_theme()->frame->decorate_tile != NULL)
            ui_style_theme()->frame->decorate_tile(widget_context[i].container,
                                                   widget_context[i].item->getType(),
                                                   (uint8_t)i);

        update_state_widget(&widget_context[i]);

        if (reload_icons == true)
            widget_icon_request(i);
    }

    /* The tiles arrive rather than appearing. Staggered, so at most three are
     * moving at once -- six at a time would be 59,000 px of invalidation per
     * frame, well past what a 40 MHz bus can carry -- and the family decides
     * what "arrive" means.
     *
     * Indexed over the container's children rather than over the slots, so a
     * page with gaps in it still staggers 0, 1, 2 without pauses where an
     * unknown item was skipped. */
    ui_motion_enter(parent);
}

void show(lv_obj_t *parent)
{
    page_rebuild(parent, true);
}

//////////////////////////////////////////////////////////////////////////////
// exported

/* An item screen changed the item under it. Marking the tile is all the page
 * has to do: the loop re-renders it, re-requests its icon (openHAB icons are
 * state-dependent) and counts the update. Registered rather than called
 * directly so that widget_context_s stays private to this file. */
static void item_changed(uint8_t slot)
{
    if (slot < WIDGET_COUNT_MAX)
        widget_context[slot].refresh_request = true;
}

void openhab_ui_setup(Config *config)
{
    current_config = config;

    item_screen_set_changed_cb(item_changed);

    /* ui_style_select() and ui_style_init() used to be called here. They now run
     * in main.cpp, before the first widget of any kind: the info label is
     * created on the top layer long before this function runs, and it draws on
     * the shared styles rather than a private one of its own. */

    chrome_create();
}

/* Whether the night variant should be in effect right now.
 *
 * "auto" needs the wall clock, which does not exist until NTP has answered.
 * Until then port_localtime() fails and the variant in effect is kept rather
 * than snapped to day, so a device powered up at night does not glare for a
 * minute and then dim. */
bool openhab_ui_night_active(Config *config)
{
    switch (config->item.ui.night_mode)
    {
    case UI_NIGHT_ON:
        return true;

    case UI_NIGHT_AUTO:
    {
        struct tm timeinfo;

        if (port_localtime(&timeinfo) == false)
            return ui_style_night();

        unsigned int hour = (unsigned int)timeinfo.tm_hour;
        unsigned int from = config->item.ui.night_from;
        unsigned int to = config->item.ui.night_to;

        /* The interesting window wraps past midnight -- 22 to 6 -- so this
         * cannot be a plain range test. from == to means the window is empty. */
        if (from <= to)
            return (hour >= from && hour < to);

        return (hour >= from || hour < to);
    }

    case UI_NIGHT_OFF:
    default:
        return false;
    }
}

/* Ask for a variant; openhab_ui_loop() carries it out.
 *
 * The switch is deliberately not done here. The web handler that calls this
 * runs inside the web server's request handling, on the loop task's stack and
 * with lv_timer_handler() not being pumped -- no place to be freeing and
 * reallocating the style property arrays that the draw path reads, let alone
 * deleting and recreating widgets. */
void openhab_ui_request_connect(const char *host, uint16_t port, const char *sitemap)
{
    /* A request that changes nothing is dropped, the way a theme request
     * naming the variant already in effect is.
     *
     * settings_apply_live() calls this on every save, whatever the save
     * touched, and carrying it out throws away the page and refetches it --
     * one page GET and up to six icon GETs -- because someone changed the
     * beeper. It also resets the page state machine out of PAGE_READY, which
     * silently disables everything that only runs on a page that has one:
     * item state polling, and putting back an open control after a theme
     * change. */
    if (   connect_current_host[0] != '\0'
        && port == connect_current_port
        && strcmp(host, connect_current_host) == 0
        && strcmp(sitemap, connect_current_sitemap) == 0)
        return;

    strlcpy(connect_pending_host, host, sizeof(connect_pending_host));
    strlcpy(connect_pending_sitemap, sitemap, sizeof(connect_pending_sitemap));
    connect_pending_port = port;
    connect_pending = true;
}

void openhab_ui_request_theme(enum ui_theme_family_e family, bool night)
{
    if (family == ui_style_family() && night == ui_style_night())
        return;

    theme_pending_family = family;
    theme_pending_night = night;
    theme_pending = true;
}

/* Switch variant without a reboot.
 *
 * The shared styles are refilled in place and reported, which carries every
 * colour and font -- including the info label on the top layer, since LVGL
 * keeps its layers in the display's screen array. Two things a style refresh
 * cannot do are done by hand: an open window is closed, because the block the
 * LCARS header ends in is an object rather than a property, and the tiles are
 * recreated, because the symbol opacities are local styles and a variant may
 * want a different marker per item type. The settings screen, if it is up, is
 * rebuilt for the same kind of reason -- and it has to be, since the theme is
 * changed from one of its own tabs. */
static void theme_apply_pending(void)
{
    theme_pending = false;

    /* Before anything is deleted: a screen-load animation owns two screens at
     * once, and what follows is about to delete one of them. */
    ui_screen_settle();

    /* An item screen is built by its type's builder, not restyled by one, so a
     * theme change rebuilds it from nothing. This is the generalisation of what
     * the old code did by deleting open_window here: some of what a family
     * contributes is objects, and a style refresh cannot reach an object. */
    ItemType reopen = item_screen_open_type();
    uint8_t  reopen_slot = item_screen_open_slot();

    item_screen_dismiss();

    /* Everything the *outgoing* family built comes down first, while its own
     * frame ops are still the ones ui_style_theme() answers with. Doing this
     * after ui_style_select() would hand the old family's objects to the new
     * family's destroy(). Tiles before the chrome, because the chrome owns the
     * container they live in. */
    for (size_t i = 0; i < WIDGET_COUNT_MAX; i++)
        widget_destroy(content, &widget_context[i]);

    chrome_destroy();

    ui_style_select(theme_pending_family, theme_pending_night);
    ui_style_apply();

#if CONFIG_OHEZ_DEBUG_OPENHAB_UI
    printf("theme_apply_pending: %s\r\n", ui_style_name());
#endif

    /* Same reason, on the other screen: the settings tab bar and keyboard set
     * some styles locally at creation, and the Info table's cells are a
     * snapshot -- none of which a style refresh can redo. */
    ui_settings_rebuild();

    chrome_create();

    header_set_title(sitemap.getPageName());
    ui_style_theme()->frame->set_link(wifi_online, -1);

    /* reload_icons stays false: the decoded pixels are still the right ones,
     * page_generation is untouched so anything already in flight still lands,
     * and the two pending flags keep their meaning. */
    if (page_state == PAGE_READY)
        page_rebuild(content, false);

    /* Put back what was open. The user did not navigate -- the theme changed
     * under them, and on the automatic night schedule they may not have
     * touched the panel at all -- so this reopens on the same item.
     *
     * Only if the tiles came back, which means only if the page was ready. A
     * theme change that arrives while the page is being refetched has no item
     * to reopen and, once the new page lands, may have no such item at all. */
    if (reopen != ItemType::type_unknown && reopen_slot < WIDGET_COUNT_MAX &&
        widget_context[reopen_slot].item != nullptr)
        item_screen_open(widget_context[reopen_slot].item, reopen_slot);
}

void openhab_ui_set_wifi_state(bool wifi_state)
{
    wifi_online = wifi_state;

    /* Straight through as well as remembered: the RSSI poll below only runs
     * every few seconds and a link that just came up should say so now. */
    ui_style_theme()->frame->set_link(wifi_online, -1);
}

void openhab_ui_connect(const char *host, uint16_t port, const char *sitemap)
{
    strlcpy(connect_current_host, host, sizeof(connect_current_host));
    strlcpy(connect_current_sitemap, sitemap, sizeof(connect_current_sitemap));
    connect_current_port = port;

    snprintf(current_website, sizeof(current_website), "http://%s:%u", host, port);

    int page_len = snprintf(current_page, sizeof(current_page), "%s/rest/sitemaps/%s/%s?type=json", current_website, sitemap, sitemap);

    if (page_len < 0 || (size_t)page_len >= sizeof(current_page))
    {
        // Truncated URLs fail every request, and the resulting error statistics
        // reboot the device, so report the actual cause.
        printf("openhab_ui_connect: sitemap URL truncated to %u bytes: %s\r\n",
               (unsigned)sizeof(current_page), current_page);
    }

    page_request(0);
}

/* Ask for current_page, `delay_ms` from now.
 *
 * Recording a want, not making a request. The submit happens in
 * page_submit_if_due() from the loop, so that the one place that talks to the
 * client task is inside the loop -- a tap on a link tile runs inside
 * lv_timer_handler(), and the web handler that reconnects runs on the server's
 * task, and neither is a place to be starting a fetch. */
static void page_request(uint64_t delay_ms)
{
    page_state = PAGE_REQUEST;
    page_retry_timeout = port_millis() + delay_ms;
}

static void page_submit_if_due(void)
{
    if (page_state != PAGE_REQUEST || port_millis() < page_retry_timeout)
        return;

    uint32_t generation = openhab_client_request_page(current_page);

    if (generation == 0)
    {
        /* No client task, or a request queue that is already full. Counted,
         * because a wedged client is exactly what the statistics are read to
         * find, and there is no other way for it to show up in them. */
        statistics.sitemap_fail_cnt++;
        page_retry_timeout = port_millis() + GET_SITEMAP_RETRY_INTERVAL;
        return;
    }

    /* Everything issued for the previous page went stale at that call. The
     * generation it handed back is what the icon and state requests for this
     * page will carry. */
    page_generation = generation;
    page_state = PAGE_WAITING;
    page_request_deadline = port_millis() + GET_SITEMAP_ANSWER_TIMEOUT;

    /* And with it, every tile stops waiting for an answer it will never get.
     *
     * A stale request produces no result at all -- the worker drops it, or
     * results_apply_one() does -- so nothing else would ever clear these, and
     * a tile whose icon was in flight when the page changed would refuse to
     * ask for its new one and stay blank for good. Here rather than in
     * page_rebuild(), because a rebuild also happens for a theme change, which
     * keeps the generation and must keep the flags with it. */
    for (size_t i = 0; i < WIDGET_COUNT_MAX; ++i)
    {
        widget_context[i].icon_pending = false;
        widget_context[i].state_pending = false;
    }
}

static void page_timeout_check(void)
{
    if (page_state != PAGE_WAITING || port_millis() < page_request_deadline)
        return;

    printf("openhab_ui: no answer within %u ms for the page at %s\r\n",
           (unsigned)GET_SITEMAP_ANSWER_TIMEOUT, current_page);

    statistics.sitemap_fail_cnt++;
    page_request(GET_SITEMAP_RETRY_INTERVAL);
}

#if CONFIG_IDF_TARGET_LINUX || CONFIG_OHEZ_TESTIF
/* The item path walks the panel to one control and opens it.
 *
 * On the simulator it is armed by OHEZ_ITEM: a sibling of OHEZ_SETTINGS, and
 * there for the same reason -- the item screens are three taps deep on a sub
 * page, which makes "show me the setpoint screen in LCARS night" a tedious
 * thing to ask for by hand and an impossible thing to ask for from a script.
 * On a bench panel the same walk is how the control interface's `nav`
 * command gets anywhere at all.
 *
 * The value is a dot-separated path of tile indices: every step but the last
 * follows that tile's linked page, and the last opens that tile's control. So
 * OHEZ_ITEM=5 opens the sixth tile of the home page, and OHEZ_ITEM=0.4 follows
 * the first tile and then opens the fifth tile of the page behind it.
 *
 * One step per page load, driven from the point where a page becomes ready,
 * because each step needs the page the previous one asked for. */
static const char *item_path;

static void item_path_step(void)
{
    if (item_path == NULL || *item_path == '\0')
        return;

    char *end;
    long  slot = strtol(item_path, &end, 10);

    if (end == item_path || slot < 0 || slot >= WIDGET_COUNT_MAX)
    {
        printf("openhab_ui: OHEZ_ITEM: \"%s\" is not a tile index\r\n", item_path);
        item_path = NULL;
        return;
    }

    Item *item = widget_context[slot].item;

    if (item == NULL || item->getType() == ItemType::type_unknown)
    {
        printf("openhab_ui: OHEZ_ITEM: tile %ld is empty\r\n", slot);
        item_path = NULL;
        return;
    }

    item_path = (*end == '.') ? end + 1 : NULL;

    if (item_path != NULL)
    {
        /* An intermediate step: follow the link, and the next page load takes
         * the step after it. */
        strlcpy(last_page, current_page, sizeof(last_page));
        strlcpy(current_page, item->getPageLink(), sizeof(current_page));
        page_request(0);
        return;
    }

    if (item_screen_find(item->getType()) != NULL)
        item_screen_open(item, (uint8_t)slot);
    else
        printf("openhab_ui: OHEZ_ITEM: tile %ld has no control screen\r\n", slot);
}

#if CONFIG_IDF_TARGET_LINUX
void openhab_ui_open_item_from_env(void)
{
    item_path = getenv("OHEZ_ITEM");
}
#endif

/* The same walk, asked for at any moment rather than only at boot.
 *
 * Two differences from the environment variable, and both are why this is not
 * simply an assignment. The path has to be copied: it arrives in a datagram
 * buffer that is reused on the next command, where getenv() returns something
 * that outlives the process. And the walk has to be started here when the page
 * is already up -- item_path_step() is otherwise driven by a page load, and on
 * a panel that has been sitting on its home page for a minute there is no next
 * page load to drive it. */
bool openhab_ui_open_item_path(const char *path)
{
    static char item_path_buf[STR_PAGE_LEN];

    if (path == NULL || *path == '\0')
        return false;

    if (strlcpy(item_path_buf, path, sizeof(item_path_buf)) >= sizeof(item_path_buf))
        return false;

    item_path = item_path_buf;

    if (page_state == PAGE_READY)
        item_path_step();

    return true;
}
#endif /* CONFIG_IDF_TARGET_LINUX || CONFIG_OHEZ_TESTIF */

static void page_result_apply(struct openhab_result_s *res)
{
    /* The scratch for the parse's document pool. A payload that rode the
     * client's receive buffer (payload_static) holds the body at its front
     * and has the rest of the buffer free behind it, and the worker is kept
     * out of the buffer until the release below -- so the tail is the pool,
     * and the whole page load makes no heap allocation at all. A payload that
     * was allocated (the simulator's fixtures) has no tail to speak of, and
     * the parse falls back to the heap. */
    char  *scratch      = NULL;
    size_t scratch_size = 0;

    if (res->payload_static == true)
    {
        size_t body = (res->payload_len + 1 + 3) & ~(size_t)3;

        if (body < OPENHAB_CLIENT_PAGE_BUFFER_SIZE + 1)
        {
            scratch      = res->payload + body;
            scratch_size = OPENHAB_CLIENT_PAGE_BUFFER_SIZE + 1 - body;
        }
    }

    if (   res->ok == true
        && res->payload != NULL
        && sitemap.parse(res->payload, res->payload_len, scratch, scratch_size) == 0)
    {
        /* Let go of the page before building the tiles rather than after. It
         * is up to 12 KB, show() is about to create six widgets and decode six
         * icons, and the parse has already copied everything it needed out of
         * it. */
        openhab_client_result_release(res);

        page_state = PAGE_READY;

        /* The sitemap came back. Messagebox::destroy() is deliberately silent
         * -- it cannot tell a recovery from a timeout -- and this is the one
         * recovery with no replacement banner to announce it, so it is said
         * here. The isUp() guard is what stops every successful poll saying
         * it. */
        if (openhab_ui_messagebox.isUp() == true)
            BEEPER_EVENT_NOTIFY();

        openhab_ui_messagebox.destroy();
        show(content);
#if CONFIG_IDF_TARGET_LINUX || CONFIG_OHEZ_TESTIF
        item_path_step();
#endif
#if CONFIG_OHEZ_DEBUG_OPENHAB_UI
        printf("Free Heap: %u\r\n", (unsigned)port_free_heap());
#endif
        statistics.sitemap_success_cnt++;
        return;
    }

    /* Unconditional, unlike the debug line it replaces, and with the heap on
     * it. This is the failure people report, and it has four unrelated causes
     * -- a transport error, a page that does not fit, a page that will not
     * parse, and a heap with no room for either -- which the log lines above
     * this one tell apart only if somebody is reading them. The pair of heap
     * figures is what says whether to look at this panel's memory at all:
     * plenty free and no large block left is a fragmented heap, and a page
     * buffer is the first thing on the panel to be refused by one. */
    printf("openhab_ui: no usable page at %s (heap %u free, %u largest)\r\n",
           current_page, (unsigned)port_free_heap(),
           (unsigned)port_largest_free_block());

    openhab_ui_messagebox.create(openhab_ui_messagebox.ERROR, "SITEMAP ACCESS FAILED", current_page, 0);

    /* The other fault with nothing behind it. The retry below keeps asking and
     * the box says so; the button is for the case where it never will, which
     * is what the watchdog this replaced was built for. */
    openhab_ui_messagebox.offerRestart();
    page_request(GET_SITEMAP_RETRY_INTERVAL);

    statistics.sitemap_fail_cnt++;
}

/* Take one finished request off the client task's queue and act on it.
 *
 * One, not all of them. This runs every few milliseconds, so a page's worth of
 * answers is absorbed in well under a frame either way, and taking one at a
 * time bounds what a single iteration can cost at one JSON parse or one PNG
 * decode. Draining the queue would put all of them in the same frame, which is
 * the stutter this whole change exists to remove, just moved. */
static void results_apply_one(void)
{
    struct openhab_result_s res;

    if (openhab_client_poll(&res) == false)
        return;

    /* Every request but a command is scoped to the page it was issued for. One
     * that outlived its page has nowhere to go: its slot may hold a different
     * item now, and free_icon() may already have released the pixels behind
     * it. So this is checked before anything below reads widget_context[]. */
    if (   res.generation != OPENHAB_CLIENT_GENERATION_ALWAYS
        && res.generation != page_generation)
    {
        openhab_client_result_release(&res);
        return;
    }

    /* Everything below indexes widget_context[] with it. The slot comes off
     * our own queue, so this can only fail if the two ever disagree about how
     * many tiles there are -- which is exactly when an out-of-bounds write
     * would be hardest to find.
     *
     * Named by the two kinds that carry a slot rather than by the kinds that
     * do not: a request added later belongs to no widget far more often than
     * it belongs to one, and the list of exceptions was one such request away
     * from silently dropping it. */
    if ((res.type == OPENHAB_REQ_ICON || res.type == OPENHAB_REQ_STATE)
        && res.slot >= WIDGET_COUNT_MAX)
    {
        openhab_client_result_release(&res);
        return;
    }

    switch (res.type)
    {
    case OPENHAB_REQ_PAGE:
        page_result_apply(&res);
        break;

    case OPENHAB_REQ_ICON:
        widget_context[res.slot].icon_pending = false;

        /* A failed or missing icon is not counted, and never was: getIcon()
         * returning 0 was silent. Counting it would let a panel whose openHAB
         * has no icon set drive itself into a restart every three minutes. */
        if (res.ok == true && res.payload != NULL)
            widget_icon_decode_and_show(&widget_context[res.slot], &res);
        break;

    case OPENHAB_REQ_STATE:
    {
        struct widget_context_s *wctx = &widget_context[res.slot];

        wctx->state_pending = false;

        if (res.ok == false)
        {
            statistics.update_fail_cnt++;
            break;
        }

        statistics.update_success_cnt++;

        /* A NULL payload is offline mode reporting that there was nothing to
         * fetch. Leaving the item alone is what keeps a switch toggled locally
         * looking like it worked -- the fixture pages carry a fixed state per
         * item, so a poll could only undo it. */
        if (res.payload == NULL || wctx->item == NULL)
            break;

        if (wctx->item->applyState(res.payload, res.payload_len) > 0)
        {
            // item value changed
            update_state_widget(wctx);

            /* And the control looking at it, if one is open. The old windows
             * never followed the server: a dimmer changed from a phone left a
             * stale number on the glass until the window was closed. */
            item_screen_refresh(res.slot);

            widget_icon_request(res.slot);
        }
        break;
    }

    case OPENHAB_REQ_SITEMAPS:
        /* Straight on to the module that owns the list. It is not the tile
         * page's business: nothing here changes, and the answer is for
         * whoever is looking at the settings. */
        openhab_sitemaps_apply(&res);
        break;

    case OPENHAB_REQ_COMMAND:
    default:
        /* The commands move in the commit after this one. */
        break;
    }

    /* Idempotent, so the page arm above having already let go of a 12 KB
     * payload before creating the tiles is not a double free. */
    openhab_client_result_release(&res);
}

void openhab_ui_loop(void)
{
    static uint64_t night_check_next_timestamp;
    static uint64_t update_ntp_next_timestamp;
    static uint64_t statistics_window_timestamp;
#if CONFIG_OHEZ_DEBUG_OPENHAB_UI
    static uint64_t statistics_timestamp;
#endif
    openhab_ui_messagebox.loop();

    results_apply_one();
    page_submit_if_due();
    page_timeout_check();

    /* What the web API asked for since the last turn: a recorded sound
     * request, carried out here because ui_beep_play() belongs to this
     * task. */
    ui_beep_loop();

    if (page_state == PAGE_READY)
    {
        for (size_t i = 0; i < WIDGET_COUNT_MAX; ++i)
        {
            if (widget_context[i].item == NULL)
                continue;

            /* Without an item link there is nothing to poll -- link and group
             * widgets often carry only a page link. Requesting "/state" then
             * fails every time and would drive the error statistics below into
             * a reboot. */
            if (   (widget_context[i].item->getType() != ItemType::type_unknown)
                && (widget_context[i].item->getType() != ItemType::type_link)
                && (widget_context[i].item->getType() != ItemType::type_parent_link)
                && (widget_context[i].item->hasLink() == true))
            {
                if (widget_context[i].refresh_request == true)
                {
                    // update widget from local state
                    widget_context[i].update_timestamp = port_millis();
                    widget_context[i].refresh_request = false;
                    update_state_widget(&widget_context[i]);
                    widget_icon_request(i);
                    statistics.update_success_cnt++;
                }

                if (   widget_context[i].state_pending == false
                    && port_millis() - widget_context[i].update_timestamp >= ITEM_UPDATE_INTERVAL)
                {
                    // ask openhab for the current remote state
                    char url[STR_URL_LEN];

                    /* Stamped at the submit and not at the answer, so the
                     * interval stays submit-to-submit as it was when the call
                     * blocked here. */
                    widget_context[i].update_timestamp = port_millis();
                    widget_context[i].refresh_request = false;

                    if (   widget_context[i].item->stateUrl(url, sizeof(url)) == false
                        || openhab_client_request_state(url, (uint8_t)i, page_generation) == false)
                    {
                        /* A URL that would not build, or a queue that would
                         * not take it. Counted here rather than nowhere: every
                         * poll still produces exactly one success or one
                         * failure, so the counters below stay a true tally. */
                        statistics.update_fail_cnt++;
                    }
                    else
                    {
                        widget_context[i].state_pending = true;
                    }
                }
            }
        }
    }

    /* Plain comparisons: port_millis() is 64 bit, so there is no rollover to
     * be safe against. These used to be (long)(millis() - deadline) >= 0, which
     * meant wrap-safe arithmetic on the device and an ordinary comparison on
     * the 64-bit host -- the same source with two different behaviours. */
    if (port_millis() >= update_ntp_next_timestamp)
    {
        update_ntp_next_timestamp = port_millis() + NTP_TIME_UPDATE_INTERVAL;

#if CONFIG_OHEZ_DEBUG_OPENHAB_UI
        printf("openhab_ui_loop: update time using ntp\r\n");
#endif
        /* Re-applied rather than set once, which is what makes the NTP host,
         * the offset and the DST flag live settings instead of restart-only
         * ones -- config_fields.cpp says so and this is why it is true. */
        port_ntp_setup(current_config->item.ntp.hostname,
                       current_config->item.ntp.gmt_offset * 3600,
                       current_config->item.ntp.daylightsaving ? 3600 : 0);
    }

    /* The automatic night schedule. Same deadline shape as above, and
     * free when nothing has changed: openhab_ui_request_theme() drops a request
     * that names the variant already in effect. */
    if (port_millis() >= night_check_next_timestamp)
    {
        night_check_next_timestamp = port_millis() + NIGHT_CHECK_INTERVAL;

        openhab_ui_request_theme(current_config->item.ui.theme,
                                 openhab_ui_night_active(current_config));
    }

    header_update();

    if (port_millis() - statistics_window_timestamp >= (STATISTICS_WINDOW_S * 1000))
    {
        statistics_window_timestamp = port_millis();

        /* This is where the connection-error watchdog was: more failures than
         * successes across one window and it called port_restart(), which on a
         * panel whose server was merely slow rebooted the thing somebody was
         * standing in front of. The sitemap box offers that restart instead --
         * see Messagebox::offerRestart() -- and what is left here is the
         * window the counters below are counted over. */
#if CONFIG_OHEZ_DEBUG_OPENHAB_UI
        printf("STATISTICS reset\r\n");
#endif
        statistics.update_fail_cnt = 0;
        statistics.update_success_cnt = 0;
        statistics.sitemap_fail_cnt = 0;
        statistics.sitemap_success_cnt = 0;
    }

#if CONFIG_OHEZ_DEBUG_OPENHAB_UI
    if (port_millis() - statistics_timestamp >= (10 * 1000))
    {
        unsigned long long up = (unsigned long long)(port_millis() / 1000);

        statistics_timestamp = port_millis();

        printf("STATISTICS Uptime: %llu days, %02llu:%02llu:%02llu UpdSucc: %u UpdFail: %u SiteSucc: %u SiteFail: %u\r\n",
               up / 86400, (up / 3600) % 24, (up / 60) % 60, up % 60,
               (unsigned)statistics.update_success_cnt, (unsigned)statistics.update_fail_cnt,
               (unsigned)statistics.sitemap_success_cnt, (unsigned)statistics.sitemap_fail_cnt);
    }
#endif

    /* Last in the loop on purpose, though no longer for the reason it once
     * was. Requests really are in flight across iterations now, so "nothing is
     * in flight while the styles are reset" is not something this can promise.
     * What it can is that results are only ever applied at the top of the
     * loop, in results_apply_one() -- so neither of the two below can be
     * interrupted by a page arriving in the middle of recreating the tiles it
     * is about to replace. */
    if (connect_pending == true)
    {
        connect_pending = false;
        openhab_ui_connect(connect_pending_host, connect_pending_port,
                           connect_pending_sitemap);
    }

    if (theme_pending == true)
        theme_apply_pending();
}
