/**
 * @file board_i2c.h
 *
 * The board's one I2C bus.
 *
 * There is exactly one because on the Lanbon L8 there has to be: the FT6336
 * touch panel and an optional BME280 are both on it, and IDF 5's i2c_master
 * driver refuses a second driver on the same port -- as does mixing it with
 * the legacy driver/i2c.h, which is why the BME280 driver here is written
 * against i2c_master rather than taken from the registry.
 *
 * On the ArduiTouch boards the touch panel is on SPI and the bus carries only
 * the sensor, but it is created the same way so that there is one answer to
 * "which pins is I2C on", in board_pins.h.
 */
#ifndef BOARD_I2C_H
#define BOARD_I2C_H

#include "driver/i2c_master.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * The bus, created on first use.
 *
 * @return the handle, or NULL if the bus could not be created -- which is
 *   fatal for the touch panel on the Lanbon and merely means "no sensor"
 *   elsewhere, so the decision is left to the caller.
 */
i2c_master_bus_handle_t board_i2c_bus(void);

#ifdef __cplusplus
}
#endif

#endif /* BOARD_I2C_H */
