/* The night look of the tile icons, applied as LVGL draws them.
 *
 * A tile icon is an LVGL I4 image: a 16-entry palette in front of the pixels.
 * The built-in ones are records in flash, shared by every tile that shows
 * them, so their palette cannot be changed, and a heap copy per icon is
 * memory this panel does not have. LVGL's binary decoder reads an I4 image
 * line by line into a one-line ARGB8888 buffer, though, so the change can be
 * made there: ui_icon_night_setup() installs a decoder in front of it that
 * lets the binary decoder do the work and then passes each line through
 * icon_night_invert() (icons/icon_night.h). That costs no memory and 32 pixel
 * operations per line drawn.
 *
 * Only an image whose descriptor carries UI_ICON_NIGHT_FLAG takes this path;
 * every other image goes to the binary decoder as before. The flag has an
 * effect on indexed images only. An ARGB8888 icon (a server icon whose
 * reduction to I4 failed) is drawn from its own pixels without a line
 * buffer, and keeps the day look.
 */

#ifndef UI_ICON_NIGHT_H
#define UI_ICON_NIGHT_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Set in lv_image_dsc_t.header.flags to draw that image with the night look. */
#define UI_ICON_NIGHT_FLAG LV_IMAGE_FLAGS_USER1

/** Install the decoder. Once, after lv_init(). */
void ui_icon_night_setup(void);

#ifdef __cplusplus
}
#endif

#endif /* UI_ICON_NIGHT_H */
