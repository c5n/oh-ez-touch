#include "openhab_connector.hpp"
#include <HTTPClient.h>

#ifndef DEBUG_OPENHAB_CONNECTOR
#define DEBUG_OPENHAB_CONNECTOR 0
#endif

#ifndef DEBUG_OPENHAB_CONNECTOR_PACKETDUMP
#define DEBUG_OPENHAB_CONNECTOR_PACKETDUMP 0
#endif

#define WIDGET_COUNT_MAX 6

int Item::update(String link)
{
    int retval = 0;

    String url = link;
    url += "/state";

#if DEBUG_OPENHAB_CONNECTOR
    Serial.print("Item::update: Requesting URL: ");
    Serial.println(url);
#endif

    HTTPClient http;
    http.begin(url);

    int httpCode = http.GET();

    if (httpCode == HTTP_CODE_OK)
    {
        String state = http.getString();

        if (type == ItemType::type_number)
        {
            float new_state = state.toFloat();
            if (state_number != new_state)
            {
                retval = 1;
                state_number = new_state;
            }
        }
        else
        {
            if (state_text != state)
            {
                retval = 1;
                state_text = state;
            }
        }
    }
    else
    {
        Serial.print("Item::update: URL: ");
        Serial.println(url);
        Serial.print("Error on HTTP request. httpCode: ");
        Serial.println(httpCode);

        retval = -1;
    }

    http.end();

    return retval;
}

int Item::publish(String link)
{
    int retval = 0;

    String url = link;

#if DEBUG_OPENHAB_CONNECTOR
    Serial.print("Item::publish: Requesting URL: ");
    Serial.println(url);
#endif

    HTTPClient http;
    http.begin(url);
    http.addHeader("Content-Type", "text/plain");

    String message = {};

    if (type == ItemType::type_number)
    {
        message = String(state_number);
    }
    else
    {
        message = String(state_text);
    }

#if DEBUG_OPENHAB_CONNECTOR
    printf("Item::publish: POST Message: %s\r\n", message.c_str());
#endif

    int httpCode = http.POST(message);

    if (httpCode != HTTP_CODE_OK)
    {
        Serial.print("Item::publish: URL: ");
        Serial.println(url);
        Serial.print("Error on HTTP request. httpCode: ");
        Serial.println(httpCode);

        retval = -1;
    }

    http.end();

    return retval;
}

size_t Item::getIcon(String website, String name, String state, unsigned char *buffer, size_t buffer_size)
{
    size_t icon_size = 0;

    String url = website + "/icon/" + name + "?state=" + state + "&format=png";

#if DEBUG_OPENHAB_CONNECTOR
    Serial.print("Item::getIcon: Requesting URL: ");
    Serial.println(url);
#endif

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
        Serial.printf("Item::getIcon: Stream size %u\r\n", stream->available());
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
            Serial.printf("get_icon: %u bytes read\r\n", c);
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
        Serial.print("Item::getIcon: URL: ");
        Serial.println(url);
        Serial.print("Error on HTTP request. httpCode: ");
        Serial.println(httpCode);
    }

    http.end(); //Free the resources

    // remove footer which consists of CR LF "0" CR LF CR LF
    // ToDo: find a better solution
    if (icon_size > 7)
        icon_size -= 7;

    return icon_size;
}

int Sitemap::openlink(String url)
{
    int retval = 0;

#if DEBUG_OPENHAB_CONNECTOR
    Serial.print("Sitemap::openlink: Requesting URL: ");
    Serial.println(url);
#endif

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
            Serial.print(F("Sitemap::openlink: deserializeJson() failed: "));
            Serial.println(error.c_str());
            return false;
        }

        if (doc.containsKey("error"))
        {
            Serial.print("Sitemap::openlink: json error message: ");
            Serial.println(doc["error"]["message"].as<String>());
            return false;
        }

#if DEBUG_OPENHAB_CONNECTOR
        Serial.print("Doc memory usage: ");
        Serial.println(doc.memoryUsage());
        Serial.println(doc["title"].as<char *>());
#endif

        title = doc["title"].as<String>();

        // Update Items
        size_t array_index = 0;

        // check if current location is a child of the sitemap
        if (doc["parent"]["link"])
        {
            item_array[array_index].setLink(doc["parent"]["link"]);
            item_array[array_index].setType(ItemType::type_parent_link);
        }

        JsonArray widget_array = doc["widgets"].as<JsonArray>();

        for (int widget_index = 0; widget_index < widget_array.size(); widget_index++)
        {
            JsonVariant widget = widget_array.getElement(widget_index);

            // Label
            if (widget["linkedPage"])
                item_array[array_index].setLabel(widget["linkedPage"]["title"]);
            else if (widget["item"])
                item_array[array_index].setLabel(widget["item"]["label"]);
            else
                item_array[array_index].setLabel("NO LABEL");

            // Icon
            if (widget["icon"])
                item_array[array_index].setIconName(widget["icon"]);

            // Type
            if (widget["linkedPage"]["link"])
                item_array[array_index].setType(ItemType::type_link);
            else if (widget["type"] == "Text")
                item_array[array_index].setType(ItemType::type_string);
            else if (widget["type"] == "Switch")
            {
                if (widget["item"]["type"].as<String>() == "Switch")
                    item_array[array_index].setType(ItemType::type_switch);
                else if (widget["item"]["type"].as<String>() == "Rollershutter")
                    item_array[array_index].setType(ItemType::type_rollershutter);
                else if (widget["item"]["type"].as<String>() == "Player")
                    item_array[array_index].setType(ItemType::type_player);
            }
            else if (widget["type"] == "Setpoint")
                item_array[array_index].setType(ItemType::type_setpoint);
            else if (widget["type"] == "Slider")
                item_array[array_index].setType(ItemType::type_slider);
            else if (widget["type"] == "Selection")
                item_array[array_index].setType(ItemType::type_selection);
            else if (widget["type"] == "Colorpicker")
                item_array[array_index].setType(ItemType::type_colorpicker);

            // MinVal
            if (widget["minValue"])
                item_array[array_index].setMinVal(widget["minValue"].as<float>());
            else if (widget["item"]["stateDescription"]["minimum"])
                item_array[array_index].setMinVal(widget["item"]["stateDescription"]["minimum"].as<float>());
            else
                item_array[array_index].setMinVal(0.0f);

            // MaxVal
            if (widget["maxValue"])
                item_array[array_index].setMaxVal(widget["maxValue"].as<float>());
            else if (widget["item"]["stateDescription"]["maximum"])
                item_array[array_index].setMaxVal(widget["item"]["stateDescription"]["maximum"].as<float>());
            else
                item_array[array_index].setMaxVal(100.0f);

            // Step
            if (widget["step"])
                item_array[array_index].setStep(widget["step"].as<float>());
            else if (widget["item"]["stateDescription"]["step"])
                item_array[array_index].setStep(widget["item"]["stateDescription"]["step"].as<float>());
            else
                item_array[array_index].setStep(1.0f);

            if (widget["item"]["state"])
                item_array[array_index].setStateText(widget["item"]["state"]);
                // ToDo Numbers!

            // Number format string
            if (widget["item"]["stateDescription"]["pattern"])
                item_array[array_index].setNumberPattern(widget["item"]["stateDescription"]["pattern"]);
            else
                item_array[array_index].setNumberPattern("%d");

            // Link
            if (item_array[array_index].getType() == ItemType::type_link)
                item_array[array_index].setLink(widget["linkedPage"]["link"]);
            else
                item_array[array_index].setLink(widget["item"]["link"]);

            // Mappings
            if (widget["mappings"])
            {
                JsonArray map_array = widget["mappings"].as<JsonArray>();
                for (size_t i = 0; i < map_array.size(); ++i)
                {
                    JsonVariant map_elem = map_array.getElement(i);
                    item_array[array_index].setSelectionLabel(i, map_elem["label"]);
                    item_array[array_index].setSelectionCommand(i, map_elem["command"]);
                }
                item_array[array_index].setSelectionCount(map_array.size());
            }
            else if (widget["item"]["commandDescription"]["commandOptions"])
            {
                JsonArray map_array = widget["item"]["commandDescription"]["commandOptions"].as<JsonArray>();
                for (size_t i = 0; i < map_array.size(); ++i)
                {
                    JsonVariant map_elem = map_array.getElement(i);
                    item_array[array_index].setSelectionLabel(i, map_elem["label"]);
                    item_array[array_index].setSelectionCommand(i, map_elem["command"]);
                }
                item_array[array_index].setSelectionCount(map_array.size());
            }

            ++array_index;

            if (array_index > WIDGET_COUNT_MAX)
                break;
        }
    }
    else // httpCode != HTTP_CODE_OK
    {
        Serial.print("Sitemap::openlink: URL: ");
        Serial.println(url);
        Serial.print("Error on HTTP request. httpCode: ");
        Serial.println(httpCode);

        retval = -1;
    }

    http.end();

    // ToDo: free json array

    return retval;
}
