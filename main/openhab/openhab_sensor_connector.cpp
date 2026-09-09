#include "openhab_sensor_connector.hpp"

#include "debug.h"

#include <stdio.h>

#include "openhab_client.hpp"

#define STR_URL_LEN     128

void openhab_sensor_connector_publish(Config &cfg, const char* item, const char* value)
{
    char url[STR_URL_LEN];

    snprintf(url, sizeof(url), "http://%s:%u/rest/items/%s",
            cfg.item.openhab.hostname,
            cfg.item.openhab.port,
            item);

#if CONFIG_OHEZ_DEBUG_OPENHAB_SENSOR_CONNECTOR
    printf("openhab_sensor_connector_publish: Requesting URL: %s\r\n", url);
    printf("openhab_sensor_connector_publish: POST Message: %s\r\n", value);
#endif

    /* Through the client task, like every other request. This runs from
     * openhab_sensor_main_loop() on the task that draws, and three blocking
     * POSTs per sensor interval is three chances per interval for the screen
     * to stop. Whether the reading reached openHAB is not something anything
     * here could act on -- the next one is along in a minute either way -- so
     * the only failure worth reporting is not managing to queue it. */
    if (openhab_client_command(url, value) == false)
        printf("openhab_sensor_connector_publish: not queued: %s\r\n", url);
}
