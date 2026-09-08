#include "openhab_sensor_main.hpp"
#include "openhab_sensor_bme280.hpp"

#include "port/port_sys.h"

#include "config.hpp"

#ifndef DEBUG_OPENHAB_SENSOR_MAIN
#define DEBUG_OPENHAB_SENSOR_MAIN 0
#endif

static bool sensor_bme280_initialized = false;

void openhab_sensor_main_setup(Config &config)
{
    if (config.item.openhab.sensors.bme280.use == true)
    {
        openhab_sensor_bme280_setup();
        sensor_bme280_initialized = true;
    }
}

void openhab_sensor_main_loop(Config &config)
{
    static uint64_t bme280_refresh_timeout = 0;

    if ((sensor_bme280_initialized == true) && (port_millis() >= bme280_refresh_timeout))
    {
#if DEBUG_OPENHAB_SENSOR_MAIN
        printf("openhab_sensor_main_loop: refreshing bme280\r\n");
#endif
        bme280_refresh_timeout = port_millis() + config.item.openhab.sensors.bme280.interval * 1000;
        openhab_sensor_bme280_update(config);
    }
}
