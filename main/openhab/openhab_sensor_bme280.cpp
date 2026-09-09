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

bool openhab_sensor_bme280_read(float *temperature_c, float *humidity_pct, float *pressure_hpa)
{
    if (port_bme280_read(temperature_c, humidity_pct, pressure_hpa) == false)
    {
#if CONFIG_OHEZ_DEBUG_OPENHAB_SENSOR_BME280
        printf("openhab_sensor_bme280_read: no reading\r\n");
#endif
        /* Nothing is published when the reading fails, by either sink. The
         * alternative -- sending the last value again, or a zero -- would put
         * fiction into someone's item history, where it is indistinguishable
         * from a real measurement. */
        return false;
    }

    return true;
}

static void publish_reading(Config &cfg, const char *item, float value)
{
    if (strlen(item) == 0)
        return;

    char buffer[STR_SENSOR_VALUE_LEN];
    snprintf(buffer, sizeof(buffer), "%.3f", value);

#if CONFIG_OHEZ_DEBUG_OPENHAB_SENSOR_BME280
    printf("openhab_sensor_bme280_publish: %s = %s\r\n", item, buffer);
#endif
    openhab_sensor_connector_publish(cfg, item, buffer);
}

void openhab_sensor_bme280_publish(Config &cfg, float temperature_c, float humidity_pct,
                                   float pressure_hpa)
{
    publish_reading(cfg, cfg.item.openhab.sensors.bme280.items.temperature, temperature_c);
    publish_reading(cfg, cfg.item.openhab.sensors.bme280.items.humidity, humidity_pct);
    publish_reading(cfg, cfg.item.openhab.sensors.bme280.items.pressure, pressure_hpa);
}
