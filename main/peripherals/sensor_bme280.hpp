#ifndef SENSOR_BME280_HPP
#define SENSOR_BME280_HPP

/* False when there is no sensor: no bus, nothing answering, or a chip that
 * is not a BME280. Nothing is published in that case. */
bool sensor_bme280_setup();

/**
 * Take one reading.
 *
 * Separate from publishing it because the sensor and the sink know nothing
 * about each other: this file owns the chip, and peripherals/sensor_main.cpp is
 * the one place that knows a reading has just been taken and hands it to MQTT.
 *
 * @return false if the sensor did not answer; the outputs are then untouched.
 */
bool sensor_bme280_read(float *temperature_c, float *humidity_pct, float *pressure_hpa);

#endif
