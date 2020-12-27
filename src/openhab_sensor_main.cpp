#include "openhab_sensor_main.hpp"
#include "openhab_sensor_bme280.hpp"

#include "Arduino.h"
#include "config.hpp"

#ifndef OPENHAB_SENDOR_MAIN_DEBUG
#define OPENHAB_SENDOR_MAIN_DEBUG 1
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

    if ((sensor_bme280_initialized == true) && (millis() > bme280_refresh_timeout))
    {
#if OPENHAB_SENDOR_MAIN_DEBUG
        Serial.println("openhab_sensor_main_loop: refreshing bme280");
#endif
        bme280_refresh_timeout = millis() + config.item.openhab.sensors.bme280.interval * 1000;
        openhab_sensor_bme280_update(config);
    }
}
