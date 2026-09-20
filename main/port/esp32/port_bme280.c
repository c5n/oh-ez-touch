/**
 * @file esp32/port_bme280.c
 *
 * A BME280 over IDF's i2c_master driver.
 *
 * In-tree rather than from the registry, and that is forced rather than
 * preferred: espressif/bme280 and esp-idf-lib/bmp280 both sit on the legacy
 * driver/i2c.h, while esp_lcd_new_panel_io_i2c() -- which the Lanbon's touch
 * panel needs -- dispatches to i2c_master. IDF catches the combination at
 * runtime ("CONFLICT! driver_ng is not allowed to be used with this old
 * driver"), so on that board the two cannot coexist. board_i2c.c owns the bus
 * they share.
 *
 * The compensation below is Bosch's, transcribed from the BME280 datasheet
 * (BST-BME280-DS002, section 4.2.3). It is fixed-point on purpose: the
 * published figures should match every other BME280 in the world to the last
 * digit, and a floating-point reimplementation would not.
 */
#include "port_bme280.h"

#include <string.h>

#include "board_i2c.h"
#include "board_pins.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "port_bme280";

/* 0x76 with SDO low, 0x77 with it high. Both are tried: which one a breakout
 * board presents is a pull-up resistor nobody documents. */
#define BME280_ADDR_PRIMARY   0x76
#define BME280_ADDR_SECONDARY 0x77

#define BME280_REG_CALIB_00   0x88 /* dig_T1 .. dig_P9, 26 bytes */
#define BME280_REG_ID         0xD0
#define BME280_REG_RESET      0xE0
#define BME280_REG_CALIB_26   0xE1 /* dig_H2 .. dig_H6, 7 bytes  */
#define BME280_REG_CTRL_HUM   0xF2
#define BME280_REG_STATUS     0xF3
#define BME280_REG_CTRL_MEAS  0xF4
#define BME280_REG_CONFIG     0xF5
#define BME280_REG_DATA       0xF7 /* pressure, temperature, humidity: 8 bytes */

#define BME280_CHIP_ID        0x60 /* 0x58 would be a BMP280: no humidity */

#define BME280_STATUS_MEASURING 0x08

/* Oversampling x1 on all three, filter off, forced mode: Bosch's "weather
 * monitoring" profile, and what the Adafruit configuration this replaces asked
 * for. One conversion takes under 10 ms. */
#define BME280_CTRL_HUM_X1    0x01
#define BME280_CTRL_MEAS_X1_FORCED 0x25 /* osrs_t=1 << 5 | osrs_p=1 << 2 | forced */
#define BME280_CONFIG_FILTER_OFF   0x00

#define BME280_I2C_TIMEOUT_MS 100

static i2c_master_dev_handle_t dev;

static struct
{
    uint16_t T1;
    int16_t  T2, T3;
    uint16_t P1;
    int16_t  P2, P3, P4, P5, P6, P7, P8, P9;
    uint8_t  H1;
    int16_t  H2;
    uint8_t  H3;
    int16_t  H4, H5;
    int8_t   H6;
} calib;

/* Carried between the temperature and the other two compensations, exactly as
 * in the datasheet: the pressure and humidity formulas need the fine
 * temperature reading. */
static int32_t t_fine;

static bool read_regs(uint8_t reg, uint8_t *buf, size_t len)
{
    return (i2c_master_transmit_receive(dev, &reg, 1, buf, len,
                                        BME280_I2C_TIMEOUT_MS) == ESP_OK);
}

static bool write_reg(uint8_t reg, uint8_t value)
{
    uint8_t frame[2] = { reg, value };

    return (i2c_master_transmit(dev, frame, sizeof(frame), BME280_I2C_TIMEOUT_MS) == ESP_OK);
}

static uint16_t u16_le(const uint8_t *p)
{
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static int16_t s16_le(const uint8_t *p)
{
    return (int16_t)u16_le(p);
}

static bool read_calibration(void)
{
    uint8_t a[26];
    uint8_t b[7];

    if (read_regs(BME280_REG_CALIB_00, a, sizeof(a)) == false)
        return false;

    if (read_regs(BME280_REG_CALIB_26, b, sizeof(b)) == false)
        return false;

    calib.T1 = u16_le(&a[0]);
    calib.T2 = s16_le(&a[2]);
    calib.T3 = s16_le(&a[4]);
    calib.P1 = u16_le(&a[6]);
    calib.P2 = s16_le(&a[8]);
    calib.P3 = s16_le(&a[10]);
    calib.P4 = s16_le(&a[12]);
    calib.P5 = s16_le(&a[14]);
    calib.P6 = s16_le(&a[16]);
    calib.P7 = s16_le(&a[18]);
    calib.P8 = s16_le(&a[20]);
    calib.P9 = s16_le(&a[22]);
    /* a[24] is reserved; dig_H1 is at 0xA1, which is a[25] in this block. */
    calib.H1 = a[25];

    calib.H2 = s16_le(&b[0]);
    calib.H3 = b[2];
    /* H4 and H5 are twelve bits each and share the nibbles of one byte. */
    calib.H4 = (int16_t)(((int8_t)b[3] << 4) | (b[4] & 0x0F));
    calib.H5 = (int16_t)(((int8_t)b[5] << 4) | (b[4] >> 4));
    calib.H6 = (int8_t)b[6];

    return true;
}

/* Datasheet 4.2.3, "compensate_T": hundredths of a degree Celsius. */
static int32_t compensate_temperature(int32_t adc_T)
{
    int32_t var1 = ((((adc_T >> 3) - ((int32_t)calib.T1 << 1))) * ((int32_t)calib.T2)) >> 11;
    int32_t var2 = (((((adc_T >> 4) - ((int32_t)calib.T1))
                      * ((adc_T >> 4) - ((int32_t)calib.T1))) >> 12)
                    * ((int32_t)calib.T3)) >> 14;

    t_fine = var1 + var2;

    return (t_fine * 5 + 128) >> 8;
}

/* Datasheet 4.2.3, "compensate_P", 64-bit variant: Pascal in Q24.8. */
static uint32_t compensate_pressure(int32_t adc_P)
{
    int64_t var1 = ((int64_t)t_fine) - 128000;
    int64_t var2 = var1 * var1 * (int64_t)calib.P6;

    var2 = var2 + ((var1 * (int64_t)calib.P5) << 17);
    var2 = var2 + (((int64_t)calib.P4) << 35);
    var1 = ((var1 * var1 * (int64_t)calib.P3) >> 8) + ((var1 * (int64_t)calib.P2) << 12);
    var1 = (((((int64_t)1) << 47) + var1)) * ((int64_t)calib.P1) >> 33;

    if (var1 == 0)
        return 0; /* the datasheet's own guard against dividing by zero */

    int64_t p = 1048576 - adc_P;

    p = (((p << 31) - var2) * 3125) / var1;
    var1 = (((int64_t)calib.P9) * (p >> 13) * (p >> 13)) >> 25;
    var2 = (((int64_t)calib.P8) * p) >> 19;
    p = ((p + var1 + var2) >> 8) + (((int64_t)calib.P7) << 4);

    return (uint32_t)p;
}

/* Datasheet 4.2.3, "compensate_H": %RH in Q22.10. */
static uint32_t compensate_humidity(int32_t adc_H)
{
    int32_t v = t_fine - ((int32_t)76800);

    v = (((((adc_H << 14) - (((int32_t)calib.H4) << 20) - (((int32_t)calib.H5) * v))
           + ((int32_t)16384)) >> 15)
         * (((((((v * ((int32_t)calib.H6)) >> 10)
                * (((v * ((int32_t)calib.H3)) >> 11) + ((int32_t)32768))) >> 10)
              + ((int32_t)2097152)) * ((int32_t)calib.H2) + 8192) >> 14));

    v = v - (((((v >> 15) * (v >> 15)) >> 7) * ((int32_t)calib.H1)) >> 4);

    if (v < 0)
        v = 0;
    if (v > 419430400)
        v = 419430400;

    return (uint32_t)(v >> 12);
}

static bool probe(uint8_t address)
{
    i2c_master_bus_handle_t bus = board_i2c_bus();

    if (bus == NULL)
        return false;

    i2c_device_config_t config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = address,
        .scl_speed_hz = 400 * 1000,
    };

    if (i2c_master_bus_add_device(bus, &config, &dev) != ESP_OK)
        return false;

    uint8_t id = 0;

    if (read_regs(BME280_REG_ID, &id, 1) == true && id == BME280_CHIP_ID)
        return true;

    if (id != 0)
        ESP_LOGW(TAG, "0x%02x answered with chip id 0x%02x, not a BME280", address, id);

    i2c_master_bus_rm_device(dev);
    dev = NULL;

    return false;
}

bool port_bme280_init(void)
{
    if (dev != NULL)
        return true;

    if (probe(BME280_ADDR_PRIMARY) == false && probe(BME280_ADDR_SECONDARY) == false)
    {
        ESP_LOGW(TAG, "no BME280 on SDA %d / SCL %d", OHEZ_I2C_PIN_SDA, OHEZ_I2C_PIN_SCL);
        return false;
    }

    if (read_calibration() == false)
    {
        ESP_LOGE(TAG, "cannot read the calibration data");
        i2c_master_bus_rm_device(dev);
        dev = NULL;
        return false;
    }

    /* ctrl_hum only takes effect when ctrl_meas is written afterwards, which is
     * the one ordering rule in the whole datasheet that is easy to get wrong.
     * ctrl_meas is written per measurement, so this is just the resting state. */
    write_reg(BME280_REG_CONFIG, BME280_CONFIG_FILTER_OFF);
    write_reg(BME280_REG_CTRL_HUM, BME280_CTRL_HUM_X1);

    ESP_LOGI(TAG, "BME280 found");

    return true;
}

bool port_bme280_read(float *temperature_c, float *humidity_pct, float *pressure_hpa)
{
    if (dev == NULL)
        return false;

    /* Re-written before every measurement: ctrl_hum is latched by this write,
     * and forced mode returns to sleep after one conversion. */
    if (   write_reg(BME280_REG_CTRL_HUM, BME280_CTRL_HUM_X1) == false
        || write_reg(BME280_REG_CTRL_MEAS, BME280_CTRL_MEAS_X1_FORCED) == false)
    {
        return false;
    }

    /* Under 10 ms at this oversampling. Bounded so that a sensor that never
     * clears the bit cannot hang the task it is polled from. */
    for (int i = 0; i < 10; i++)
    {
        uint8_t status = 0;

        vTaskDelay(pdMS_TO_TICKS(5));

        if (read_regs(BME280_REG_STATUS, &status, 1) == false)
            return false;

        if ((status & BME280_STATUS_MEASURING) == 0)
            break;
    }

    uint8_t data[8];

    if (read_regs(BME280_REG_DATA, data, sizeof(data)) == false)
        return false;

    int32_t adc_P = (int32_t)(((uint32_t)data[0] << 12) | ((uint32_t)data[1] << 4) | (data[2] >> 4));
    int32_t adc_T = (int32_t)(((uint32_t)data[3] << 12) | ((uint32_t)data[4] << 4) | (data[5] >> 4));
    int32_t adc_H = (int32_t)(((uint32_t)data[6] << 8) | data[7]);

    /* Temperature first, always: it is what sets t_fine. */
    int32_t temperature = compensate_temperature(adc_T);

    *temperature_c = (float)temperature / 100.0f;
    *pressure_hpa = (float)compensate_pressure(adc_P) / 25600.0f; /* Q24.8 Pa -> hPa */
    *humidity_pct = (float)compensate_humidity(adc_H) / 1024.0f;

    return true;
}
