#include "openhab_sensor_main.hpp"

#include "debug.h"
#include "openhab_sensor_bme280.hpp"

#include "port/port_sys.h"

#include "config/config.hpp"

static bool sensor_bme280_initialized = false;

void openhab_sensor_main_setup(Config &config)
{
    if (config.item.openhab.sensors.bme280.use == true)
    {
        /* Only when one actually answered. Polling a sensor that is not there
         * would be a hundred failed I2C transactions an hour and an item that
         * never updates, with nothing said about why. */
        sensor_bme280_initialized = openhab_sensor_bme280_setup();
    }
}

void openhab_sensor_main_loop(Config &config)
{
    static uint64_t bme280_refresh_timeout = 0;

    if ((sensor_bme280_initialized == true) && (port_millis() >= bme280_refresh_timeout))
    {
#if CONFIG_OHEZ_DEBUG_OPENHAB_SENSOR_MAIN
        printf("openhab_sensor_main_loop: refreshing bme280\r\n");
#endif
        bme280_refresh_timeout = port_millis() + config.item.openhab.sensors.bme280.interval * 1000;
        openhab_sensor_bme280_update(config);
    }
}
