#include "sensor_bme280.hpp"
#include "debug.h"

#include <stdio.h>

#include "port/port_bme280.h"

bool sensor_bme280_setup()
{
    return port_bme280_init();
}

bool sensor_bme280_read(float *temperature_c, float *humidity_pct, float *pressure_hpa)
{
    if (port_bme280_read(temperature_c, humidity_pct, pressure_hpa) == false)
    {
#if CONFIG_OHEZ_DEBUG_SENSOR_BME280
        printf("sensor_bme280_read: no reading\r\n");
#endif
        /* Nothing is published when the reading fails. The alternative --
         * sending the last value again, or a zero -- would put fiction into
         * someone's item history, where it is indistinguishable from a real
         * measurement. */
        return false;
    }

#if CONFIG_OHEZ_DEBUG_SENSOR_BME280
    printf("sensor_bme280_read: %.3f C, %.3f %%, %.3f hPa\r\n",
           *temperature_c, *humidity_pct, *pressure_hpa);
#endif

    return true;
}
