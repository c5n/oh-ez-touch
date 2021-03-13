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
        bme280.takeForcedMeasurement();

        if (strlen(cfg.item.openhab.sensors.bme280.items.temperature) > 0)
        {
                float temperature = bme280.readTemperature();
#if DEBUG_OPENHAB_SENSOR_BME280
                Serial.printf("openhab_sensor_bme280_update: Temperature: %f\r\n", temperature);
#endif
                openhab_sensor_connector_publish(cfg, cfg.item.openhab.sensors.bme280.items.temperature, String(temperature));
        }

        if (strlen(cfg.item.openhab.sensors.bme280.items.humidity) > 0)
        {
                float humidity = bme280.readHumidity();
#if DEBUG_OPENHAB_SENSOR_BME280
                Serial.printf("openhab_sensor_bme280_update: Humidity: %f\r\n", humidity);
#endif
                openhab_sensor_connector_publish(cfg, cfg.item.openhab.sensors.bme280.items.humidity, String(humidity));
        }

        if (strlen(cfg.item.openhab.sensors.bme280.items.pressure) > 0)
        {
                float pressure = bme280.readPressure() / 100.0f;
#if DEBUG_OPENHAB_SENSOR_BME280
                Serial.printf("openhab_sensor_bme280_update: Pressure: %f\r\n", pressure);
#endif
                openhab_sensor_connector_publish(cfg, cfg.item.openhab.sensors.bme280.items.pressure, String(pressure));
        }
}

#endif
