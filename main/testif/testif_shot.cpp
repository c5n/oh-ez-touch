/**
 * @file testif_shot.cpp
 *
 * The screenshot: LVGL's own frame buffer, streamed out of the web server
 * exactly as it stands.
 *
 * Nothing is encoded here and nothing is allocated for it. The panel this
 * firmware really runs on is short of RAM, and a PNG encoder wants an output
 * buffer and a compressor's working set that it cannot spare -- so the wire
 * carries raw pixels and tools/ohez_ctl.py makes the PNG on the development
 * machine, where memory is free. That choice is also what keeps the device
 * half small: a panel would stream areas out of flush_cb with no buffer at
 * all, into the very same header.
 *
 * The simulator keeps a whole frame already. With LV_SDL_RENDER_MODE set to
 * LV_DISPLAY_RENDER_MODE_PARTIAL, LVGL's SDL software backend renders into
 * small draw buffers and copies each flushed area into `fb_act`, a persistent
 * full-resolution buffer it then blits to the window in one go (see
 * flush_cb() and window_update() in lvgl/src/drivers/sdl/lv_sdl_sw.c). So
 * `fb_act` is not a re-render of the widget tree -- it is the pixels that are
 * on the screen, including the banners on the top layer and whatever a
 * transition is halfway through, which a re-render would miss.
 *
 * Reaching it costs the one piece of coupling in this change: the struct
 * lv_sdl_backend_get_display_data() hands back is defined in that .c file and
 * not in any header, so its layout is repeated below. check_layout() tests it
 * against an invariant the driver itself maintains before anything trusts it.
 */

#include "sdkconfig.h"

#include "testif.hpp"

#if CONFIG_IDF_TARGET_LINUX

#include <stdio.h>
#include <string.h>

#include <lvgl.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "port_display.h"
#include "port_sys.h"
#include "web/webui_transport.h"

#include "testif_internal.hpp"

static const char *TAG = "testif";

/* Declared in lvgl/src/drivers/sdl/lv_sdl_private.h, which is not included
 * here: that header pulls in the OpenGL ES backend's private headers, and this
 * file wants one function out of it. */
extern "C" void *lv_sdl_backend_get_display_data(lv_display_t *display);

/**
 * The SDL software backend's per-display state, copied from
 * lvgl/src/drivers/sdl/lv_sdl_sw.c.
 *
 * Only `fb_act` is read. The fields before it are here to place it, and they
 * are named as upstream names them so that a diff against that file is
 * possible at all.
 */
struct sdl_sw_display_data_s
{
    void    *texture;
    void    *renderer;
    uint8_t *fb1;
    uint8_t *fb2;
    uint8_t *fb_act;
    uint8_t *buf1;
    uint8_t *buf2;
    uint8_t *rotated_buf;
    size_t   rotated_buf_size;
};

/* Set once at startup. A layout that does not check out costs the route and
 * not the run -- the simulator is still perfectly usable without screenshots,
 * and serving whatever happened to be at that offset would be worse than
 * serving nothing. */
static bool shot_available;

/* How long the UI may be held still for one transfer. Reached only by a client
 * that stops reading mid-body; the frame is 150 KB over loopback otherwise. */
#define HOLD_MAX_MS 500

static volatile bool     hold_requested;
static volatile bool     hold_acked;
static volatile uint64_t hold_since;

/* --------------------------------------------------------------- the buffer */

static struct sdl_sw_display_data_s *display_data(lv_display_t *disp)
{
    return (struct sdl_sw_display_data_s *)lv_sdl_backend_get_display_data(disp);
}

static bool check_layout(lv_display_t *disp)
{
    if (disp == NULL)
        return false;

    if (lv_display_get_render_mode(disp) != LV_DISPLAY_RENDER_MODE_PARTIAL)
    {
        /* In every other mode fb_act is whichever draw buffer was flushed
         * last, which is not a whole frame. */
        ESP_LOGW(TAG, "display is not in partial render mode; no screenshots");
        return false;
    }

    if (lv_display_get_color_format(disp) != LV_COLOR_FORMAT_RGB565)
    {
        ESP_LOGW(TAG, "display is not RGB565; no screenshots");
        return false;
    }

    struct sdl_sw_display_data_s *ddata = display_data(disp);

    if (ddata == NULL || ddata->fb1 == NULL || ddata->fb_act == NULL)
    {
        ESP_LOGW(TAG, "no SDL frame buffer; no screenshots");
        return false;
    }

    /* The invariant: in partial mode lv_sdl_sw.c assigns fb_act = fb1 when it
     * allocates them and never moves it again. Two pointers that agree is weak
     * evidence on its own and decisive here -- if the struct above had drifted
     * out of step with upstream, these two reads would be coming from
     * different fields and would almost certainly differ. */
    if (ddata->fb_act != ddata->fb1)
    {
        ESP_LOGW(TAG, "SDL display data layout has changed; no screenshots");
        return false;
    }

    return true;
}

/* ----------------------------------------------------------------- the hold */

bool testif_frame_hold(void)
{
    if (hold_requested == false)
    {
        hold_acked = false;
        return false;
    }

    if (hold_acked == false)
    {
        /* First sight of the request, and the moment that matters: being here
         * means this task is *between* two lv_timer_handler() calls, so no
         * flush is in progress and the frame is whole. The reader waits for
         * this acknowledgement before it touches a pixel. */
        hold_acked = true;
        hold_since = port_millis();
    }
    else if (port_millis() - hold_since > HOLD_MAX_MS)
    {
        /* A reader that went away. Letting the UI freeze until the process is
         * killed would be a worse failure than a torn screenshot. */
        ESP_LOGW(TAG, "screenshot hold timed out");
        hold_requested = false;
        hold_acked     = false;
        return false;
    }

    return true;
}

/* ------------------------------------------------------------------ the route */

/* "OHFB", a version, a format, the geometry. In the body rather than in HTTP
 * headers so that a saved file still says what it is, and so that the device
 * half -- which would send areas as they are flushed rather than one frame --
 * can describe itself in the same way. */
#define SHOT_HEADER_SIZE 16
#define SHOT_FORMAT_RGB565_LE 1

static void put_u16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)(v >> 8);
}

static void handle_screenshot(webui_request_t *req)
{
    lv_display_t *disp = lv_display_get_default();

    if (shot_available == false || disp == NULL)
    {
        webui_send(req, 503, "text/plain", "no frame buffer\n");
        return;
    }

    uint32_t width  = (uint32_t)lv_display_get_horizontal_resolution(disp);
    uint32_t height = (uint32_t)lv_display_get_vertical_resolution(disp);
    uint32_t stride = lv_draw_buf_width_to_stride(width, LV_COLOR_FORMAT_RGB565);

    /* Ask the UI task to stop redrawing, and wait to be told it has. Without
     * the acknowledgement this would race a flush already in progress and the
     * frame could be half old. */
    hold_requested = true;

    for (unsigned waited = 0; hold_acked == false && waited < HOLD_MAX_MS; waited += 5)
        vTaskDelay(pdMS_TO_TICKS(5));

    if (hold_acked == false)
    {
        hold_requested = false;
        webui_send(req, 503, "text/plain", "the UI did not settle\n");
        return;
    }

    uint8_t header[SHOT_HEADER_SIZE];

    memcpy(header, "OHFB", 4);
    put_u16(header + 4, 1);
    put_u16(header + 6, SHOT_FORMAT_RGB565_LE);
    put_u16(header + 8, (uint16_t)width);
    put_u16(header + 10, (uint16_t)height);
    put_u16(header + 12, (uint16_t)(stride & 0xFFFF));
    put_u16(header + 14, (uint16_t)(stride >> 16));

    webui_begin_chunked(req, "application/octet-stream");
    webui_write(req, (const char *)header, sizeof(header));

    /* Straight out of the frame buffer, in pieces small enough not to ask the
     * transport for a large write, and with no copy of the frame anywhere. */
    const uint8_t *fb    = display_data(disp)->fb_act;
    size_t         total = (size_t)stride * height;

    for (size_t sent = 0; sent < total; sent += 8192)
    {
        size_t chunk = total - sent;

        if (chunk > 8192)
            chunk = 8192;

        webui_write(req, (const char *)(fb + sent), chunk);
    }

    webui_end_chunked(req);

    hold_requested = false;
    hold_acked     = false;
}

void testif_shot_init(void)
{
    shot_available = check_layout(lv_display_get_default());

    if (shot_available == false)
        return;

    webui_transport_route("/screenshot.raw", WEBUI_GET, handle_screenshot);
}

const char *testif_cmd_shot(const testif_cmd_t *cmd, char *out, size_t out_size)
{
    (void)cmd;

    if (shot_available == false)
        return "no frame buffer";

    /* The command does not carry the frame; it says where to fetch it. Which
     * is the whole of its use: a client that has the URL does not need to know
     * how the web server was configured. */
    snprintf(out, out_size, "http://127.0.0.1:%u/screenshot.raw", (unsigned)webui_transport_port());

    return NULL;
}

#elif CONFIG_OHEZ_TESTIF

#include "testif_internal.hpp"

/* The device half this file's header sketches is still not written: a panel
 * renders into small draw buffers and keeps no whole frame to serve, so the
 * command answers with a refusal rather than with a picture of nothing. */
void testif_shot_init(void) {}

/* Nothing to hold: there is no whole frame a reader could be copying. */
bool testif_frame_hold(void)
{
    return false;
}

const char *testif_cmd_shot(const testif_cmd_t *cmd, char *out, size_t out_size)
{
    (void)cmd;
    (void)out;
    (void)out_size;

    return "no frame buffer on the device";
}

#endif
