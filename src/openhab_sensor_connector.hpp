#ifndef OPENHAB_SENSOR_CONNECTOR_HPP
#define OPENHAB_SENSOR_CONNECTOR_HPP

#include "Arduino.h"
#include <HTTPClient.h>

#ifndef DEBUG_OPENHAB_SENSOR_CONNECTOR
#define DEBUG_OPENHAB_SENSOR_CONNECTOR 0
#endif

void openhab_sensor_connector_publish(Config &cfg, String item, String value)
{
    String url = "http://" + String(cfg.item.openhab.hostname) + ":" + String(cfg.item.openhab.port);
    url += "/rest/items/" + item;

#if DEBUG_OPENHAB_SENSOR_CONNECTOR
    debug_printf("openhab_sensor_connector_publish: Requesting URL: %s\n", url.c_str());
#endif

    HTTPClient http;
    http.begin(url);

#if DEBUG_OPENHAB_SENSOR_CONNECTOR
    printf("openhab_sensor_connector_publish: POST Message: %s\n", value.c_str());
#endif

    int httpCode = http.POST(value);

    if (httpCode != HTTP_CODE_OK)
    {
        debug_printf("openhab_sensor_connector_publish: URL: %s\n", url.c_str());
        debug_printf("Error on HTTP request. httpCode: %d\n", httpCode);
    }

    http.end();
}

#endif
