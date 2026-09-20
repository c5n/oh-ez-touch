#ifndef UI_INPUT_H
#define UI_INPUT_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Take swipes away from one input device.
 *
 * Call it once for every pointer indev the build creates -- the touch panel,
 * the simulator's mouse, and the test interface's synthetic pointer -- right
 * after lv_indev_create(). It is the whole of the panel's swipe policy; see
 * ui_input.c for what that policy is and why.
 */
void ui_input_disable_swipes(lv_indev_t *indev);

#ifdef __cplusplus
}
#endif

#endif /* UI_INPUT_H */
