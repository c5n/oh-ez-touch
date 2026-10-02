/**
 * @file item_image.cpp
 *
 * An Image item's picture, as large as the screen allows: a doorbell's
 * snapshot, or any camera's.
 *
 * The picture is fetched and decoded on the client task (see image_decode.h)
 * at half the body's size in each direction -- 160x92 in landscape, about
 * 29 KB -- and drawn at twice that. Half, because the full body is 115 KB and
 * the heap has nowhere near that in one piece; the upscale is soft, but a
 * face at the door is recognisable at that, and a sharp one would need PSRAM.
 *
 * It is fetched again on a tap, on every sitemap event for its widget (a new
 * snapshot arrives as one), on the widget's refresh= if it has one, and when
 * the bell rings while it is open. The bell can also open it from any page;
 * see item_image_popup().
 */
#include "item_image.hpp"

#include "item_screen.hpp"

#include "openhab/openhab_client.hpp"
#include "port/port_sys.h"
#include "ui/ui_style.hpp"

#include <misc/cache/instance/lv_image_cache.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Never refetched faster than this, whatever refresh= says: a whole JPEG is
 * the most expensive request this panel makes, and the one task that makes
 * requests is blocked while it does. */
#define IMAGE_REFRESH_MIN_MS 2000

/* What has to be left in one piece once the picture has its block, so that
 * the next page load or icon decode is not the allocation that fails. */
#define IMAGE_HEAP_RESERVE 16384

/* The one picture on screen. One, because there is one item screen. */
static struct
{
    struct item_view_s *view;
    lv_obj_t *image;
    lv_obj_t *status;
    lv_image_dsc_t dsc;
    uint8_t tag;          /* of the request the screen is waiting for */
    bool in_flight;
    bool again;           /* asked for again while one was in flight */
    lv_timer_t *refresh_timer;
    lv_timer_t *close_timer;
} pic;

/* The Item a ring opens: not on the page, so nobody else owns one. */
static Item popup_item;

static void status_set(const char *text)
{
    if (pic.status == NULL)
        return;

    if (text == NULL)
    {
        lv_obj_set_hidden(pic.status, true);
        return;
    }

    lv_label_set_text(pic.status, text);
    lv_obj_set_hidden(pic.status, false);
}

static void pixels_free(void)
{
    if (pic.dsc.data == NULL)
        return;

    /* Before the free, for the reason free_icon() gives: LVGL caches by
     * source pointer, and a redraw would otherwise walk freed memory. */
    if (pic.image != NULL)
        lv_image_set_src(pic.image, NULL);

    lv_image_cache_drop(&pic.dsc);
    free((void *)pic.dsc.data);
    memset(&pic.dsc, 0, sizeof(pic.dsc));
}

static void request(void)
{
    struct item_view_s *v = pic.view;
    char url[STR_URL_LEN];

    if (v == NULL)
        return;

    if (pic.in_flight == true)
    {
        pic.again = true;
        return;
    }

    if (v->item->stateUrl(url, sizeof(url)) == false)
    {
        status_set("No picture");
        return;
    }

    /* Half the body, and less if the heap cannot spare it. The old picture
     * is still up while the new one is decoded, so both have to fit. */
    uint16_t max_w = (uint16_t)(lv_obj_get_content_width(v->body) / 2);
    uint16_t max_h = (uint16_t)(lv_obj_get_content_height(v->body) / 2);
    size_t largest = port_largest_free_block();

    /* 0 is the simulator's "cannot say", not "nothing free". */
    while (largest != 0 && max_w > 40
           && (size_t)max_w * max_h * 2 + IMAGE_HEAP_RESERVE > largest)
    {
        max_w = (uint16_t)(max_w * 3 / 4);
        max_h = (uint16_t)(max_h * 3 / 4);
    }

    if (openhab_client_request_image(url, max_w, max_h, ++pic.tag) == false)
    {
        status_set("Busy");
        return;
    }

    pic.in_flight = true;
    pic.again = false;

    if (pic.dsc.data == NULL)
        status_set("Loading...");
}

/* Skipped while a fetch is still on its way: on a slow link a refresh= shorter
 * than the fetch would otherwise queue the next one behind every answer. */
static void refresh_timer_cb(lv_timer_t *t)
{
    LV_UNUSED(t);

    if (pic.in_flight == false)
        request();
}

static void close_timer_cb(lv_timer_t *t)
{
    LV_UNUSED(t);

    /* The timer is deleted with the screen, so it is ours that is up. */
    pic.close_timer = NULL;
    lv_timer_delete(t);
    item_screen_close();
}

/* A tap: look again -- and somebody is looking, so the ring's own timeout no
 * longer applies. */
static void tap_event(lv_event_t *e)
{
    LV_UNUSED(e);

    if (pic.close_timer != NULL)
    {
        lv_timer_delete(pic.close_timer);
        pic.close_timer = NULL;
    }

    request();
}

static void build(struct item_view_s *v)
{
    memset(&pic.dsc, 0, sizeof(pic.dsc));
    pic.view = v;
    pic.in_flight = false;
    pic.again = false;

    /* Black under the picture, whatever the theme: a letterbox in the
     * theme's tile colour looks like a frame around a photograph. */
    lv_obj_set_style_bg_color(v->body, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(v->body, LV_OPA_COVER, 0);
    lv_obj_set_clickable(v->body, true);
    lv_obj_add_event_cb(v->body, tap_event, LV_EVENT_CLICKED, v);
    lv_obj_update_layout(v->body);

    pic.image = lv_image_create(v->body);
    lv_obj_center(pic.image);
    lv_obj_set_clickable(pic.image, false);

    pic.status = lv_label_create(v->body);
    lv_obj_add_style(pic.status, &ui_style_label, LV_PART_MAIN);
    lv_obj_set_style_text_color(pic.status, lv_color_white(), 0);
    lv_obj_center(pic.status);

    v->value = pic.status;

    uint32_t every = v->item->getRefreshMs();

    if (every > 0)
    {
        if (every < IMAGE_REFRESH_MIN_MS)
            every = IMAGE_REFRESH_MIN_MS;

        pic.refresh_timer = lv_timer_create(refresh_timer_cb, every, NULL);
    }

    request();
}

/* A sitemap event for the widget: a new snapshot, most likely. */
static void refresh(struct item_view_s *v)
{
    LV_UNUSED(v);
    request();
}

static void destroy(struct item_view_s *v)
{
    LV_UNUSED(v);

    if (pic.refresh_timer != NULL)
        lv_timer_delete(pic.refresh_timer);

    if (pic.close_timer != NULL)
        lv_timer_delete(pic.close_timer);

    pixels_free();

    /* The tag is kept: an answer still in flight for this screen must not
     * match the next one. */
    uint8_t tag = pic.tag;

    memset(&pic, 0, sizeof(pic));
    pic.tag = tag;
}

const struct item_screen_dsc_s item_screen_image = {
    ItemType::type_image, build, refresh, destroy, false};

void item_image_apply_result(struct openhab_result_s *res)
{
    if (pic.view == NULL || res->slot != pic.tag)
        return;

    pic.in_flight = false;

    if (res->ok == false || res->payload == NULL || res->width == 0 || res->height == 0)
    {
        /* A picture already up stays up; a failed refresh is not news. */
        if (pic.dsc.data == NULL)
            status_set("No picture");
    }
    else
    {
        pixels_free();

        pic.dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
        pic.dsc.header.cf = LV_COLOR_FORMAT_RGB565;
        pic.dsc.header.w = res->width;
        pic.dsc.header.h = res->height;
        pic.dsc.header.stride = (uint32_t)res->width * 2;
        pic.dsc.data_size = (uint32_t)res->payload_len;
        pic.dsc.data = (const uint8_t *)res->payload;

        /* Ours now; release() leaves a NULL payload alone. */
        res->payload = NULL;

        /* As large as the body takes it, keeping the aspect: LVGL's scale is
         * in 1/256ths, so 512 is twice. */
        int32_t bw = lv_obj_get_content_width(pic.view->body);
        int32_t bh = lv_obj_get_content_height(pic.view->body);
        int32_t sw = bw * 256 / res->width;
        int32_t sh = bh * 256 / res->height;

        lv_image_set_src(pic.image, &pic.dsc);
        lv_image_set_pivot(pic.image, res->width / 2, res->height / 2);
        lv_image_set_scale(pic.image, (uint32_t)((sw < sh) ? sw : sh));
        lv_obj_center(pic.image);

        status_set(NULL);
    }

    if (pic.again == true)
        request();
}

void item_image_popup(const char *website, const char *item_name, const char *label,
                      uint32_t close_after_ms)
{
    /* Already looking at it: just look again. */
    if (pic.view != NULL && pic.view->item == &popup_item)
    {
        request();
        return;
    }

    char link[STR_LINK_LEN];

    if ((size_t)snprintf(link, sizeof(link), "%s/rest/items/%s", website, item_name)
        >= sizeof(link))
        return;

    popup_item.cleanItem();
    popup_item.setType(ItemType::type_image);
    popup_item.setLink(link);
    popup_item.setLabel((label != NULL && label[0] != '\0') ? label : item_name);

    item_screen_open(&popup_item, OPENHAB_CLIENT_SLOT_NONE);

    if (close_after_ms > 0 && pic.view != NULL)
        pic.close_timer = lv_timer_create(close_timer_cb, close_after_ms, NULL);
}
