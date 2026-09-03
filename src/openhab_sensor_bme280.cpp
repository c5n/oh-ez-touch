#include "openhab_sensor_bme280.hpp"
#include "openhab_sensor_connector.hpp"
#include "debug.h"

#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>

#ifndef DEBUG_OPENHAB_SENSOR_BME280
#define DEBUG_OPENHAB_SENSOR_BME280 0
#endif

#ifndef OPENHAB_SENSOR_BME280_SDA
#define OPENHAB_SENSOR_BME280_SDA 33
#endif

#ifndef OPENHAB_SENSOR_BME280_SCL
#define OPENHAB_SENSOR_BME280_SCL 32
#endif

#ifndef OPENHAB_SENSOR_BME280_ADDR
#define OPENHAB_SENSOR_BME280_ADDR 0x76
#endif

/* "%.3f" of a pressure in hPa is the longest value published here
 * ("1013.250"), so eight characters plus the terminator. */
#define STR_SENSOR_VALUE_LEN 16

static Adafruit_BME280 bme280;

void openhab_sensor_bme280_setup()
{
    Wire.begin(OPENHAB_SENSOR_BME280_SDA, OPENHAB_SENSOR_BME280_SCL);
    bme280.begin(OPENHAB_SENSOR_BME280_ADDR, &Wire);

    // recommended settings for weather monitoring
    bme280.setSampling(
        Adafruit_BME280::MODE_FORCED,
        Adafruit_BME280::SAMPLING_X1,
        Adafruit_BME280::SAMPLING_X1,
        Adafruit_BME280::SAMPLING_X1,
        Adafruit_BME280::FILTER_OFF);
}

static void publish_reading(Config &cfg, const char *item, float value)
{
    if (strlen(item) == 0)
        return;

    char buffer[STR_SENSOR_VALUE_LEN];
    snprintf(buffer, sizeof(buffer), "%.3f", value);

#if DEBUG_OPENHAB_SENSOR_BME280
    debug_printf("openhab_sensor_bme280_update: %s = %s\r\n", item, buffer);
#endif
    openhab_sensor_connector_publish(cfg, item, buffer);
}

void openhab_sensor_bme280_update(Config &cfg)
{
    bme280.takeForcedMeasurement();

    publish_reading(cfg, cfg.item.openhab.sensors.bme280.items.temperature, bme280.readTemperature());
    publish_reading(cfg, cfg.item.openhab.sensors.bme280.items.humidity, bme280.readHumidity());
    publish_reading(cfg, cfg.item.openhab.sensors.bme280.items.pressure, bme280.readPressure() / 100.0f);
}
