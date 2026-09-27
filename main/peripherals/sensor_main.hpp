#ifndef SENSOR_MAIN_HPP
#define SENSOR_MAIN_HPP

#include "config/config.hpp"

void sensor_main_setup(Config &config);
void sensor_main_loop(Config &config);

/* Whether the BME280 is being polled: the Sensors setting on, and a chip that
 * answered at boot. The Systeminfo page asks before it takes a reading for
 * display, so it never runs a bus the configuration left dark. */
bool sensor_main_bme280_active(void);

#endif
