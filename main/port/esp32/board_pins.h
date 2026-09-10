/**
 * @file board_pins.h
 *
 * What each supported panel is wired like. One table, selected by the Kconfig
 * board choice; this is the whole of what the four PlatformIO environments
 * differed in.
 *
 * Two of the flags they carried are deliberately not reproduced:
 *
 *   TFT_RD=2 / TFT_WR=4 (ArduiTouch28 and the JTAG environment) are TFT_eSPI
 *     parallel-bus settings with no meaning on an SPI panel. TFT_WR=4 even
 *     collided with TFT_DC=4, which is how one can tell they were never read.
 *
 *   SPI_FREQUENCY=80000000 (Lanbon) is out of spec on that board. Its
 *     SCLK/MOSI/MISO (19/23/25) match neither SPI host's IOMUX pins, so the
 *     signals route through the GPIO matrix, and the ESP32's SPI master is
 *     limited to 40 MHz that way. It runs at 40 MHz here, and the actual clock
 *     is logged at startup.
 */
#ifndef BOARD_PINS_H
#define BOARD_PINS_H

#include "sdkconfig.h"

/* SCLK 18 / MOSI 23 / MISO 19 on the ArduiTouch boards is an exact IOMUX match
 * for VSPI, which is SPI3_HOST -- no GPIO matrix, no signal delay. The Lanbon
 * matches neither host and uses the same one for want of a reason not to. */
#define OHEZ_LCD_SPI_HOST           SPI3_HOST

/* ---------------------------------------------------------------- ArduiTouch */
#if defined(CONFIG_OHEZ_BOARD_ARDUITOUCH) || defined(CONFIG_OHEZ_BOARD_ARDUITOUCH28)

#define OHEZ_PANEL_ILI9341          1
#define OHEZ_PANEL_ST7789           0

#define OHEZ_LCD_PIN_SCLK           18
#define OHEZ_LCD_PIN_MOSI           23
#define OHEZ_LCD_PIN_MISO           19   /* read back over the same bus by the XPT2046 */
#define OHEZ_LCD_PIN_CS              5
#define OHEZ_LCD_PIN_DC              4
#define OHEZ_LCD_PIN_RST            22
#define OHEZ_LCD_PIXEL_CLOCK_HZ     (40 * 1000 * 1000)

#define OHEZ_TOUCH_XPT2046          1
#define OHEZ_TOUCH_FT5X06           0

#ifdef CONFIG_OHEZ_ARDUITOUCH_JTAG_PINS
#define OHEZ_TOUCH_PIN_CS           26
#define OHEZ_BACKLIGHT_PIN          16
#else
#define OHEZ_TOUCH_PIN_CS           14
#define OHEZ_BACKLIGHT_PIN          15
#endif

/* Active low: 100 % brightness drove the pin to duty 0 under TFT_eSPI. */
#define OHEZ_BACKLIGHT_ACTIVE_LOW   1

#define OHEZ_HAS_BEEPER             1
#define OHEZ_BEEPER_PIN             21

/* Nothing on the bus but an optional BME280, on the pins that driver has
 * always defaulted to. */
#define OHEZ_I2C_PIN_SDA            33
#define OHEZ_I2C_PIN_SCL            32

/* No relays and no mood light on these boards: OHEZ_RELAY_PINS and
 * OHEZ_LED_PINS are left undefined, which is what port_relay.c and port_led.c
 * compile down to nothing on. */

/* The 2.4" and the 2.8" panel are mounted the other way up from each other, so
 * only one of them needs the pointer flipped end for end. */
#ifdef CONFIG_OHEZ_BOARD_ARDUITOUCH
#define OHEZ_TOUCH_FLIP             1
#else
#define OHEZ_TOUCH_FLIP             0
#endif

/* -------------------------------------------------------------------- Lanbon */
#elif defined(CONFIG_OHEZ_BOARD_LANBON)

#define OHEZ_PANEL_ILI9341          0
#define OHEZ_PANEL_ST7789           1

#define OHEZ_LCD_PIN_SCLK           19
#define OHEZ_LCD_PIN_MOSI           23
#define OHEZ_LCD_PIN_MISO           25
#define OHEZ_LCD_PIN_CS             22
#define OHEZ_LCD_PIN_DC             21
#define OHEZ_LCD_PIN_RST            18
#define OHEZ_LCD_PIXEL_CLOCK_HZ     (40 * 1000 * 1000)

#define OHEZ_TOUCH_XPT2046          0
#define OHEZ_TOUCH_FT5X06           1
#define OHEZ_TOUCH_I2C_HZ           (400 * 1000)
#define OHEZ_TOUCH_FLIP             0

/* The touch panel is on this bus, and an optional BME280 has to share it. It
 * cannot have the 33/32 the sensor driver defaults to: on this board those are
 * the green and blue channels of the RGB LED, so a BME280 configured with the
 * defaults would drive the LED as an I2C bus. */
#define OHEZ_I2C_PIN_SDA             4
#define OHEZ_I2C_PIN_SCL             0

#define OHEZ_BACKLIGHT_PIN           5
#define OHEZ_BACKLIGHT_ACTIVE_LOW    0

/* No buzzer on this board. */
#define OHEZ_HAS_BEEPER              0

/* The three relays of the L8-HS, and the RGB "mood light" behind the glass.
 * Both are outputs and nothing else: no switch reads back, and the panel's own
 * UI never touches them -- they are driven over MQTT, which is what
 * peripherals/relay.cpp and peripherals/led.cpp are for.
 *
 * The relay pins are 12, 14 and 27, not the 12, 24 and 37 the PlatformIO
 * environment carried. Those two were impossible on this chip: there is no
 * GPIO 24 on an ESP32 at all, and GPIO 37 is input-only and not bonded out on
 * a WROOM module. Nothing ever read the flags -- they were defined in
 * platformio.ini and used by no source file -- so the mistake had no symptom
 * to be found by. 14 and 27 are what the hardware actually uses.
 *
 * GPIO 12 is MTDI, the strapping pin that selects the flash voltage at reset.
 * It is only sampled during reset, so driving it afterwards is fine, and it is
 * left low here at boot, which is the level that strapping wants anyway.
 *
 * The mood light is three separate PWM channels rather than one colour: the
 * hardware is three LEDs, and giving each its own topic lets a broker mix
 * whatever it likes without this firmware having an opinion about colour.
 * Newer L8 units drive the same light through a WS2811 on GPIO 26 instead, and
 * those will see red respond and the other two do nothing. */
#define OHEZ_RELAY_PINS             { 12, 14, 27 }
#define OHEZ_RELAY_ACTIVE_LOW       0

#define OHEZ_LED_PINS               { 26, 32, 33 }
#define OHEZ_LED_NAMES              { "red", "green", "blue" }
#define OHEZ_LED_ACTIVE_LOW         0

#else
#error "No board selected. Run idf.py menuconfig -> OhEzTouch -> Target board."
#endif

#endif /* BOARD_PINS_H */
