#include "openhab_sensor_main.hpp"

#include "debug.h"
#include "openhab_sensor_bme280.hpp"

#include "mqtt/ohez_mqtt.hpp"
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
        float temperature = 0.0f;
        float humidity = 0.0f;
        float pressure = 0.0f;

#if CONFIG_OHEZ_DEBUG_OPENHAB_SENSOR_MAIN
        printf("openhab_sensor_main_loop: refreshing bme280\r\n");
#endif
        bme280_refresh_timeout = port_millis() + config.item.openhab.sensors.bme280.interval * 1000;

        if (openhab_sensor_bme280_read(&temperature, &humidity, &pressure) == false)
            return;

        /* The fan-out, and the reason the read is separate from the publish:
         * one measurement, two sinks. This is also the whole of the coupling
         * between the sensors and MQTT -- the client is told about a reading, it
         * does not go looking for one, so enabling the sensor still means the
         * one setting it always did.
         *
         * Whether either sink does anything with it is its own business: the
         * openHAB one skips an item name left blank, and the MQTT one returns
         * at once when there is no broker connection. */
        openhab_sensor_bme280_publish(config, temperature, humidity, pressure);
        ohez_mqtt_publish_bme280(temperature, humidity, pressure);
    }
}
