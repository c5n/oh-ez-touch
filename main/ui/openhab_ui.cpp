#include "sdkconfig.h"

#include "openhab_ui.hpp"
#include "openhab/openhab_client.hpp"
#include "openhab/openhab_connector.hpp"
#include "ui_infolabel.hpp"
#include "ui_beep.hpp"
#include "ui_settings.hpp"
#include "items/item_screen.hpp"
#include "ui_screen.hpp"
#include "ui_motion.hpp"
#include "ui_style.hpp"

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
 * asked for again. Three times the client's own per-request timeout, because a
 * page queued behind a few icons has to be given time to reach the front.
 *
 * The client task answers every request it does not drop, so this should never
 * fire. It exists because the one way the asynchronous shape can fail that the
 * synchronous one could not is by waiting forever -- and a page that never
 * loads and never retries is a blank screen that the connection-error watchdog
 * further down never notices, because nothing is counting. */
#ifndef GET_SITEMAP_ANSWER_TIMEOUT
#define GET_SITEMAP_ANSWER_TIMEOUT 15000
#endif

#ifndef NTP_TIME_UPDATE_INTERVAL
#define NTP_TIME_UPDATE_INTERVAL (60 * 60 * 1000)
#endif

#ifndef CONNECTION_ERROR_TIMEOUT_S
#define CONNECTION_ERROR_TIMEOUT_S 180
#endif

#define HEADER_SIGNAL_UPDATE_INTERVAL 5000

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

Infolabel openhab_ui_infolabel;

static Config *current_config;

struct header_s
{
    lv_obj_t *container = nullptr;
    struct
    {
        lv_obj_t *clock = nullptr;
        lv_obj_t *title = nullptr;
        lv_obj_t *wifi = nullptr;
        lv_obj_t *signal = nullptr;
    } item;
};

static struct header_s header;

static lv_obj_t *content = nullptr;

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
    openhab_client_command(ctx->item->getLink(), ctx->item->getStateText());
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
static enum ui_theme_family_e theme_pending_family;
static bool theme_pending_night;
static char current_page[STR_PAGE_LEN];
static char last_page[STR_PAGE_LEN];
static char current_website[STR_WEBSITE_LEN];

uint8_t openhab_ui_signal_quality(int8_t rssi)
{
    if (rssi < -100)
        return 0;
    else if (rssi > -50)
        return 100;
    else
        return 2 * (rssi + 100);
}

/* The item's pattern comes straight from openHAB and is applied to the item's
 * numeric state. "%d" (the default when openHAB sends no pattern) needs an
 * integer argument, every other pattern is fed the float. */
static void set_label_from_pattern(lv_obj_t *label, Item *item, float value)
{
    const char *pattern = item->getNumberPattern();

    if (strncmp(pattern, "%d", 2) == 0)
        lv_label_set_text_fmt(label, pattern, (uint16_t)value);
    else
        lv_label_set_text_fmt(label, pattern, value);
}

/* A plain container: v9's lv_obj_create() comes with theme background, border,
 * radius, padding and scrolling, none of which the v7 lv_cont it replaces had. */
static lv_obj_t *plain_container(lv_obj_t *parent)
{
    lv_obj_t *obj = lv_obj_create(parent);

    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_set_style_pad_gap(obj, 0, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_radius(obj, 0, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);

    return obj;
}

/* Touching the status bar is how the settings screen is reached. */
static void header_event_handler(lv_event_t *e)
{
    LV_UNUSED(e);

    if (ui_settings_is_open() == false)
    {
        BEEPER_EVENT_WINDOW();
        ui_settings_open(SETTINGS_TAB_INFO);
    }
}

/* openHAB sends a colorpicker's state as "h,s,v": degrees, then two percents. */
lv_color_hsv_t hsvCStringToLVColor(const char *hsvstring)
{
    const char *ptr = hsvstring;
    char       *endptr;

    lv_color_hsv_t hsvcolor;

    hsvcolor.h = strtol(ptr, &endptr, 10);
    hsvcolor.s = strtol(endptr + 1, &endptr, 10);
    hsvcolor.v = strtol(endptr + 1, &endptr, 10);

    return hsvcolor;
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
            BEEPER_EVENT_LINK_BACK()
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
            ctx->item->setStateText("ON");
        else
            ctx->item->setStateText("OFF");

        item_publish(ctx);
        ctx->refresh_request = true;
        BEEPER_EVENT_CHANGE();
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
            lv_label_set_text(label, item->getSelectionLabel(index));
            return;
        }
    }

    lv_label_set_text(label, item->getStateText());
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
            lv_label_set_text(ctx->state_widget, ctx->item->getTransformedStateText());
        else
            lv_label_set_text(ctx->state_widget, ctx->item->getStateText());
        break;

    case ItemType::type_group:
        // A group aggregates its members, which may be a number or a text.
        if (ctx->item->stateIsNumber() == true)
            set_label_from_pattern(ctx->state_widget, ctx->item, ctx->item->getStateNumber());
        else
            lv_label_set_text(ctx->state_widget, ctx->item->getStateText());
        break;

    case ItemType::type_number:
    case ItemType::type_setpoint:
    case ItemType::type_slider:
    case ItemType::type_rollershutter:
        set_label_from_pattern(ctx->state_widget, ctx->item, ctx->item->getStateNumber());
        break;

    case ItemType::type_switch:
    case ItemType::type_selection:
        set_label_from_mapping(ctx->state_widget, ctx->item);
        break;

    case ItemType::type_player:
        lv_label_set_text(ctx->state_widget, ctx->item->getStateText());
        break;

    case ItemType::type_colorpicker:
    {
        lv_color_hsv_t hsv = hsvCStringToLVColor(ctx->item->getStateText());
        lv_obj_set_style_bg_color(ctx->state_widget, lv_color_hsv_to_rgb(hsv.h, hsv.s, hsv.v), 0);
        break;
    }

    default:
        ESP_LOGW("openhab_ui", "update_state_widget: unknown or unsupported item type id: %d",
                 (int)ctx->item->getType());
        break;
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

/* Decode a PNG that has arrived and show it. load_icon()'s other half.
 *
 * The decode stays on this task deliberately. It is single-digit milliseconds
 * against the hundreds the fetch can take, it keeps what is in flight down to
 * the size of the PNG rather than the size of the pixels, and it keeps the
 * malloc and the free of a block LVGL will hold a pointer to on one task.
 */
static void widget_icon_decode_and_show(struct widget_context_s *wctx,
                                        const void *png, size_t png_len)
{
    unsigned char *png_decoded;
    /* unsigned, not uint32_t: lodepng_decode32() takes unsigned *, and the two
     * are the same type on the host and different ones on the device. */
    unsigned png_width, png_height;

#if CONFIG_OHEZ_DEBUG_OPENHAB_UI
    printf("widget_icon: %s ", wctx->item != NULL ? wctx->item->getIconName() : "?");
#endif

    // Decode the loaded image in ARGB8888
    unsigned int error = lodepng_decode32(&png_decoded, &png_width, &png_height,
                                          (const unsigned char *)png, png_len);

    if (error)
    {
        printf("PNG decode error %u: %s\n", error, lodepng_error_text(error));
        return;
    }

    convert_color_depth(png_decoded, png_width * png_height);

    /* The old pixels go only now that there are new ones to put in their
     * place. load_icon() freed them up front and so left the tile blank when
     * the fetch or the decode then failed; a refresh that does not arrive now
     * leaves the icon that is already there. */
    if (wctx->img_dsc.data != NULL)
    {
        lv_image_cache_drop(&wctx->img_dsc);
        free((void *)wctx->img_dsc.data);
    }

    // Initialize an image descriptor for LVGL with the decoded image
    wctx->img_dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    wctx->img_dsc.header.cf = LV_COLOR_FORMAT_ARGB8888;
    wctx->img_dsc.header.flags = 0;
    wctx->img_dsc.header.w = png_width;
    wctx->img_dsc.header.h = png_height;
    wctx->img_dsc.header.stride = png_width * 4;
    wctx->img_dsc.data_size = png_width * png_height * 4;
    wctx->img_dsc.data = png_decoded;

#if CONFIG_OHEZ_DEBUG_OPENHAB_UI
    printf("size: %u x %u, data_size %u\n", png_width, png_height,
           (unsigned)wctx->img_dsc.data_size);
#endif

    if (wctx->img_obj != NULL)
        widget_icon_set_bitmap(wctx);
}

#define HEADER_HEIGHT (LV_DPI_DEF / 3)

static void header_create(void)
{
    /* A flex row of clock, title, signal and wifi. The v7 version aligned the
     * four labels to the container's edges by hand; SPACE_BETWEEN with the
     * title growing into the slack gives the same arrangement without the pixel
     * offsets. The height is fixed rather than LV_SIZE_CONTENT, because
     * centring children inside a content-sized parent would be circular. */
    header.container = plain_container(lv_screen_active());
    lv_obj_set_size(header.container, lv_pct(100), HEADER_HEIGHT);
    lv_obj_set_pos(header.container, 0, 0);
    lv_obj_set_style_pad_hor(header.container, LV_DPI_DEF / 10, 0);
    lv_obj_set_style_pad_column(header.container, LV_DPI_DEF / 20, 0);
    lv_obj_set_flex_flow(header.container, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header.container, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_add_flag(header.container, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(header.container, header_event_handler, LV_EVENT_CLICKED, NULL);

    header.item.clock = lv_label_create(header.container);
    lv_label_set_text(header.item.clock, "--:--");

    header.item.title = lv_label_create(header.container);
    lv_label_set_text(header.item.title, "Welcome to OhEzTouch");
    /* DOT, not SCROLL. A scrolling label re-invalidates its own box on every
     * refresh period for the life of the device: at 160x22 that is 1.4 ms of
     * SPI every 16 ms, 9% of the bus, spent animating a page title nobody is
     * waiting to finish reading. It also means the panel is never idle, so no
     * frame budget is ever really free. Truncating costs nothing and the page
     * title is short. */
    lv_label_set_long_mode(header.item.title, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(header.item.title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_flex_grow(header.item.title, 1);

    header.item.signal = lv_label_create(header.container);
    lv_label_set_text(header.item.signal, "  %");

    header.item.wifi = lv_label_create(header.container);
    lv_label_set_text(header.item.wifi, LV_SYMBOL_POWER);
}

static void header_set_title(const char* text)
{
    lv_label_set_text(header.item.title, text);
}

static void header_update()
{
    static int last_second;
    struct tm timeinfo;

    /* Not device-only: port_localtime() answers from the host clock in the
     * simulator, so it shows the real time here and the night schedule can be
     * watched crossing its boundary. On the device it fails until NTP has
     * answered, and the clock label simply stays as it was. */
    if (port_localtime(&timeinfo))
    {
        if (timeinfo.tm_sec != last_second)
        {
            last_second = timeinfo.tm_sec;

            if (timeinfo.tm_sec % 2 == 0)
                lv_label_set_text_fmt(header.item.clock, "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
            else
                lv_label_set_text_fmt(header.item.clock, "%02d %02d", timeinfo.tm_hour, timeinfo.tm_min);
        }
    }

    static uint64_t signal_last_update;

    if (port_millis() - signal_last_update >= HEADER_SIGNAL_UPDATE_INTERVAL)
    {
        port_net_info_t net;

        signal_last_update = port_millis();
        port_net_info(&net);

        /* A wired link has no signal strength, and a percentage would be
         * invented. Say "connected by wire" instead. */
        if (net.rssi == PORT_NET_RSSI_WIRED)
            lv_label_set_text(header.item.signal, LV_SYMBOL_SHUFFLE);
        else
            lv_label_set_text_fmt(header.item.signal, "%02d%%",
                                  openhab_ui_signal_quality(net.rssi));
    }
}

static void content_create(void)
{
    int32_t hres = lv_display_get_horizontal_resolution(NULL);
    int32_t vres = lv_display_get_vertical_resolution(NULL);

    content = plain_container(lv_screen_active());

    lv_obj_set_size(content, hres, vres - HEADER_HEIGHT);
    lv_obj_set_pos(content, 0, HEADER_HEIGHT);

    /* LV_LAYOUT_PRETTY_MID with six tiles sized to a third of the width and
     * half the height: wrapping flex, spaced evenly on both axes. */
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(content, LV_FLEX_ALIGN_SPACE_EVENLY,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_SPACE_EVENLY);
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
 * that shows one uses the same label; only the button border differs. */
static lv_obj_t *state_label_create(struct widget_context_s *wctx)
{
    lv_obj_t *state_label = lv_label_create(wctx->container);

    lv_obj_set_style_text_align(state_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(state_label, LV_LABEL_LONG_WRAP);
    lv_obj_add_style(state_label, &ui_style_label_state, LV_PART_MAIN);
    lv_obj_move_foreground(state_label);
    lv_obj_set_width(state_label, lv_pct(100));
    lv_obj_align(state_label, LV_ALIGN_BOTTOM_MID, 0, -3);

    return state_label;
}

void widget_create(lv_obj_t *parent, struct widget_context_s *wctx)
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
    wctx->container = lv_obj_create(parent);
    lv_obj_remove_flag(wctx->container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(wctx->container, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(wctx->container, event_handler, LV_EVENT_CLICKED, wctx);
    lv_obj_set_size(wctx->container,
                    lv_obj_get_width(parent) / 3 - 2,
                    lv_obj_get_height(parent) / 2 - 2);

    lv_obj_add_style(wctx->container, &ui_style_tile, LV_PART_MAIN);
    lv_obj_add_style(wctx->container, &ui_style_tile_pressed, ui_style_selector(LV_PART_MAIN, LV_STATE_PRESSED));

    /* The tile already changes colour when pressed; this is what makes the
     * change take time and gives the plate its deformation. Added after the
     * pressed style, so the transition governs the properties that style sets. */
    ui_motion_pressable(wctx->container);

    // Create top label object
    wctx->label = lv_label_create(wctx->container);
    lv_obj_add_style(wctx->label, &ui_style_label, LV_PART_MAIN);
    lv_label_set_long_mode(wctx->label, LV_LABEL_LONG_WRAP);
    lv_label_set_text(wctx->label, wctx->item->getLabel());
    lv_obj_set_width(wctx->label, lv_pct(100));
    lv_obj_set_style_text_align(wctx->label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_move_foreground(wctx->label);
    lv_obj_align(wctx->label, LV_ALIGN_TOP_MID, 0, 3);

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
        widget_create(parent, &widget_context[i]);
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

    header_create();
    content_create();
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

    ui_style_select(theme_pending_family, theme_pending_night);
    ui_style_apply();

#if CONFIG_OHEZ_DEBUG_OPENHAB_UI
    printf("theme_apply_pending: %s\r\n", ui_style_name());
#endif

    /* Same reason, on the other screen: the settings tab bar and keyboard set
     * some styles locally at creation, and the Info table's cells are a
     * snapshot -- none of which a style refresh can redo. */
    ui_settings_rebuild();

    if (page_state == PAGE_READY)
        page_rebuild(content, false);

    /* Put back what was open. The user did not navigate -- the theme changed
     * under them, and on the automatic night schedule they may not have touched
     * the panel at all -- so this reopens on the same item and lets the
     * builder's own entrance play. */
    if (reopen != ItemType::type_unknown && reopen_slot < WIDGET_COUNT_MAX)
        item_screen_open(widget_context[reopen_slot].item, reopen_slot);
}

void openhab_ui_set_wifi_state(bool wifi_state)
{
    if (wifi_state == true)
        lv_label_set_text(header.item.wifi, LV_SYMBOL_WIFI);
    else
        lv_label_set_text(header.item.wifi, LV_SYMBOL_REFRESH);
}

void openhab_ui_connect(const char *host, uint16_t port, const char *sitemap)
{
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
         * because a wedged client is exactly what the connection-error
         * watchdog restarts the panel for, and there is no other way for it to
         * find out. */
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

    printf("openhab_ui_loop: no answer for the page at: %s\r\n", current_page);

    statistics.sitemap_fail_cnt++;
    page_request(GET_SITEMAP_RETRY_INTERVAL);
}

#if CONFIG_IDF_TARGET_LINUX
/* OHEZ_ITEM walks the simulator to one control and opens it.
 *
 * A sibling of OHEZ_SETTINGS, and there for the same reason: the item screens
 * are three taps deep on a sub page, which makes "show me the setpoint screen
 * in LCARS night" a tedious thing to ask for by hand and an impossible thing to
 * ask for from a script.
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

void openhab_ui_open_item_from_env(void)
{
    item_path = getenv("OHEZ_ITEM");
}
#endif /* CONFIG_IDF_TARGET_LINUX */

static void page_result_apply(struct openhab_result_s *res)
{
    if (   res->ok == true
        && res->payload != NULL
        && sitemap.parse(res->payload, res->payload_len) == 0)
    {
        /* Let go of the page before building the tiles rather than after. It
         * is up to 12 KB, show() is about to create six widgets and decode six
         * icons, and the parse has already copied everything it needed out of
         * it. */
        openhab_client_result_release(res);

        page_state = PAGE_READY;
        openhab_ui_infolabel.destroy();
        show(content);
#if CONFIG_IDF_TARGET_LINUX
        item_path_step();
#endif
#if CONFIG_OHEZ_DEBUG_OPENHAB_UI
        printf("Free Heap: %u\r\n", (unsigned)port_free_heap());
#endif
        statistics.sitemap_success_cnt++;
        return;
    }

#if CONFIG_OHEZ_DEBUG_OPENHAB_UI
    printf("openhab_ui_loop: no usable page at: %s\r\n", current_page);
#endif
    openhab_ui_infolabel.create(openhab_ui_infolabel.ERROR, "SITEMAP ACCESS FAILED", current_page, 0);
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
     * would be hardest to find. */
    if (res.type != OPENHAB_REQ_PAGE && res.type != OPENHAB_REQ_COMMAND
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
            widget_icon_decode_and_show(&widget_context[res.slot], res.payload, res.payload_len);
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
    static uint64_t connection_error_handling_timestamp;
#if CONFIG_OHEZ_DEBUG_OPENHAB_UI
    static uint64_t statistics_timestamp;
#endif
    openhab_ui_infolabel.loop();

    results_apply_one();
    page_submit_if_due();
    page_timeout_check();

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
                         * failure, so the watchdog below counts what it always
                         * counted. */
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

    if (port_millis() - connection_error_handling_timestamp >= (CONNECTION_ERROR_TIMEOUT_S * 1000))
    {
        connection_error_handling_timestamp = port_millis();

        if ((statistics.update_fail_cnt > statistics.update_success_cnt) || (statistics.sitemap_fail_cnt > statistics.sitemap_success_cnt))
        {
            /* More failures than successes for a minute: something is wedged
             * that a restart has a fair chance of clearing. On the host this
             * exits the process, which is the same statement. */
            port_restart();
        }

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
