#include "openhab_connector.hpp"

#include "debug.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* JsonVariant::as<const char *>() yields NULL for a missing or non-string
 * value; the comparisons below want an empty string in that case. */
static inline const char *json_str(JsonVariant value)
{
    const char *str = value.as<const char *>();
    return (str != NULL) ? str : "";
}

/* A widget's label without the "[state]" openHAB appends to it, and without
 * the run of spaces in front of that.
 *
 * "Kitchen [21.5 degC]" is one string in the sitemap; the panel draws the
 * name and the reading in two different places, so the bracket and everything
 * after it goes.
 *
 * Two things this had wrong. It stepped back one character before looking at
 * anything, so a label that was empty or that began with '[' formed
 * `buffer - 1` -- undefined behaviour, and only harmless by luck. And it
 * passed a plain char to isspace(), which is undefined for any byte with the
 * top bit set: every label with an umlaut in it reached that call with a
 * negative value. glibc happens to tolerate that -- its table is offset for
 * it -- so the host tests cannot demonstrate the second one; newlib, which is
 * what the panel runs, indexes its table directly.
 *
 * Returns `out`, always terminated, so the caller can pass it straight on.
 */
static const char *label_trim(const char *label, char *out, size_t out_size)
{
    const char *end = strchr(label, '[');
    size_t      len = (end != NULL) ? (size_t)(end - label) : strlen(label);

    /* Trailing space, tested as unsigned: isspace() takes an int that must be
     * representable as unsigned char, and a UTF-8 continuation byte in a
     * plain char is not. */
    while (len > 0 && isspace((unsigned char)label[len - 1]))
        len--;

    if (len >= out_size)
        len = out_size - 1;

    memcpy(out, label, len);
    out[len] = '\0';

    return out;
}

/* openHAB offers the same label/command list either as the widget's "mappings"
 * or as the item's "commandOptions"; both are read into the item's fixed
 * selection arrays, so the count has to be clamped to what those hold. */
static void parse_selection(Item *item, JsonVariant array_value)
{
    JsonArray map_array = array_value.as<JsonArray>();
    size_t count = map_array.size();

    if (count > ITEM_SELECTION_COUNT_MAX)
        count = ITEM_SELECTION_COUNT_MAX;

    for (size_t i = 0; i < count; ++i)
    {
        JsonVariant map_elem = map_array[i];
        item->setSelectionLabel(i, json_str(map_elem["label"]));
        item->setSelectionCommand(i, json_str(map_elem["command"]));
    }

    item->setSelectionCount(count);
}

bool Item::stateUrl(char *out, size_t out_size) const
{
    /* Without a link there is no item endpoint to append "/state" to, and the
     * URL that would come out of it -- "/state" -- resolves to something else
     * entirely. Link and group widgets often carry only a page link. */
    if (link[0] == '\0')
        return false;

    int len = snprintf(out, out_size, "%s/state", link);

    return (len > 0 && (size_t)len < out_size);
}

bool Item::iconUrl(const char *website, char *out, size_t out_size) const
{
    /* No icon name is not a failure to report anywhere -- plenty of widgets
     * have none. It just means there is nothing to fetch, and asking openHAB
     * for "/icon/?state=..." would be a 404 per poll. */
    if (icon_name[0] == '\0')
        return false;

    int len = snprintf(out, out_size, "%s/icon/%s?state=%s&format=png",
                       website, icon_name, state_text);

    return (len > 0 && (size_t)len < out_size);
}

/* Read one non-negative integer, and say where it stopped.
 *
 * strtol() alone is not enough here: it reports "nothing parsed" only through
 * endptr, and the caller has to know that before it can step over a separator
 * that may not be there. */
static bool parse_uint_field(const char **cursor, long *out)
{
    char *end;
    long  value = strtol(*cursor, &end, 10);

    if (end == *cursor)
        return false;

    *cursor = end;
    *out = value;

    return true;
}

bool Item::getStateHsv(uint16_t *h, uint8_t *s, uint8_t *v) const
{
    const char *cursor = state_text;
    long        parsed[3] = {0, 0, 0};

    /* Black, so a caller that ignores the return value still paints something
     * defined rather than whatever was on its stack. */
    *h = 0;
    *s = 0;
    *v = 0;

    for (size_t i = 0; i < 3; i++)
    {
        if (parse_uint_field(&cursor, &parsed[i]) == false)
            return false;

        /* Step over the separator, but only if there is one. Skipping it
         * blind is what read past the end of a short state. */
        if (i < 2)
        {
            if (*cursor != ',')
                return false;

            cursor++;
        }
    }

    /* Clamped rather than rejected: a hue of 400 is a server being loose with
     * a value that still means something, and lv_color_hsv_to_rgb() takes
     * these as a uint16 and two uint8 that it does not range check. */
    if (parsed[0] < 0)
        parsed[0] = 0;
    if (parsed[0] > 359)
        parsed[0] = 359;

    for (size_t i = 1; i < 3; i++)
    {
        if (parsed[i] < 0)
            parsed[i] = 0;
        if (parsed[i] > 100)
            parsed[i] = 100;
    }

    *h = (uint16_t)parsed[0];
    *s = (uint8_t)parsed[1];
    *v = (uint8_t)parsed[2];

    return true;
}

int Item::applyState(const char *text, size_t len)
{
    char remote_state[STR_STATE_TEXT_LEN];

    /* Copied out rather than used in place: the caller's buffer is a network
     * payload and carries no terminator, and the numeric branch below has to
     * rewrite the value anyway. Truncating to the field width is what
     * HTTPClient::getString() plus strlcpy() did. */
    if (len >= sizeof(remote_state))
        len = sizeof(remote_state) - 1;

    memcpy(remote_state, text, len);
    remote_state[len] = '\0';

    if (   type == ItemType::type_number
        || type == ItemType::type_setpoint
        || type == ItemType::type_slider)
    {
        /* Strip the unit openHAB appends ("21.5 degC"), and re-print with the
         * same format setStateNumber() uses so that the comparison below sees
         * identical text for an unchanged value. Going via a local keeps the
         * source and destination of snprintf() apart. */
        float remote_value = strtof(remote_state, NULL);
        snprintf(remote_state, sizeof(remote_state), "%f", remote_value);
    }

    if (strcmp(state_text, remote_state) == 0)
        return 0;

    strlcpy(state_text, remote_state, sizeof(state_text));

#if CONFIG_OHEZ_DEBUG_OPENHAB_CONNECTOR
    printf("  update statetext to \"%s\"\r\n", state_text);
#endif

    return 1;
}

/* Turn a sitemap page into the title and the item array.
 *
 * `payload` need not be terminated and is not written to, but it has to stay
 * alive for the duration of the call: ArduinoJson parses in place and the
 * document below holds pointers into it. It does not have to survive the
 * return -- every field extracted here goes through one of the strlcpy()
 * setters in the header, so no Item ends up pointing into the page.
 *
 * Errors are reported here without the URL, which this does not know; the
 * caller adds it, the way openhab_http.cpp and its callers already divide it
 * up. */
int Sitemap::parse(const char *payload, size_t payload_len)
{
    int retval = 0;
    /* ArduinoJson 7 documents size themselves, so the former fixed 12000 byte
     * capacity is gone; the parser now grows the pool to fit the page. */
    JsonDocument doc;
    bool payload_ok = true;

#if CONFIG_OHEZ_DEBUG_OPENHAB_CONNECTOR
    printf("Sitemap::parse: %u byte payload:\r\n%.*s\r\n",
           (unsigned)payload_len, (int)payload_len, payload);
#endif

    // Parse JSON object
    /* The length is passed explicitly: the network payload is not terminated,
     * and the char * overload parses in place without copying. */
    DeserializationError error = deserializeJson(doc, payload, payload_len,
                                                 DeserializationOption::NestingLimit(15));

    /* Both of these used to "return false", which is 0 and therefore the
     * success code of this function, so the caller kept the stale page and the
     * HTTP client was never closed. */
    if (error)
    {
        printf("Sitemap::parse: deserializeJson() failed: %s\r\n", error.c_str());
        payload_ok = false;
    }
    /* containsKey() is deprecated in ArduinoJson 7. The value is indexed as an
     * object right below, so test for exactly that. */
    else if (doc["error"].is<JsonObject>())
    {
        printf("Sitemap::parse: json error message: %s\r\n", json_str(doc["error"]["message"]));
        payload_ok = false;
    }

    if (payload_ok == true)
    {
#if CONFIG_OHEZ_DEBUG_OPENHAB_CONNECTOR
        /* ArduinoJson 7 dropped memoryUsage() -- it always returns zero. The
         * serialized size is the closest figure that still says something
         * about how big the page was. */
        printf("Doc serialized size: %u\r\n", (unsigned)measureJson(doc));
#endif

        if (doc["title"])
            strlcpy(title, json_str(doc["title"]), sizeof(title));
        else
            strlcpy(title, "no title", sizeof(title));

#if CONFIG_OHEZ_DEBUG_OPENHAB_CONNECTOR
        printf("  title=\"%s\"\r\n", title);
#endif

        // Cleanup Items
        for (size_t i = 0; i < ITEM_COUNT_MAX; ++i)
        {
            item_array[i].cleanItem();
        }

        item_count = 0;

        // Update Items

        // if current location is a child of the sitemap then set first item
        if (doc["parent"]["link"])
        {
            item_array[item_count].setType(ItemType::type_parent_link);
            item_array[item_count].setPageLink(doc["parent"]["link"]);
#if CONFIG_OHEZ_DEBUG_OPENHAB_CONNECTOR
            printf("  idx: %u type=parent_link   link=\"%s\"\r\n", (unsigned)item_count,
                   item_array[item_count].getPageLink());
#endif
            item_count++;
        }

        JsonArray widget_array = doc["widgets"].as<JsonArray>();

        /* Scratch for label_trim(), reused across widgets: it is a whole
         * label wide and there is only ever one in flight. */
        char label_buffer[STR_LABEL_LEN];

        for (size_t widget_index = 0; widget_index < widget_array.size(); widget_index++)
        {
            JsonVariant widget = widget_array[widget_index];
            Item* item = &item_array[item_count];

            /* Every lookup walks the object, so the two nodes that are read
             * over and over below are resolved once here. */
            JsonVariant json_item = widget["item"];
            const char *item_type = json_str(json_item["type"]);

            // Label
            if (widget["label"])
                item->setLabel(label_trim(json_str(widget["label"]), label_buffer,
                                          sizeof(label_buffer)));
            else
                item->setLabel("NO LABEL");

#if CONFIG_OHEZ_DEBUG_OPENHAB_CONNECTOR
            printf("  idx: %u label=\"%s\"", (unsigned)item_count, item->getLabel());
#endif

            // Icon
            if (widget["icon"])
            {
                item->setIconName(widget["icon"]);
#if CONFIG_OHEZ_DEBUG_OPENHAB_CONNECTOR
                printf("  icon=\"%s\"", item->getIconName());
#endif
            }

            // Type
            item->setType(ItemType::type_unknown);

            if (widget["type"] == "Text")
            {
                if (widget["linkedPage"]["link"])
                    item->setType(ItemType::type_link);
                // >= <= needed to distinct from strings
                else if (   item_type[0] != '\0'
                         && strcmp(item_type, "Number") >= 0
                         && strcmp(item_type, "Number:Z") <= 0)
                    item->setType(ItemType::type_number);
                else
                    item->setType(ItemType::type_string);
            }
            else if (widget["type"] == "Group")
            {
                item->setType(ItemType::type_group);
            }
            else if (widget["type"] == "Switch")
            {
                if (strcmp(item_type, "Switch") == 0)
                    item->setType(ItemType::type_switch);
                else if (strcmp(item_type, "Rollershutter") == 0)
                    item->setType(ItemType::type_rollershutter);
                else if (strcmp(item_type, "Player") == 0)
                    item->setType(ItemType::type_player);
                else if (strcmp(item_type, "Group") == 0)
                {
                    const char *group_type = json_str(json_item["groupType"]);

                    if (strcmp(group_type, "Switch") == 0)
                        item->setType(ItemType::type_switch);
                    else if (strcmp(group_type, "Rollershutter") == 0)
                        item->setType(ItemType::type_rollershutter);
                }
            }
            else if (widget["type"] == "Setpoint")
                item->setType(ItemType::type_setpoint);
            else if (widget["type"] == "Slider")
                item->setType(ItemType::type_slider);
            else if (widget["type"] == "Selection")
                item->setType(ItemType::type_selection);
            else if (widget["type"] == "Colorpicker")
                item->setType(ItemType::type_colorpicker);

#if CONFIG_OHEZ_DEBUG_OPENHAB_CONNECTOR
            printf("  type=%u", item->getType());
#endif

            // MinVal
            if (widget["minValue"])
                item->setMinVal(widget["minValue"].as<float>());
            else if (json_item["stateDescription"]["minimum"])
                item->setMinVal(json_item["stateDescription"]["minimum"].as<float>());
            else
                item->setMinVal(0.0f);
#if CONFIG_OHEZ_DEBUG_OPENHAB_CONNECTOR
            printf("  minVal=%.2f", item->getMinVal());
#endif

            // MaxVal
            if (widget["maxValue"])
                item->setMaxVal(widget["maxValue"].as<float>());
            else if (json_item["stateDescription"]["maximum"])
                item->setMaxVal(json_item["stateDescription"]["maximum"].as<float>());
            else
                item->setMaxVal(100.0f);
#if CONFIG_OHEZ_DEBUG_OPENHAB_CONNECTOR
            printf("  maxVal=%.2f", item->getMaxVal());
#endif

            // Step
            if (widget["step"])
                item->setStep(widget["step"].as<float>());
            else if (json_item["stateDescription"]["step"])
                item->setStep(json_item["stateDescription"]["step"].as<float>());
            else
                item->setStep(1.0f);
#if CONFIG_OHEZ_DEBUG_OPENHAB_CONNECTOR
            printf("  step=%.2f", item->getStep());
#endif

            // Number format string
            if (json_item["stateDescription"]["pattern"])
                item->setNumberPattern(json_item["stateDescription"]["pattern"]);
            else
                item->setNumberPattern("%d");
#if CONFIG_OHEZ_DEBUG_OPENHAB_CONNECTOR
            printf("  numpat=\"%s\"", item->getNumberPattern());
#endif

            // State
            if (json_item["state"])
            {
                if (   item->getType() == ItemType::type_number
                    || item->getType() == ItemType::type_setpoint
                    || item->getType() == ItemType::type_slider)
                {
                    // convert number to get rid of unit
                    item->setStateNumber(strtof(json_str(json_item["state"]), NULL));
#if CONFIG_OHEZ_DEBUG_OPENHAB_CONNECTOR
                    printf("  num-statetext=\"%s\"", item->getStateText());
#endif
                }
                else
                {
                    item->setStateText(json_item["state"]);
#if CONFIG_OHEZ_DEBUG_OPENHAB_CONNECTOR
                    printf("  statetext=\"%s\"", item->getStateText());
#endif
                }
            }

            // Transformed State
            if (json_item["transformedState"])
            {
                item->setTransformedStateText(json_item["transformedState"]);
            }

            // Links
            if (widget["linkedPage"]["link"])
            {
                item->setPageLink(widget["linkedPage"]["link"]);
#if CONFIG_OHEZ_DEBUG_OPENHAB_CONNECTOR
                printf("  page_link=\"%s\"", item->getPageLink());
#endif
            }

            if (json_item["link"])
            {
                item->setLink(json_item["link"]);
#if CONFIG_OHEZ_DEBUG_OPENHAB_CONNECTOR
                printf("  link=\"%s\"", item->getLink());
#endif
            }

            // Mappings
            if (widget["mappings"])
                parse_selection(item, widget["mappings"]);
            else if (json_item["commandDescription"]["commandOptions"])
                parse_selection(item, json_item["commandDescription"]["commandOptions"]);

#if CONFIG_OHEZ_DEBUG_OPENHAB_CONNECTOR
            printf("\r\n");
#endif

            item_count++;

            if (item_count >= ITEM_COUNT_MAX)
                break;
        }
    }
    else
    {
        retval = -1;
    }

    return retval;
}
