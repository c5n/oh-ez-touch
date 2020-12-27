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
    type_number,
    type_text,
    type_selection,
    type_unknown
};

class Item
{
private:
    enum ItemType type = type_unknown;
    String state_text = {};
    float state_number = 0.0f;
    int state_decimal = 0;
    String pattern = {};
    float min_val = 0.0f;
    float max_val = 0.0f;
    float step_val = 1.0f;
    char selection_command[ITEM_SELECTION_COUNT_MAX][ITEM_SELECTION_COMMAND_LEN_MAX] = {};
    char selection_label[ITEM_SELECTION_COUNT_MAX][ITEM_SELECTION_LABEL_LEN_MAX] = {};
    size_t mapping_count = 0;

public:
    int update(String link);
    int publish(String link);

    size_t getIcon(String website, String name, String state, unsigned char *buffer, size_t buffer_size);

    enum ItemType getType() { return type; }
    void setType(enum ItemType newtype) { type = newtype; }
    String getStateText() { return state_text; }
    void setStateText(String newtext) { state_text = newtext; }
    float getStateNumber() { return state_number; }
    void setStateNumber(float newnumber) { state_number = newnumber; }
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

    JsonVariant getWidget(size_t index)
    {
        JsonArray array = doc["widgets"].as<JsonArray>();
        return array.getElement(index);
    }

public:
    int openlink(String url);

    String getPageName()
    {
        return (doc["title"]);
    }

    bool hasParent()
    {
        if (doc["parent"]["link"])
            return true;

        return false;
    }

    String getParentLink()
    {
        return (doc["parent"]["link"]);
    }

    bool hasChild(size_t index)
    {
        JsonVariant v = getWidget(index);
        if (v["linkedPage"]["link"])
            return true;

        return false;
    }

    String getChildLink(size_t index)
    {
        JsonVariant v = getWidget(index);
        return v["linkedPage"]["link"];
    }

    size_t getWidgetcount()
    {
        JsonArray array = doc["widgets"].as<JsonArray>();
        return array.size();
    }

    String getWidgetIconName(size_t index)
    {
        JsonVariant v = getWidget(index);
        return v["icon"];
    }

    String getWidgetLabel(size_t index)
    {
        JsonVariant v = getWidget(index);

        if (v["linkedPage"])
            return v["linkedPage"]["title"];
        else if (v["item"])
            return v["item"]["label"];

        return "NO_LABEL";
    }

    WidgetType getWidgetType(size_t index)
    {
        JsonVariant v = getWidget(index);
        if (v["type"] == "Text")
            return WidgetType::item_text;
        else if (v["type"] == "Switch")
            return WidgetType::item_switch;
        else if (v["type"] == "Setpoint")
            return WidgetType::item_setpoint;
        else if (v["type"] == "Slider")
            return WidgetType::item_slider;
        else if (v["type"] == "Selection")
            return WidgetType::item_selection;
        else if (v["type"] == "Colorpicker")
            return WidgetType::item_colorpicker;

        return WidgetType::unknown;
    }

    ItemType getWidgetItemType(size_t index)
    {
        JsonVariant v = getWidget(index);
        if (v["item"]["type"].as<String>() >= "Number")
            return ItemType::type_number;

        return ItemType::type_text;
    }

    float getWidgetItemMinVal(size_t index)
    {
        JsonVariant v = getWidget(index);

        float retval = 0.0f;

        if (v["minValue"])
            retval = v["minValue"].as<float>();
        else if (v["item"]["stateDescription"]["minimum"])
            retval = v["item"]["stateDescription"]["minimum"].as<float>();

        return retval;
    }

    float getWidgetItemMaxVal(size_t index)
    {
        JsonVariant v = getWidget(index);

        float retval = 0.0f;

        if (v["maxValue"])
            retval = v["maxValue"].as<float>();
        else if (v["item"]["stateDescription"]["maximum"])
            retval = v["item"]["stateDescription"]["maximum"].as<float>();

        return retval;
    }

    float getWidgetItemStep(size_t index)
    {
        JsonVariant v = getWidget(index);

        float retval = 1.0f;

        if (v["step"])
            retval = v["step"].as<float>();
        else if (v["item"]["stateDescription"]["step"])
            retval = v["item"]["stateDescription"]["step"].as<float>();

        return retval;
    }

    String getWidgetItemState(size_t index)
    {
        JsonVariant v = getWidget(index);
        return v["item"]["state"];
    }

    String getWidgetItemPattern(size_t index)
    {
        JsonVariant v = getWidget(index);
        if (v["item"]["stateDescription"]["pattern"])
            return v["item"]["stateDescription"]["pattern"];
        else
            return "%d";
    }

    String getWidgetItemLink(size_t index)
    {
        JsonVariant v = getWidget(index);
        return v["item"]["link"];
    }

    size_t getSelectionCount(size_t index)
    {
        JsonVariant v = getWidget(index);
        if (v["mappings"] && v["mappings"].as<JsonArray>().size() > 0)
        {
            JsonArray array = v["mappings"].as<JsonArray>();
            return array.size();
        }
        else if (v["item"]["commandDescription"]["commandOptions"])
        {
            JsonArray array = v["item"]["commandDescription"]["commandOptions"].as<JsonArray>();
            return array.size();
        }
        return 0;
    }

    String getSelectionCommand(size_t wid_index, size_t sel_index)
    {
        JsonVariant v = getWidget(wid_index);
        JsonArray array;
        if (v["mappings"] && v["mappings"].as<JsonArray>().size() > 0)
        {
            array = v["mappings"].as<JsonArray>();
        }
        else if (v["item"]["commandDescription"]["commandOptions"])
        {
            array = v["item"]["commandDescription"]["commandOptions"].as<JsonArray>();
        }
        else
        {
            return "";
        }
        JsonVariant w = array.getElement(sel_index);
        return w["command"];
    }

    String getSelectionLabel(size_t wid_index, size_t sel_index)
    {
        JsonVariant v = getWidget(wid_index);
        JsonArray array;
        if (v["mappings"] && v["mappings"].as<JsonArray>().size() > 0)
        {
            array = v["mappings"].as<JsonArray>();
        }
        else if (v["item"]["commandDescription"]["commandOptions"])
        {
            array = v["item"]["commandDescription"]["commandOptions"].as<JsonArray>();
        }
        else
        {
            return "";
        }
        JsonVariant w = array.getElement(sel_index);
        return w["label"];
    }
};

#endif
