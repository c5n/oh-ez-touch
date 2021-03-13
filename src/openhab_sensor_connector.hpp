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
    Serial.print("openhab_sensor_connector_publish: Requesting URL: ");
    Serial.println(url);
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
        Serial.print("openhab_sensor_connector_publish: URL: ");
        Serial.println(url);
        Serial.print("Error on HTTP request. httpCode: ");
        Serial.println(httpCode);
    }

    http.end();
}

#endif
