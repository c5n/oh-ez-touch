#ifndef OPENHAB_CONNECTOR_H
#define OPENHAB_CONNECTOR_H

#include "Arduino.h"
#include <ArduinoJson.h>

#define ITEM_SELECTION_COUNT_MAX 10
#define ITEM_SELECTION_COMMAND_LEN_MAX 20
#define ITEM_SELECTION_LABEL_LEN_MAX 20

#define STR_LABEL_LEN 32
#define STR_ICON_NAME_LEN 32
#define STR_STATE_TEXT_LEN 32
#define STR_PATTERN_LEN 16
#define STR_LINK_LEN 128
#define STR_TITLE_LEN 32

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
    char label[STR_LABEL_LEN];
    char icon_name[STR_ICON_NAME_LEN];
    enum ItemType type = type_unknown;
    char state_text[STR_STATE_TEXT_LEN];
    char pattern[STR_PATTERN_LEN];
    float min_val = 0.0f;
    float max_val = 0.0f;
    float step_val = 1.0f;
    char selection_command[ITEM_SELECTION_COUNT_MAX][ITEM_SELECTION_COMMAND_LEN_MAX] = {};
    char selection_label[ITEM_SELECTION_COUNT_MAX][ITEM_SELECTION_LABEL_LEN_MAX] = {};
    size_t mapping_count = 0;
    char link[STR_LINK_LEN];

public:
    int update(const char* link);
    int publish(const char* link);

    void cleanItem()
    {
        label[0] = 0;
        icon_name[0] = 0;
        type = type_unknown;
        state_text[0] = 0;
        link[0] = 0;
    }

    size_t getIcon(const char* website, const char* name, const char* state, unsigned char *buffer, size_t buffer_size);

    void setLabel(const char* newlabel) { strncpy(label, newlabel, sizeof(label)); }
    const char* getLabel() { return label; }

    void setIconName(const char* newiconname) { strncpy(icon_name, newiconname, sizeof(icon_name)); }
    const char* getIconName() { return icon_name; }

    void setLink(const char * newlink) { strncpy(link, newlink, sizeof(link)); }
    const char * getLink() { return link; }

    enum ItemType getType() { return type; }
    void setType(enum ItemType newtype) { type = newtype; }

    const char* getStateText() { return state_text; }
    void setStateText(const char* newtext) { strncpy(state_text, newtext, sizeof(state_text)); }
    float getStateNumber() { return strtof(state_text, NULL); }
    void setStateNumber(float newnumber) { snprintf(state_text, sizeof(state_text), "%e", newnumber); }

    const char* getNumberPattern() { return pattern; }
    void setNumberPattern(const char* newpattern) { strncpy(pattern, newpattern, sizeof(pattern)); }

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
    char title[STR_TITLE_LEN];
    Item item_array[6];

public:
    int openlink(const char* url);

    const char* getPageName() { return title; }
    Item* getItem(size_t index) { return &item_array[index]; }
};

#endif
