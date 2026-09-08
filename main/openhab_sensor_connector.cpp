#include "openhab_sensor_connector.hpp"

#include <stdio.h>

#include "openhab_http.hpp"

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
    printf("openhab_sensor_connector_publish: POST Message: %s\r\n", value);
#endif

    if (openhab_http_post_text(url, value) != 0)
        printf("openhab_sensor_connector_publish ERROR URL: %s\r\n", url);
}
