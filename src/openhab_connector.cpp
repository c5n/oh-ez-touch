#include "openhab_connector.hpp"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#if (SIMULATOR != 1)
#include <HTTPClient.h>
#else
#include "sim/icon_fixture.hpp"
#include "sim/sitemap_fixture.hpp"
#endif

/* JsonVariant::as<const char *>() yields NULL for a missing or non-string
 * value; the comparisons below want an empty string in that case. */
static inline const char *json_str(JsonVariant value)
{
    const char *str = value.as<const char *>();
    return (str != NULL) ? str : "";
}

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
        strlcpy(remote_state, http.getString().c_str(), sizeof(remote_state));

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
            strlcpy(Item::state_text, remote_state, sizeof(Item::state_text));
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

#if (SIMULATOR == 1)
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
    /* ArduinoJson 7 documents size themselves, so the former fixed 12000 byte
     * capacity is gone; the parser now grows the pool to fit the page. */
    JsonDocument doc;

#if DEBUG_OPENHAB_CONNECTOR
    printf("Item::openlink: Requesting URL: %s\r\n", url);
#endif

#if (SIMULATOR == 1)
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
    Serial.println(httpCode);
    Serial.println(payload);
#endif
#endif

    if (payload_ok == true)
    {
        // Parse JSON object
        DeserializationError error = deserializeJson(doc, payload, DeserializationOption::NestingLimit(15));
        if (error)
        {
            printf("Sitemap::openlink: deserializeJson() failed: %s", error.c_str());
            doc.clear();
            return false;
        }

        /* containsKey() is deprecated in ArduinoJson 7. The value is indexed as
         * an object right below, so test for exactly that. */
        if (doc["error"].is<JsonObject>())
        {
            printf("Sitemap::openlink: json error message: %s", json_str(doc["error"]["message"]));
            doc.clear();
            return false;
        }

#if DEBUG_OPENHAB_CONNECTOR
        /* ArduinoJson 7 dropped memoryUsage() -- it always returns zero. The
         * serialized size is the closest figure that still says something
         * about how big the page was. */
        Serial.print("Doc serialized size: ");
        Serial.println(measureJson(doc));
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
                else if (   widget["item"]["type"]
                         && strcmp(json_str(widget["item"]["type"]), "Number") >= 0
                         && strcmp(json_str(widget["item"]["type"]), "Number:Z") <= 0)
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
                if (strcmp(json_str(widget["item"]["type"]), "Switch") == 0)
                    item->setType(ItemType::type_switch);
                else if (strcmp(json_str(widget["item"]["type"]), "Rollershutter") == 0)
                    item->setType(ItemType::type_rollershutter);
                else if (strcmp(json_str(widget["item"]["type"]), "Player") == 0)
                    item->setType(ItemType::type_player);
                else if (strcmp(json_str(widget["item"]["type"]), "Group") == 0)
                {
                    if (strcmp(json_str(widget["item"]["groupType"]), "Switch") == 0)
                        item->setType(ItemType::type_switch);
                    else if (strcmp(json_str(widget["item"]["groupType"]), "Rollershutter") == 0)
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
                    item->setStateNumber(strtof(json_str(widget["item"]["state"]), NULL));
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

            if (Sitemap::item_count >= ITEM_COUNT_MAX)
                break;
        }
    }
    else
    {
        retval = -1;
    }

    doc.clear();

#if (SIMULATOR != 1)
    http.end();
#endif

    return retval;
}
