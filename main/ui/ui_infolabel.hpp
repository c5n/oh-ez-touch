#ifndef UI_INFOLABEL_HPP
#define UI_INFOLABEL_HPP

#include "debug.h"
#include "port/port_sys.h"
#include "ui_style.hpp"

#include <stdint.h>
#include <stdio.h>
#include <lvgl.h>

/* A transient banner shown over the UI, e.g. for WLAN state changes.
 *
 * Under LVGL v7 this was an lv_msgbox used purely as a styled text panel. The
 * v9 msgbox is a full dialog with a header, footer and button area, which does
 * not fit -- so this is now a plain object with one label, which is all the
 * widget ever was. It lives on the top layer so it floats above the page
 * without being deleted when the page is rebuilt. */
class Infolabel
{
private:
    lv_obj_t *il = NULL;
    lv_obj_t *label = NULL;
    uint64_t timeout_timestamp = 0;

public:
    enum infolabel_type_e
    {
        INFO,
        WARNING,
        ERROR
    };

private:
    /* The severity of the banner on screen. Kept because it is the one thing
     * about a live banner that is not readable back off the label. */
    enum infolabel_type_e kind = INFO;

public:
    /* Whether a banner is up and what it says, for the simulator's control
     * interface. The text is read back out of the label rather than stored a
     * second time here -- which is also why it arrives as the "topic\ntext"
     * that create() formatted, and why one caller's long sitemap URL survives
     * intact. */
    bool isUp(void) const { return il != NULL; }

    enum infolabel_type_e getKind(void) const { return kind; }

    const char *getText(void) const
    {
        return (label != NULL) ? lv_label_get_text(label) : NULL;
    }

    void create(enum infolabel_type_e type, const char* topic, const char* text, uint16_t timeout)
    {
        if (il == NULL)
        {
#if CONFIG_OHEZ_DEBUG_UI_INFOLABEL
            printf("Infolabel::create: Topic: %s   Text: %s\r\n", topic, text);
#endif
            /* The panel used to build a private style here. It now shares the
             * theme's, which is what lets a theme change repaint a banner that
             * is already on screen -- lv_obj_report_style_change() reaches the
             * top layer, but it can only refresh a style someone else owns. */
            il = lv_obj_create(lv_layer_top());
            lv_obj_remove_flag(il, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_add_style(il, &ui_style_info, LV_PART_MAIN);
            lv_obj_set_width(il, lv_display_get_horizontal_resolution(NULL) * 9 / 10);
            lv_obj_set_height(il, LV_SIZE_CONTENT);

            label = lv_label_create(il);
            lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
            lv_obj_set_width(label, lv_pct(100));
            lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
        }

        /* Outside the guard above, unlike the style setup that used to live
         * there: main.cpp calls create() again on a live banner to report the
         * next WLAN state, and a severity set only on the first call meant an
         * error kept the colour of the info that preceded it. */
        lv_obj_remove_style(il, &ui_style_info_warning, LV_PART_MAIN);
        lv_obj_remove_style(il, &ui_style_info_error, LV_PART_MAIN);

        if (type == WARNING)
            lv_obj_add_style(il, &ui_style_info_warning, LV_PART_MAIN);
        else if (type == ERROR)
            lv_obj_add_style(il, &ui_style_info_error, LV_PART_MAIN);

        /* Formatted straight into the label rather than through a fixed
         * buffer of our own. One caller passes a sitemap URL as the text, which
         * is longer than any buffer worth putting on this stack -- so the copy
         * truncated, deliberately, and -Wformat-truncation was right to say so.
         * LVGL sizes its own. */
        lv_label_set_text_fmt(label, "%s\n%s", topic, text);

        kind = type;

        lv_obj_align(il, LV_ALIGN_CENTER, 0, 0);

        if (timeout > 0)
            timeout_timestamp = port_millis() + timeout * 1000;
        else
            timeout_timestamp = 0;
    }

    void destroy(void)
    {
        if (il != NULL)
        {
#if CONFIG_OHEZ_DEBUG_UI_INFOLABEL
            printf("Infolabel::destroy: Destroying label\r\n");
#endif
            lv_obj_delete(il);
            il = NULL;
            label = NULL;
            timeout_timestamp = 0;
        }
    }

    void loop(void)
    {
        if (timeout_timestamp > 0 && port_millis() >= timeout_timestamp)
        {
#if CONFIG_OHEZ_DEBUG_UI_INFOLABEL
            printf("Infolabel::loop: infolabel timeout reached\r\n");
#endif
            destroy();
            timeout_timestamp = 0;
        }
    }
};

/* The two banners this firmware has, declared where the class is so that the
 * one reader of both -- the simulator's control interface, which reports
 * whichever is up -- does not have to declare them itself.
 *
 * They are separate instances on purpose: main.cpp owns the WLAN and setup
 * messages, openhab_ui.cpp owns "this sitemap will not load", and either can
 * be on screen without the other. */
extern Infolabel infolabel;            /* main.cpp */
extern Infolabel openhab_ui_infolabel; /* openhab_ui.cpp */

#endif
