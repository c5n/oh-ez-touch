#include "sdkconfig.h"

#include "openhab_connector.hpp"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if !CONFIG_IDF_TARGET_LINUX
#include <HTTPClient.h>
#else
#include "sim/icon_fixture.hpp"
#include "sim/sitemap_fixture.hpp"
#endif

#ifndef DEBUG_OPENHAB_CONNECTOR
#define DEBUG_OPENHAB_CONNECTOR 0
#endif

#ifndef DEBUG_OPENHAB_CONNECTOR_PACKETDUMP
#define DEBUG_OPENHAB_CONNECTOR_PACKETDUMP 0
#endif

/* JsonVariant::as<const char *>() yields NULL for a missing or non-string
 * value; the comparisons below want an empty string in that case. */
static inline const char *json_str(JsonVariant value)
{
    const char *str = value.as<const char *>();
    return (str != NULL) ? str : "";
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

int Item::update(const char* link)
{
    int retval = 0;

    char url[STR_LINK_LEN];
    snprintf(url, sizeof(url), "%s/state", link);

#if DEBUG_OPENHAB_CONNECTOR
    printf("Item::update: Requesting URL: %s\r\n", url);
#endif
#if !CONFIG_IDF_TARGET_LINUX
    HTTPClient http;
    http.begin(url);

    int httpCode = http.GET();

    if (httpCode == HTTP_CODE_OK)
    {
        char remote_state[STR_STATE_TEXT_LEN];
        strlcpy(remote_state, http.getString().c_str(), sizeof(remote_state));

        // State
        if (   type == ItemType::type_number
            || type == ItemType::type_setpoint
            || type == ItemType::type_slider)
        {
            /* Strip the unit openHAB appends ("21.5 degC"), and re-print with
             * the same format setStateNumber() uses so that the comparison
             * below sees identical text for an unchanged value. Going via a
             * local keeps the source and destination of snprintf() apart. */
            float remote_value = strtof(remote_state, NULL);
            snprintf(remote_state, sizeof(remote_state), "%f", remote_value);
        }

        if (strcmp(state_text, remote_state) != 0)
        {
            retval = 1;
            strlcpy(state_text, remote_state, sizeof(state_text));
#if DEBUG_OPENHAB_CONNECTOR
            printf("  update statetext to \"%s\"\r\n", state_text);
#endif
        }
    }
    else
    {
        printf("Item::update: ERROR httpCode: %i URL: %s\r\n", httpCode, url);
        retval = -1;
    }

    http.end();
#endif
    return retval;
}

int Item::publish(const char* url)
{
    int retval = 0;

#if DEBUG_OPENHAB_CONNECTOR
    printf("Item::publish: Requesting URL: %s\r\n", url);
#endif

#if !CONFIG_IDF_TARGET_LINUX
    HTTPClient http;
    http.begin(url);
    http.addHeader("Content-Type", "text/plain");

#if DEBUG_OPENHAB_CONNECTOR
    printf("Item::publish: POST Message: %s\r\n", state_text);
#endif

    int httpCode = http.POST(state_text);

    if (httpCode != HTTP_CODE_OK)
    {
        printf("Item::publish ERROR httpCode: %i URL: %s\r\n", httpCode, url);
        retval = -1;
    }

    http.end();
#endif

    return retval;
}

size_t Item::getIcon(const char* website, const char* name, const char* state, unsigned char *buffer, size_t buffer_size)
{
    size_t icon_size = 0;
    char url[STR_LINK_LEN];

    snprintf(url, sizeof(url), "%s/icon/%s?state=%s&format=png", website, name, state);

#if DEBUG_OPENHAB_CONNECTOR
    printf("Item::getIcon: Requesting URL: %s\r\n", url);
#endif

#if CONFIG_IDF_TARGET_LINUX
    // No HTTP client in the simulator; use a compiled-in icon if there is one.
    size_t fixture_size = 0;
    const unsigned char *fixture_icon = sim_icon_fixture_get(name, state, &fixture_size);

    if (fixture_icon != NULL && fixture_size <= buffer_size)
    {
        memcpy(buffer, fixture_icon, fixture_size);
        icon_size = fixture_size;
    }
#else
    HTTPClient http;
    http.begin(url);

    int httpCode = http.GET();

    // file found at server
    if (httpCode == HTTP_CODE_OK)
    {
        // get lenght of document (is -1 when Server sends no Content-Length header)
        int len = http.getSize();

        // get tcp stream
        WiFiClient *stream = http.getStreamPtr();

        uint8_t *p_dst = buffer;
        size_t dst_avail = buffer_size;

#if DEBUG_OPENHAB_CONNECTOR
        printf("Item::getIcon: Stream size %u\r\n", stream->available());
#endif
        stream->setTimeout(2);

        // skip header which consists of length string terminated by CRLF
        while (http.connected() && stream->available() && stream->find("\r\n", 2) == false)
            ;

        // read all data from server
        while (http.connected() && (len > 0 || len == -1) && stream->available())
        {
            // get available data size
            size_t size = stream->available();

            if (size > dst_avail)
            {
                // insuifficent space available. Abort.
                icon_size = 0;
                break;
            }

            // size <= dst_avail here, so the whole chunk fits
            int c = stream->readBytes(p_dst, size);

#if DEBUG_OPENHAB_CONNECTOR
            debug_printf("get_icon: %d bytes read\r\n", c);
#if DEBUG_OPENHAB_CONNECTOR_PACKETDUMP
            for (int i = 0; i < c; i++)
                printf("%02x ", p_dst[i]);
            printf("\r\n");
#endif
#endif
            icon_size += c;

            if (len > 0)
                len -= c;

            dst_avail -= c;
            p_dst += c;

            delay(5);
        }
    }
    else // httpCode != HTTP_CODE_OK
    {
        printf("Item::getIcon: ERROR httpCode: %i URL: %s\r\n", httpCode, url);
    }

    http.end(); //Free the resources

    // remove footer which consists of CR LF "0" CR LF CR LF
    // ToDo: find a better solution
    if (icon_size > 7)
        icon_size -= 7;
#endif
    return icon_size;
}

int Sitemap::openlink(const char* url)
{
    int retval = 0;
    /* ArduinoJson 7 documents size themselves, so the former fixed 12000 byte
     * capacity is gone; the parser now grows the pool to fit the page. */
    JsonDocument doc;

#if DEBUG_OPENHAB_CONNECTOR
    printf("Sitemap::openlink: Requesting URL: %s\r\n", url);
#endif

#if CONFIG_IDF_TARGET_LINUX
    // No HTTP client in the simulator; serve a compiled-in page instead.
    const char *payload = sim_sitemap_fixture_get(url);
    bool payload_ok = (payload != NULL);

    if (payload_ok == false)
        printf("Sitemap::openlink: no fixture page for URL: %s\r\n", url);
#else
    HTTPClient http;
    http.begin(url);

    int httpCode = http.GET();
    bool payload_ok = (httpCode == HTTP_CODE_OK);
    String payload_string;

    if (payload_ok == true)
        payload_string = http.getString();
    else
        printf("Sitemap::openlink: ERROR httpCode: %i URL: %s\r\n", httpCode, url);

    const char *payload = payload_string.c_str();

#if DEBUG_OPENHAB_CONNECTOR
    printf("Sitemap::openlink: httpCode %d, payload:\r\n%s\r\n", httpCode, payload);
#endif
#endif

    if (payload_ok == true)
    {
        // Parse JSON object
        DeserializationError error = deserializeJson(doc, payload, DeserializationOption::NestingLimit(15));

        /* Both of these used to "return false", which is 0 and therefore the
         * success code of this function, so the caller kept the stale page and
         * the HTTP client was never closed. */
        if (error)
        {
            printf("Sitemap::openlink: deserializeJson() failed: %s\r\n", error.c_str());
            payload_ok = false;
        }
        /* containsKey() is deprecated in ArduinoJson 7. The value is indexed as
         * an object right below, so test for exactly that. */
        else if (doc["error"].is<JsonObject>())
        {
            printf("Sitemap::openlink: json error message: %s\r\n", json_str(doc["error"]["message"]));
            payload_ok = false;
        }
    }

    if (payload_ok == true)
    {
#if DEBUG_OPENHAB_CONNECTOR
        /* ArduinoJson 7 dropped memoryUsage() -- it always returns zero. The
         * serialized size is the closest figure that still says something
         * about how big the page was. */
        printf("Doc serialized size: %u\r\n", (unsigned)measureJson(doc));
#endif

        // Save current and last page urls
        strlcpy(last_url, current_url, sizeof(last_url));
        strlcpy(current_url, url, sizeof(current_url));

        if (doc["title"])
            strlcpy(title, json_str(doc["title"]), sizeof(title));
        else
            strlcpy(title, "no title", sizeof(title));

#if DEBUG_OPENHAB_CONNECTOR
        printf("Sitemap::openlink(\"%s\")\r\n", url);
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
#if DEBUG_OPENHAB_CONNECTOR
            printf("  idx: %u type=parent_link   link=\"%s\"\r\n", item_count, item_array[item_count].getPageLink());
#endif
            item_count++;
        }

        JsonArray widget_array = doc["widgets"].as<JsonArray>();

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
            {
                char buffer[STR_LABEL_LEN];
                snprintf(buffer, sizeof(buffer), "%s", json_str(widget["label"]));
                char *end = strchr(buffer, '[');
                if (end == NULL)
                    end = buffer + strlen(buffer);
                end -= 1;
                while(end > buffer && isspace(*end)) end--;
                end[1] = '\0';

                item->setLabel(buffer);
            }
            else
            {
                item->setLabel("NO LABEL");
            }

#if DEBUG_OPENHAB_CONNECTOR
            printf("  idx: %u label=\"%s\"", item_count, item->getLabel());
#endif

            // Icon
            if (widget["icon"])
            {
                item->setIconName(widget["icon"]);
#if DEBUG_OPENHAB_CONNECTOR
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

#if DEBUG_OPENHAB_CONNECTOR
            printf("  type=%u", item->getType());
#endif

            // MinVal
            if (widget["minValue"])
                item->setMinVal(widget["minValue"].as<float>());
            else if (json_item["stateDescription"]["minimum"])
                item->setMinVal(json_item["stateDescription"]["minimum"].as<float>());
            else
                item->setMinVal(0.0f);
#if DEBUG_OPENHAB_CONNECTOR
            printf("  minVal=%.2f", item->getMinVal());
#endif

            // MaxVal
            if (widget["maxValue"])
                item->setMaxVal(widget["maxValue"].as<float>());
            else if (json_item["stateDescription"]["maximum"])
                item->setMaxVal(json_item["stateDescription"]["maximum"].as<float>());
            else
                item->setMaxVal(100.0f);
#if DEBUG_OPENHAB_CONNECTOR
            printf("  maxVal=%.2f", item->getMaxVal());
#endif

            // Step
            if (widget["step"])
                item->setStep(widget["step"].as<float>());
            else if (json_item["stateDescription"]["step"])
                item->setStep(json_item["stateDescription"]["step"].as<float>());
            else
                item->setStep(1.0f);
#if DEBUG_OPENHAB_CONNECTOR
            printf("  step=%.2f", item->getStep());
#endif

            // Number format string
            if (json_item["stateDescription"]["pattern"])
                item->setNumberPattern(json_item["stateDescription"]["pattern"]);
            else
                item->setNumberPattern("%d");
#if DEBUG_OPENHAB_CONNECTOR
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
#if DEBUG_OPENHAB_CONNECTOR
                    printf("  num-statetext=\"%s\"", item->getStateText());
#endif
                }
                else
                {
                    item->setStateText(json_item["state"]);
#if DEBUG_OPENHAB_CONNECTOR
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
#if DEBUG_OPENHAB_CONNECTOR
                printf("  page_link=\"%s\"", item->getPageLink());
#endif
            }

            if (json_item["link"])
            {
                item->setLink(json_item["link"]);
#if DEBUG_OPENHAB_CONNECTOR
                printf("  link=\"%s\"", item->getLink());
#endif
            }

            // Mappings
            if (widget["mappings"])
                parse_selection(item, widget["mappings"]);
            else if (json_item["commandDescription"]["commandOptions"])
                parse_selection(item, json_item["commandDescription"]["commandOptions"]);

#if DEBUG_OPENHAB_CONNECTOR
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

#if !CONFIG_IDF_TARGET_LINUX
    http.end();
#endif

    return retval;
}
