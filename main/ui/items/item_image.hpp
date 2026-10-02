/**
 * @file item_image.hpp
 *
 * The picture screen's two ways in that are not a tile: its answers from the
 * client task, and the doorbell. See item_image.cpp.
 */
#ifndef ITEM_IMAGE_HPP
#define ITEM_IMAGE_HPP

#include "openhab/openhab_client.hpp"

#include <stdint.h>

/* An OPENHAB_REQ_IMAGE result. Takes the pixels when they are for the screen
 * that is up, by setting res->payload to NULL; the caller releases the result
 * either way. */
void item_image_apply_result(struct openhab_result_s *res);

/* Show `item_name`'s picture from whatever page is on screen, and close again
 * after `close_after_ms` unless somebody touches it (0: stay). A second call
 * while it is up fetches the picture again. */
void item_image_popup(const char *website, const char *item_name, const char *label,
                      uint32_t close_after_ms);

#endif /* ITEM_IMAGE_HPP */
