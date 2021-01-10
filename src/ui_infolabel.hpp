#ifndef UI_INFOLABEL_HPP
#define UI_INFOLABEL_HPP

#include <lvgl.h>

#ifndef DEBUG_UI_INFOLABEL
#define DEBUG_UI_INFOLABEL 0
#endif

class Infolabel
{
private:
    lv_obj_t *il = NULL;
    lv_style_t label_style;
    unsigned long timeout_timestamp;

public:
    enum infolabel_type_e
    {
        INFO,
        WARNING,
        ERROR
    };

    void create(enum infolabel_type_e type, String text, uint16_t timeout)
    {
        if (il == NULL)
        {
#if DEBUG_UI_INFOLABEL
            debug_printf("Infolabel::create: Creating new label Text: %s", text.c_str());
#endif
            lv_style_copy(&label_style, &lv_style_plain);
            label_style.body.border.width = 4;

            if (type == INFO)
                label_style.body.main_color = LV_COLOR_SILVER;
            else if (type == WARNING)
                label_style.body.main_color = LV_COLOR_YELLOW;
            else if (type == ERROR)
                label_style.body.main_color = LV_COLOR_RED;

            label_style.body.grad_color = label_style.body.main_color;
            label_style.body.padding.top = LV_DPI / 10;
            label_style.body.padding.bottom = LV_DPI / 10;
            label_style.body.padding.left = LV_DPI / 10;
            label_style.body.padding.right = LV_DPI / 10;
            label_style.text.font = &custom_font_roboto_22;
            il = lv_mbox_create(lv_scr_act(), NULL);
            lv_obj_set_style(il, &label_style);
            lv_obj_set_width(il, lv_disp_get_hor_res(NULL) * 9 / 10);
        }

        lv_mbox_set_text(il, text.c_str());

        lv_obj_align(il, NULL, LV_ALIGN_CENTER, 0, 0);

        if (timeout > 0)
            timeout_timestamp = millis() + timeout * 1000;
        else
            timeout_timestamp = 0;
    }

    void destroy(void)
    {
        if (il != NULL)
        {
#if DEBUG_UI_INFOLABEL
            debug_printf("Infolabel::destroy: Destroying label\n");
#endif
            lv_obj_del(il);
            il = NULL;
            timeout_timestamp = 0;
        }
    }

    void loop(void)
    {
        if (timeout_timestamp > 0 && millis() >= timeout_timestamp)
        {
#if DEBUG_UI_INFOLABEL
            debug_printf("Infolabel::loop: infolabel timeout reached\n");
#endif
            destroy();
            timeout_timestamp = 0;
        }
    }
};

#endif