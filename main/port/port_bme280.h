/**
 * @file port_bme280.h
 *
 * The optional BME280 temperature, humidity and pressure sensor.
 *
 * A port rather than a driver call because the simulator has no I2C bus and no
 * sensor on it; what sits above this -- which items to publish the readings to,
 * and how often -- is shared.
 */
#ifndef PORT_BME280_H
#define PORT_BME280_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Find the sensor and configure it.
 *
 * @return false when there is none: no bus, nothing answering at either
 *   address, or a chip id that is not a BME280. The caller stops there rather
 *   than publishing invented readings.
 */
bool port_bme280_init(void);

/**
 * Take one measurement.
 *
 * Forced mode -- the sensor sleeps between readings and is woken for each one,
 * which is Bosch's recommendation for weather monitoring and what the Adafruit
 * configuration here asked for.
 *
 * @return false if the sensor did not answer; the outputs are then untouched.
 */
bool port_bme280_read(float *temperature_c, float *humidity_pct, float *pressure_hpa);

#ifdef __cplusplus
}
#endif

#endif /* PORT_BME280_H */
