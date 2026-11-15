/* See ui_icon_night.h. */

#include "ui_icon_night.h"

#include "icons/icon_night.h"

/* The decoder descriptor is opaque in the public API; a custom decoder needs
 * its source and its decoded line, so this is the header LVGL provides for
 * one. */
#include "src/image/lv_image_decoder_private.h"

/* Claim an image only when it asks for the night look and is indexed: those
 * are the images the binary decoder reads through a line buffer. */
static lv_result_t night_info(lv_image_decoder_t *decoder, lv_image_decoder_dsc_t *dsc,
                              lv_image_header_t *header)
{
    if (dsc->src_type != LV_IMAGE_SRC_VARIABLE)
        return LV_RESULT_INVALID;

    const lv_image_dsc_t *image = (const lv_image_dsc_t *)dsc->src;

    if ((image->header.flags & UI_ICON_NIGHT_FLAG) == 0
        || LV_COLOR_FORMAT_IS_INDEXED(image->header.cf) == false)
        return LV_RESULT_INVALID;

    return lv_bin_decoder_info(decoder, dsc, header);
}

static lv_result_t night_get_area(lv_image_decoder_t *decoder, lv_image_decoder_dsc_t *dsc,
                                  const lv_area_t *full_area, lv_area_t *decoded_area)
{
    lv_result_t res = lv_bin_decoder_get_area(decoder, dsc, full_area, decoded_area);

    if (res != LV_RESULT_OK || dsc->decoded == NULL)
        return res;

    const lv_draw_buf_t *line = dsc->decoded;

    if (line->header.cf == LV_COLOR_FORMAT_ARGB8888)
        icon_night_invert((uint8_t *)line->data, line->header.w);

    return res;
}

void ui_icon_night_setup(void)
{
    lv_image_decoder_t *decoder = lv_image_decoder_create();

    if (decoder == NULL)
        return;

    /* lv_image_decoder_create() puts the new decoder at the head of the list,
     * so it is asked before the binary decoder. */
    lv_image_decoder_set_info_cb(decoder, night_info);
    lv_image_decoder_set_open_cb(decoder, lv_bin_decoder_open);
    lv_image_decoder_set_get_area_cb(decoder, night_get_area);
    lv_image_decoder_set_close_cb(decoder, lv_bin_decoder_close);
    decoder->name = "ohez-night";
}
