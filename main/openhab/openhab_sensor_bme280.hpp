#ifndef OPENHAB_SENSOR_BME280_HPP
#define OPENHAB_SENSOR_BME280_HPP

#include "config/config.hpp"

/* False when there is no sensor: no bus, nothing answering, or a chip that
 * is not a BME280. Nothing is published in that case. */
bool openhab_sensor_bme280_setup();
void openhab_sensor_bme280_update(Config &cfg);

#endif
