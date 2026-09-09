#include "openhab_sensor_bme280.hpp"
#include "openhab_sensor_connector.hpp"
#include "debug.h"

#include <stdio.h>
#include <string.h>

#include "port/port_bme280.h"

/* "%.3f" of a pressure in hPa is the longest value published here
 * ("1013.250"), so eight characters plus the terminator. */
#define STR_SENSOR_VALUE_LEN 16

bool openhab_sensor_bme280_setup()
{
    return port_bme280_init();
}

static void publish_reading(Config &cfg, const char *item, float value)
{
    if (strlen(item) == 0)
        return;

    char buffer[STR_SENSOR_VALUE_LEN];
    snprintf(buffer, sizeof(buffer), "%.3f", value);

#if CONFIG_OHEZ_DEBUG_OPENHAB_SENSOR_BME280
    printf("openhab_sensor_bme280_update: %s = %s\r\n", item, buffer);
#endif
    openhab_sensor_connector_publish(cfg, item, buffer);
}

void openhab_sensor_bme280_update(Config &cfg)
{
    float temperature = 0.0f;
    float humidity = 0.0f;
    float pressure = 0.0f;

    /* Nothing is published when the reading fails. The alternative -- sending
     * the last value again, or a zero -- would put fiction into someone's item
     * history, where it is indistinguishable from a real measurement. */
    if (port_bme280_read(&temperature, &humidity, &pressure) == false)
    {
#if CONFIG_OHEZ_DEBUG_OPENHAB_SENSOR_BME280
        printf("openhab_sensor_bme280_update: no reading\r\n");
#endif
        return;
    }

    publish_reading(cfg, cfg.item.openhab.sensors.bme280.items.temperature, temperature);
    publish_reading(cfg, cfg.item.openhab.sensors.bme280.items.humidity, humidity);
    publish_reading(cfg, cfg.item.openhab.sensors.bme280.items.pressure, pressure);
}
