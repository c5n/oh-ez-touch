#ifndef OPENHAB_SENSOR_BME280_HPP
#define OPENHAB_SENSOR_BME280_HPP

#include "config/config.hpp"

/* False when there is no sensor: no bus, nothing answering, or a chip that
 * is not a BME280. Nothing is published in that case. */
bool openhab_sensor_bme280_setup();

/**
 * Take one reading.
 *
 * Separate from publishing it because there is more than one place a reading
 * goes now -- openHAB's REST API and the MQTT broker -- and a sensor read twice
 * so that each sink can have its own copy would give the two of them different
 * numbers for the same moment. openhab_sensor_main.cpp reads once and hands the
 * result to both.
 *
 * @return false if the sensor did not answer; the outputs are then untouched.
 */
bool openhab_sensor_bme280_read(float *temperature_c, float *humidity_pct, float *pressure_hpa);

/** POST one reading to the three configured openHAB items. An item left unset
 * in the settings is skipped. */
void openhab_sensor_bme280_publish(Config &cfg, float temperature_c, float humidity_pct,
                                   float pressure_hpa);

#endif
