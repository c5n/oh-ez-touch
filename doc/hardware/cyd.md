# Cheap Yellow Display (CYD)

The Cheap Yellow Display is the ESP32-2432S028R. It has a 320x240 ILI9341
display, a resistive XPT2046 touch panel, an RGB LED and a speaker connector.

- Build target name: `cyd`
- Defaults file: `sdkconfig.defaults.cyd`

## Pin map

### Display

The display sits on GPIO 14, 13 and 12. These pins are an exact IOMUX match
for SPI2.

| Signal | GPIO |
| --- | --- |
| SCLK | 14 |
| MOSI | 13 |
| MISO | 12 |
| CS | 15 |
| DC | 2 |
| Reset | 4 |
| Backlight | 21, active high |

The display clock is 40 MHz.

### Touch panel

The XPT2046 does not share the display bus on this board. It has its own SPI
bus:

| Signal | GPIO |
| --- | --- |
| SCLK | 25 |
| MOSI | 32 |
| MISO | 39 |
| CS | 33 |

These pins match neither IOMUX host. The signals route through the GPIO
matrix. This costs nothing at the 2 MHz an XPT2046 supports. The interrupt
pin (GPIO 36) is not used. The firmware polls the controller.

### Beeper

The board has a JST speaker connector on GPIO 26.

### I2C (optional BME280)

Use the P3 header: SDA on GPIO 27, SCL on GPIO 22, plus 3V3 and GND.

> **NOTE:** On some board revisions GPIO 21 is the third pin of the P3
> header. That pin is the display backlight here.

### RGB LED

| Channel | GPIO |
| --- | --- |
| Green | 16 |
| Blue | 17 |

The LED channels are active low. The red channel is not available. Red is on
GPIO 4, which is also the display reset line. Driving it as an LED would hold
the panel in reset. The board answers `led/green` and `led/blue` over MQTT.
See [MQTT](../mqtt.md#relays-and-leds).

## Touch calibration

A resistive panel varies per unit. The firmware uses the ranges the published
CYD examples agree on: raw X runs roughly 240 to 3860, raw Y roughly 200 to
3900. If a board is a few pixels off at the edges, adjust the constants in
`main/port/esp32/board_pins.h`.

## Portrait mounting

The Orientation setting can run this board upright at 240x320. The panel
rotation (`port_display.c`) and the XPT2046 mapping (`port_indev.c`) have the
portrait paths, but they are bench-verified in landscape only. The
calibration spans above stay with the axes they were measured on; a mirrored
or upside-down result is fixed with the mirror and flip flags in those two
files, not with the calibration numbers.
