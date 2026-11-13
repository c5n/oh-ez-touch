#include "ui_glyph_buf.h"

#include "lvgl.h"
#include "mem/work_buf.h"

/* The handler struct is opaque in the public API; this is the header LVGL
 * provides for code that installs its own allocators. */
#include "src/draw/lv_draw_buf_private.h"

static void *glyph_buf_malloc(size_t size_bytes, lv_color_format_t color_format)
{
    LV_UNUSED(color_format);

    /* The same padding LVGL's own allocator adds, so that align_pointer_cb,
     * which is still LVGL's, has room to move the start. */
    size_bytes += LV_DRAW_BUF_ALIGN - 1;

    if (size_bytes <= work_buf_size())
    {
        void *buf = work_buf_try_claim(WORK_BUF_GLYPH);

        if (buf != NULL)
            return buf;
    }

    return lv_malloc(size_bytes);
}

static void glyph_buf_free(void *buf)
{
    if (work_buf_is(buf))
    {
        work_buf_release(WORK_BUF_GLYPH);
        return;
    }

    lv_free(buf);
}

void ui_glyph_buf_setup(void)
{
    lv_draw_buf_handlers_t *handlers = lv_draw_buf_get_font_handlers();

    handlers->buf_malloc_cb = glyph_buf_malloc;
    handlers->buf_free_cb = glyph_buf_free;
}
