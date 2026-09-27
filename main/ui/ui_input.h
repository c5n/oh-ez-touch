#ifndef UI_INPUT_H
#define UI_INPUT_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Apply the panel's pointer policy to one input device.
 *
 * Call it once for every pointer indev the build creates -- the touch panel,
 * the simulator's mouse, and the test interface's synthetic pointer -- right
 * after lv_indev_create(). Together with every container being built
 * non-scrollable it is the whole of what a finger can mean here: swipe and
 * drag detection disabled outright, and a tap delivered at press-down rather
 * than at lift-off. See ui_input.c for what the policy is and why.
 */
void ui_input_pointer_policy(lv_indev_t *indev);

#ifdef __cplusplus
}
#endif

#endif /* UI_INPUT_H */
