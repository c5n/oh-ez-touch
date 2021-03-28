#ifndef UI_INFOLABEL_HPP
#define UI_INFOLABEL_HPP

#include "Arduino.h"
#include <lvgl.h>

#ifndef DEBUG_UI_INFOLABEL
#define DEBUG_UI_INFOLABEL 0
#endif

#define STR_INFOLABEL_TEMP_BUFFER_LEN (140 + 1)

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

    void create(enum infolabel_type_e type, const char* topic, const char* text, uint16_t timeout)
    {
        if (il == NULL)
        {
#if DEBUG_UI_INFOLABEL
            printf("Infolabel::create: Topic: %s   Text: %s\r\n", topic, text);
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

        char buffer[STR_INFOLABEL_TEMP_BUFFER_LEN];
        snprintf(buffer, sizeof(buffer), "%s\n%s", topic, text);
        lv_mbox_set_text(il, buffer);

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
            Serial.println("Infolabel::destroy: Destroying label");
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
            Serial.println("Infolabel::loop: infolabel timeout reached");
#endif
            destroy();
            timeout_timestamp = 0;
        }
    }
};

#endif