#ifndef OPENHAB_SENSOR_BME280_HPP
#define OPENHAB_SENSOR_BME280_HPP

#include "config.hpp"
#include "openhab_sensor_connector.hpp"

#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>

#ifndef DEBUG_OPENHAB_SENSOR_BME280
#define DEBUG_OPENHAB_SENSOR_BME280 0
#endif

#define SEALEVELPRESSURE_HPA (1013.25)

#ifndef OPENHAB_SENSOR_BME280_SDA
#define OPENHAB_SENSOR_BME280_SDA 33
#endif

#ifndef OPENHAB_SENSOR_BME280_SCL
#define OPENHAB_SENSOR_BME280_SCL 32
#endif

#ifndef OPENHAB_SENSOR_BME280_ADDR
#define OPENHAB_SENSOR_BME280_ADDR 0x76
#endif

Adafruit_BME280 bme280;

void openhab_sensor_bme280_setup()
{
        Wire.begin(OPENHAB_SENSOR_BME280_SDA, OPENHAB_SENSOR_BME280_SCL);
        bme280.begin(OPENHAB_SENSOR_BME280_ADDR, &Wire);

        // init bme280
        // recommended settings for weather monitoring
        bme280.setSampling(
            Adafruit_BME280::MODE_FORCED,
            Adafruit_BME280::SAMPLING_X1,
            Adafruit_BME280::SAMPLING_X1,
            Adafruit_BME280::SAMPLING_X1,
            Adafruit_BME280::FILTER_OFF);
}

void openhab_sensor_bme280_update(Config &cfg)
{
        char buffer[10];
        bme280.takeForcedMeasurement();

        if (strlen(cfg.item.openhab.sensors.bme280.items.temperature) > 0)
        {
                snprintf(buffer, sizeof(buffer), "%.3f", bme280.readTemperature());
#if DEBUG_OPENHAB_SENSOR_BME280
                debug_printf("openhab_sensor_bme280_update: Temperature: %s\r\n", buffer);
#endif
                openhab_sensor_connector_publish(cfg, cfg.item.openhab.sensors.bme280.items.temperature, buffer);
        }

        if (strlen(cfg.item.openhab.sensors.bme280.items.humidity) > 0)
        {
                snprintf(buffer, sizeof(buffer), "%.3f", bme280.readHumidity());
#if DEBUG_OPENHAB_SENSOR_BME280
                debug_printf("openhab_sensor_bme280_update: Humidity: %s\r\n", buffer);
#endif
                openhab_sensor_connector_publish(cfg, cfg.item.openhab.sensors.bme280.items.humidity, buffer);
        }

        if (strlen(cfg.item.openhab.sensors.bme280.items.pressure) > 0)
        {
                snprintf(buffer, sizeof(buffer), "%.3f", bme280.readPressure() / 100.0f);
#if DEBUG_OPENHAB_SENSOR_BME280
                debug_printf("openhab_sensor_bme280_update: Pressure: %s\r\n", buffer);
#endif
                openhab_sensor_connector_publish(cfg, cfg.item.openhab.sensors.bme280.items.pressure, buffer);
        }
}

#endif
