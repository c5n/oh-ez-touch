#include "openhab_connector.hpp"

#if (SIMULATOR != 1)
#include <HTTPClient.h>
#endif

#ifndef DEBUG_OPENHAB_CONNECTOR
#define DEBUG_OPENHAB_CONNECTOR 0
#endif

#ifndef DEBUG_OPENHAB_CONNECTOR_PACKETDUMP
#define DEBUG_OPENHAB_CONNECTOR_PACKETDUMP 0
#endif

int Item::update(const char* link)
{
    int retval = 0;

    char url[STR_LINK_LEN];
    snprintf(url, sizeof(url), "%s/state", link);

#if DEBUG_OPENHAB_CONNECTOR
    printf("Item::update: Requesting URL: %s\r\n", url);
#endif
#if (SIMULATOR != 1)
    HTTPClient http;
    http.begin(url);

    int httpCode = http.GET();

    if (httpCode == HTTP_CODE_OK)
    {
        char remote_state[STR_STATE_TEXT_LEN];
        strncpy(remote_state, http.getString().c_str(), sizeof(remote_state));

        // State
        if (   Item::type == ItemType::type_number
            || Item::type == ItemType::type_setpoint
            || Item::type == ItemType::type_slider)
        {
            // convert number to get rid of unit
            snprintf(remote_state, sizeof(remote_state), "%f", strtof(remote_state, NULL));
        }

        if (strcmp (Item::state_text, remote_state) != 0)
        {
            retval = 1;
            strncpy(Item::state_text, remote_state, sizeof(Item::state_text));
#if DEBUG_OPENHAB_CONNECTOR
            printf("  update statetext to \"%s\"\r\n", Item::state_text);
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

#if (SIMULATOR != 1)
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

#if (SIMULATOR != 1)
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

            int c = stream->readBytes(p_dst, ((size > dst_avail) ? dst_avail : size));

#if DEBUG_OPENHAB_CONNECTOR
            debug_printf("get_icon: %u bytes read\r\n", c);
#if DEBUG_OPENHAB_CONNECTOR_PACKETDUMP
            for (int i = 0; i < c; i++)
                printf("%02x ", p_dst[i]);
            printf("\r\n");
#endif
#endif
            icon_size += c;

            if (len > c)
                len -= c;

            if (dst_avail < c)
            {
                icon_size = 0;
                break;
            }

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
    DynamicJsonDocument doc(12000);

#if DEBUG_OPENHAB_CONNECTOR
    printf("Item::openlink: Requesting URL: %s\r\n", url);
#endif
#if (SIMULATOR != 1)
    HTTPClient http;
    http.begin(url);

    int httpCode = http.GET();

    if (httpCode == HTTP_CODE_OK)
    {
        String payload = http.getString();

#if DEBUG_OPENHAB_CONNECTOR
        Serial.println(httpCode);
        Serial.println(payload);
#endif

        // Parse JSON object
        if (doc.isNull() == false)
            doc.clear();

        DeserializationError error = deserializeJson(doc, payload, DeserializationOption::NestingLimit(15));
        if (error)
        {
            printf("Sitemap::openlink: deserializeJson() failed: %s", error.c_str());
            doc.clear();
            return false;
        }

        if (doc.containsKey("error"))
        {
            printf("Sitemap::openlink: json error message: %s", doc["error"]["message"]);
            doc.clear();
            return false;
        }

#if DEBUG_OPENHAB_CONNECTOR
        Serial.print("Doc memory usage: ");
        Serial.println(doc.memoryUsage());
#endif

        // Save current and last page urls
        strncpy(last_url, current_url, sizeof(last_url));
        strncpy(current_url, url, sizeof(current_url));

        if (doc["title"])
            strncpy(title, doc["title"], sizeof(title));
        else
            strncpy(title, "no title", sizeof(title));

#if DEBUG_OPENHAB_CONNECTOR
        printf("Sitemap::openlink(\"%s\")\r\n", url);
        printf("  title=\"%s\"\r\n", title);
#endif

        // Cleanup Items
        for (size_t i = 0; i < ITEM_COUNT_MAX; ++i)
        {
            item_array[i].cleanItem();
        }

        Sitemap::item_count = 0;

        // Update Items

        // if current location is a child of the sitemap then set first item
        if (doc["parent"]["link"])
        {
            item_array[Sitemap::item_count].setType(ItemType::type_parent_link);
            item_array[Sitemap::item_count].setPageLink(doc["parent"]["link"]);
#if DEBUG_OPENHAB_CONNECTOR
            printf("  idx: %u type=parent_link   link=\"%s\"\r\n", Sitemap::item_count, item_array[Sitemap::item_count].getPageLink());
#endif
            Sitemap::item_count++;
        }
//         else if (doc["leaf"] && doc["leaf"].as<bool>() == true)
//         {
//             item_array[Sitemap::item_count].setType(ItemType::type_parent_link);
//             item_array[Sitemap::item_count].setPageLink(last_url);
// #if DEBUG_OPENHAB_CONNECTOR
//             printf("  idx: %u type=parent_link   link=\"%s\"\r\n", Sitemap::item_count, item_array[Sitemap::item_count].getPageLink());
// #endif
//             Sitemap::item_count++;
//         }

        JsonArray widget_array = doc["widgets"].as<JsonArray>();

        for (size_t widget_index = 0; widget_index < widget_array.size(); widget_index++)
        {
            JsonVariant widget = widget_array[widget_index];
            Item* item = &item_array[Sitemap::item_count];

            // Label
            if (widget["label"])
            {
                char buffer[STR_LABEL_LEN];
                snprintf(buffer, sizeof(buffer), widget["label"]);
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
            printf("  idx: %u label=\"%s\"", Sitemap::item_count, item->getLabel());
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
                else if (widget["item"]["type"] && widget["item"]["type"].as<String>() >= "Number" && widget["item"]["type"].as<String>() <= "Number:Z")
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
                if (widget["item"]["type"].as<String>() == "Switch")
                    item->setType(ItemType::type_switch);
                else if (widget["item"]["type"].as<String>() == "Rollershutter")
                    item->setType(ItemType::type_rollershutter);
                else if (widget["item"]["type"].as<String>() == "Player")
                    item->setType(ItemType::type_player);
                else if (widget["item"]["type"].as<String>() == "Group")
                {
                    if (widget["item"]["groupType"].as<String>() == "Switch")
                        item->setType(ItemType::type_switch);
                    else if (widget["item"]["groupType"].as<String>() == "Rollershutter")
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
            else if (widget["item"]["stateDescription"]["minimum"])
                item->setMinVal(widget["item"]["stateDescription"]["minimum"].as<float>());
            else
                item->setMinVal(0.0f);
#if DEBUG_OPENHAB_CONNECTOR
            printf("  minVal=%.2f", item->getMinVal());
#endif

            // MaxVal
            if (widget["maxValue"])
                item->setMaxVal(widget["maxValue"].as<float>());
            else if (widget["item"]["stateDescription"]["maximum"])
                item->setMaxVal(widget["item"]["stateDescription"]["maximum"].as<float>());
            else
                item->setMaxVal(100.0f);
#if DEBUG_OPENHAB_CONNECTOR
            printf("  maxVal=%.2f", item->getMaxVal());
#endif

            // Step
            if (widget["step"])
                item->setStep(widget["step"].as<float>());
            else if (widget["item"]["stateDescription"]["step"])
                item->setStep(widget["item"]["stateDescription"]["step"].as<float>());
            else
                item->setStep(1.0f);
#if DEBUG_OPENHAB_CONNECTOR
            printf("  step=%.2f", item->getStep());
#endif

            // Number format string
            if (widget["item"]["stateDescription"]["pattern"])
                item->setNumberPattern(widget["item"]["stateDescription"]["pattern"]);
            else
                item->setNumberPattern("%d");
#if DEBUG_OPENHAB_CONNECTOR
            printf("  numpat=\"%s\"", item->getNumberPattern());
#endif

            // State
            if (widget["item"]["state"])
            {
                if (   item->getType() == ItemType::type_number
                    || item->getType() == ItemType::type_setpoint
                    || item->getType() == ItemType::type_slider)
                {
                    // convert number to get rid of unit
                    item->setStateNumber(widget["item"]["state"].as<String>().toFloat());
#if DEBUG_OPENHAB_CONNECTOR
                    printf("  num-statetext=\"%s\"", item->getStateText());
#endif
                }
                else
                {
                    item->setStateText(widget["item"]["state"]);
#if DEBUG_OPENHAB_CONNECTOR
                    printf("  statetext=\"%s\"", item->getStateText());
#endif
                }
            }

            // Transformed State
            if (widget["item"]["transformedState"])
            {
                item->setTransformedStateText(widget["item"]["transformedState"]);
            }

            // Links
            if (widget["linkedPage"]["link"])
            {
                item->setPageLink(widget["linkedPage"]["link"]);
#if DEBUG_OPENHAB_CONNECTOR
                printf("  page_link=\"%s\"", item->getPageLink());
#endif
            }

            if (widget["item"]["link"])
            {
                item->setLink(widget["item"]["link"]);
#if DEBUG_OPENHAB_CONNECTOR
                printf("  link=\"%s\"", item->getLink());
#endif
            }

            // Mappings
            if (widget["mappings"])
            {
                JsonArray map_array = widget["mappings"].as<JsonArray>();
                for (size_t i = 0; i < map_array.size(); ++i)
                {
                    JsonVariant map_elem = map_array[i];
                    item->setSelectionLabel(i, map_elem["label"]);
                    item->setSelectionCommand(i, map_elem["command"]);
                }
                item->setSelectionCount(map_array.size());
            }
            else if (widget["item"]["commandDescription"]["commandOptions"])
            {
                JsonArray map_array = widget["item"]["commandDescription"]["commandOptions"].as<JsonArray>();
                for (size_t i = 0; i < map_array.size(); ++i)
                {
                    JsonVariant map_elem = map_array[i];
                    item->setSelectionLabel(i, map_elem["label"]);
                    item->setSelectionCommand(i, map_elem["command"]);
                }
                item->setSelectionCount(map_array.size());
            }

#if DEBUG_OPENHAB_CONNECTOR
            printf("\r\n");
#endif

            Sitemap::item_count++;

            if (Sitemap::item_count > ITEM_COUNT_MAX)
                break;
        }
    }
    else // httpCode != HTTP_CODE_OK
    {
        printf("Sitemap::openlink: ERROR httpCode: %i URL: %s\r\n", httpCode, url);
        retval = -1;
    }

    doc.clear();

    http.end();
#endif
    return retval;
}
