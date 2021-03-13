#ifndef OPENHAB_CONNECTOR_H
#define OPENHAB_CONNECTOR_H

#include "Arduino.h"
#include <ArduinoJson.h>

#define ITEM_SELECTION_COUNT_MAX 10
#define ITEM_SELECTION_COMMAND_LEN_MAX 20
#define ITEM_SELECTION_LABEL_LEN_MAX 20

enum WidgetType
{
    item_text,
    item_switch,
    item_setpoint,
    item_slider,
    item_selection,
    item_colorpicker,
    linked_page,
    parent_page,
    unknown
};

enum ItemType
{
    type_parent_link,
    type_link,
    type_number,
    type_string,
    type_setpoint,
    type_slider,
    type_selection,
    type_colorpicker,
    type_switch,
    type_rollershutter,
    type_player,
    type_unknown
};

class Item
{
private:
    String label = {};
    String icon_name = {};
    enum ItemType type = type_unknown;
    String state_text = {};
    String pattern = {};
    float min_val = 0.0f;
    float max_val = 0.0f;
    float step_val = 1.0f;
    char selection_command[ITEM_SELECTION_COUNT_MAX][ITEM_SELECTION_COMMAND_LEN_MAX] = {};
    char selection_label[ITEM_SELECTION_COUNT_MAX][ITEM_SELECTION_LABEL_LEN_MAX] = {};
    size_t mapping_count = 0;
    String link = {};

public:
    int update(String link);
    int publish(String link);

    void cleanItem()
    {
        label = "";
        icon_name = "";
        type = type_unknown;
        state_text = "";
        link = "";
    }

    size_t getIcon(String website, String name, String state, unsigned char *buffer, size_t buffer_size);

    void setLabel(String newlabel) { label = newlabel; }
    String getLabel() { return label; }

    void setIconName(String newiconname) { icon_name = newiconname; }
    String getIconName() { return icon_name; }

    void setLink(String newlink) { link = newlink; }
    String getLink() { return link; }

    enum ItemType getType() { return type; }
    void setType(enum ItemType newtype) { type = newtype; }

    String getStateText() { return state_text; }
    void setStateText(String newtext) { state_text = newtext; }
    float getStateNumber() { return state_text.toFloat(); }
    void setStateNumber(float newnumber) { state_text = String(newnumber); }

    String getNumberPattern() { return pattern; }
    void setNumberPattern(String newpattern) { pattern = newpattern; }

    float getMinVal() { return min_val; }
    void setMinVal(float newval) { min_val = newval; }

    float getMaxVal() { return max_val; }
    void setMaxVal(float newval) { max_val = newval; }

    float getStep() { return step_val; }
    void setStep(float newval) { step_val = newval; }

    char *getSelectionCommand(size_t index) { return selection_command[index]; }
    void setSelectionCommand(size_t index, const char *command)
    {
        strncpy(selection_command[index], command, sizeof(selection_command[index]));
    }
    void setSelectionLabel(size_t index, const char *label)
    {
        strncpy(selection_label[index], label, sizeof(selection_label[index]));
    }
    char *getSelectionLabel(size_t index) { return selection_label[index]; }
    size_t getSelectionCount() { return mapping_count; }
    void setSelectionCount(size_t new_count) { mapping_count = new_count; }
};

class Sitemap
{
private:
    StaticJsonDocument<12000> doc;
    String title;
    Item item_array[6];

public:
    int openlink(String url);

    String getPageName() { return title; }
    Item* getItem(size_t index) { return &item_array[index]; }
};

#endif
