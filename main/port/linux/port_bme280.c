/**
 * @file linux/port_bme280.c
 *
 * No I2C bus on a desktop, and no sensor on it.
 *
 * Reporting "there is none" rather than inventing a plausible 21.5 degrees:
 * the readings are published to openHAB, and a simulator that quietly writes
 * fiction into someone's item history would be worse than one that does
 * nothing.
 */
#include "port_bme280.h"

bool port_bme280_init(void)
{
    return false;
}

bool port_bme280_read(float *temperature_c, float *humidity_pct, float *pressure_hpa)
{
    (void)temperature_c;
    (void)humidity_pct;
    (void)pressure_hpa;

    return false;
}
