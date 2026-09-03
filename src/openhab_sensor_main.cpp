#include "openhab_sensor_main.hpp"
#include "openhab_sensor_bme280.hpp"

#include "Arduino.h"
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
    static unsigned long bme280_refresh_timeout = 0;

    if ((sensor_bme280_initialized == true) && ((long)(millis() - bme280_refresh_timeout) >= 0))
    {
#if DEBUG_OPENHAB_SENSOR_MAIN
        Serial.println("openhab_sensor_main_loop: refreshing bme280");
#endif
        bme280_refresh_timeout = millis() + config.item.openhab.sensors.bme280.interval * 1000;
        openhab_sensor_bme280_update(config);
    }
}
