#ifndef OPENHAB_SENSOR_CONNECTOR_HPP
#define OPENHAB_SENSOR_CONNECTOR_HPP

#include "Arduino.h"
#include <HTTPClient.h>

#ifndef DEBUG_OPENHAB_SENSOR_CONNECTOR
#define DEBUG_OPENHAB_SENSOR_CONNECTOR 0
#endif

#define STR_URL_LEN     128

void openhab_sensor_connector_publish(Config &cfg, const char* item, const char* value)
{
    char url[STR_URL_LEN];

    snprintf(url, sizeof(url), "http://%s:%u/rest/items/%s",
            cfg.item.openhab.hostname,
            cfg.item.openhab.port,
            item);

#if DEBUG_OPENHAB_SENSOR_CONNECTOR
    printf("openhab_sensor_connector_publish: Requesting URL: %s\r\n", url);
#endif

    HTTPClient http;
    http.begin(url);
    http.addHeader("Content-Type", "text/plain");

#if DEBUG_OPENHAB_SENSOR_CONNECTOR
    printf("openhab_sensor_connector_publish: POST Message: %s\r\n", value.c_str());
#endif

    int httpCode = http.POST(value);

    if (httpCode != HTTP_CODE_OK)
    {
        printf("openhab_sensor_connector_publish ERROR httpCode: %i URL: %s\r\n", httpCode, url);
    }

    http.end();
}

#endif
