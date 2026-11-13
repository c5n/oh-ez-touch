/* LVGL's glyph buffers, drawn from the shared work buffer.
 *
 * LVGL draws a bitmap glyph through a buffer of its own: box_w x box_h rounded
 * up to 32 rows, A8, allocated from the heap at the start of every label draw
 * task and freed at its end. For the clock screen's digits that is 5.2 to
 * 5.7 KB in one piece, every second. When the heap's largest free block drops
 * below that -- on an ArduiTouch with every feature on it did, for a frame
 * during each BLE scan and for good after some days -- LVGL skips the glyph
 * with a warning, and the digits go missing one after another until only the
 * ":" is left.
 *
 * ui_glyph_buf_setup() installs allocators on LVGL's font draw buffer
 * handlers that take the work buffer (mem/work_buf.h) with
 * work_buf_try_claim() and give it back when LVGL frees the glyph buffer, at
 * the end of the same label draw. They never wait: when the openHAB client is
 * reading into the work buffer, a result is in it, or a read waits for it,
 * the glyph buffer comes from the heap as before.
 */

#ifndef UI_GLYPH_BUF_H
#define UI_GLYPH_BUF_H

#ifdef __cplusplus
extern "C" {
#endif

/** Install the allocators. Once, after lv_init() and before the first draw.
 *  Before work_buf_init() every glyph buffer still comes from the heap. */
void ui_glyph_buf_setup(void);

#ifdef __cplusplus
}
#endif

#endif /* UI_GLYPH_BUF_H */
