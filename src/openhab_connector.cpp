#include "openhab_connector.hpp"
#include <HTTPClient.h>
#include "debug.h"

#ifndef DEBUG_OPENHAB_CONNECTOR
#define DEBUG_OPENHAB_CONNECTOR 0
#endif

#ifndef DEBUG_OPENHAB_CONNECTOR_PACKETDUMP
#define DEBUG_OPENHAB_CONNECTOR_PACKETDUMP 0
#endif

int Item::update(String link)
{
    int retval = 0;

    String url = link;
    url += "/state";

#if DEBUG_OPENHAB_CONNECTOR
    debug_printf("Item::update: Requesting URL: %s\n", url.c_str());
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
        else if (type == ItemType::type_text)
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
        debug_printf("Item::update: URL: %s ", url.c_str());
        debug_printf("Error on HTTP request. httpCode: %d\n", httpCode);

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
    debug_printf("Item::publish: Requesting URL: %s", url.c_str());
#endif

    HTTPClient http;
    http.begin(url);
    http.addHeader("Content-Type", "text/plain");

    String message = {};

    if (type == ItemType::type_number)
    {
        message = String(state_number);
    }
    else if (type == ItemType::type_text)
    {
        message = String(state_text);
    }

#if DEBUG_OPENHAB_CONNECTOR
    printf("Item::publish: POST Message: %s\n", message.c_str());
#endif

    int httpCode = http.POST(message);

    if (httpCode != HTTP_CODE_OK)
    {
        debug_printf("Item::publish: URL: %s ", url.c_str());
        debug_printf("Error on HTTP request. httpCode: %d", httpCode);

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
    debug_printf("Item::getIcon: Requesting URL: %s", url.c_str());
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
        debug_printf("Item::getIcon: Stream size %u\n", stream->available());
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
            debug_printf("get_icon: %u bytes read\n", c);
#if DEBUG_OPENHAB_CONNECTOR_PACKETDUMP
            for (int i = 0; i < c; i++)
                printf("%02x ", p_dst[i]);
            printf("\n");
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
        debug_printf("Item::getIcon: URL: %s ", url.c_str());
        debug_printf("Error on HTTP request. httpCode: %i\n", httpCode);
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
    debug_printf("Sitemap::openlink: Requesting URL: %s \n", url.c_str());
#endif

    HTTPClient http;
    http.begin(url);

    int httpCode = http.GET();

    if (httpCode == HTTP_CODE_OK)
    {
        String payload = http.getString();

#if DEBUG_OPENHAB_CONNECTOR
        debug_printf("httpCode: %i\n", httpCode);
        debug_printf(payload.c_str());
#endif

        // Parse JSON object
        if (doc.isNull() == false)
            doc.clear();

        DeserializationError error = deserializeJson(doc, payload, DeserializationOption::NestingLimit(15));
        if (error)
        {
            debug_printf("Sitemap::openlink: deserializeJson() failed: %s\n", error.c_str());
            return false;
        }

        if (doc.containsKey("error"))
        {
            debug_printf("Sitemap::openlink: json error message: %s\n", doc["error"]["message"].as<char *>());
            return false;
        }

#if DEBUG_OPENHAB_CONNECTOR
        debug_printf("Doc memory usage: %u\n", doc.memoryUsage());
        debug_printf("%s\n", doc["title"].as<char *>());
#endif
    }
    else // httpCode != HTTP_CODE_OK
    {
        debug_printf("Sitemap::openlink: URL: %s\n", url.c_str());
        debug_printf("Error on HTTP request. httpCode: %i\n", httpCode);

        retval = -1;
    }

    http.end();

    return retval;
}
