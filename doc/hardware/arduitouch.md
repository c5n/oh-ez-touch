# ArduiTouch 2.4 inch

The ArduiTouch is a wall-mount kit. It has an ESP32 module, a 320x240 ILI9341
display, a resistive XPT2046 touch panel and a piezo beeper.

![ArduiTouch](../img/arduitouch_main.jpeg)

- Build target name: `arduitouch`
- Defaults file: `sdkconfig.defaults.arduitouch`
- This board is the Kconfig default. A build without a board defaults file
  builds for this board.

## Parts

- [ESP32 NodeMCU](https://www.az-delivery.de/products/esp32-developmentboard) or
  [ESP32 Dev Kit C V4](https://www.az-delivery.de/products/esp-32-dev-kit-c-v4).
  No SD card is necessary.
- [ArduiTouch kit](https://www.az-delivery.de/products/az-touch-wandgehauseset-mit-touchscreen-fur-esp8266-und-esp32)

Optional parts, option A (external power supply):

- DC socket, for example [this one](https://www.amazon.de/dp/B0975TSZRV)
- USB to TTL serial adapter for flashing. Recommended:
  [FT232-AZ USB to TTL adapter for 3.3 V and 5 V](https://www.az-delivery.de/products/ftdi-adapter-ft232rl)

Optional parts, option B (mains power):

- [Switch](https://www.amazon.de/dp/B0966WQRH6)
- [AC/DC transformer, 220 V to 5 V](https://www.az-delivery.de/products/copy-of-220v-zu-5v-mini-netzteil)

## Power supply

Connect the ArduiTouch to a power supply of 12 V, 300 mA or more.

## Pin map

The display and the touch controller share one SPI bus. SCLK 18, MOSI 23 and
MISO 19 are an exact IOMUX match for SPI3. The signals do not pass the GPIO
matrix.

| Signal | GPIO |
| --- | --- |
| Display SCLK | 18 |
| Display MOSI | 23 |
| Display MISO | 19 |
| Display CS | 5 |
| Display DC | 4 |
| Display reset | 22 |
| Touch CS | 14 (26 in the JTAG variant) |
| Backlight | 15 (16 in the JTAG variant), active low |
| Beeper | 21 |
| I2C SDA (optional BME280) | 33 |
| I2C SCL (optional BME280) | 32 |

The touch controller is a second device on the display bus. It has no bus pins
of its own. It reads MISO back over the same bus.

The display clock is 40 MHz. The IOMUX routing permits 80 MHz on the ESP32
side. An ILI9341 is already past its datasheet write cycle at 40 MHz. A test
on real hardware is necessary before 80 MHz can be used.

The 2.4 inch panel is mounted the other way up from the 2.8 inch panel. The
firmware flips the pointer coordinates end for end on this board.

The touch calibration constants are origin 275, span 3620 for X and origin
264, span 3532 for Y. The span values are spans, not maxima. See
`main/port/esp32/board_pins.h` for the details.

These are what an uncalibrated board of this type starts on. A resistive panel
varies per unit. If yours is out at the edges, calibrate it on the panel:
**System**, **Touch**, **Calibrate**. See
[Calibrating the touchscreen](../configuration.md#calibrating-the-touchscreen).
The constants in the header stay the fallback, and are what all four settings
at `0` mean.

## Beeper

This board has a piezo beeper on GPIO 21. See [The beeper](../beeper.md).

## JTAG variant

The file `sdkconfig.defaults.arduitouch_jtag` builds the same board with a
different pin map. It moves touch CS to GPIO 26 and the backlight to GPIO 16.
This keeps the two pins free that an attached esp-prog needs.

## Flashing with a UART adapter

The board has no USB port. Use a USB to TTL serial adapter.

1. Set the adapter to 5 V with the jumper.
2. Connect UART VCC to ESP32 5V.
3. Connect UART GND to ESP32 GND (pin 6, same row as 5V).
4. Connect UART RX to ESP32 TXD.
5. Connect UART TX to ESP32 RXD.
6. Connect ESP32 GND to ESP32 GPIO 0. This enables the flash mode.

Connect the adapter to the computer. A `ttyUSB` device appears, usually
`/dev/ttyUSB0`. To identify a newly connected adapter, run:

```bash
dmesg | grep /dev/ttyUSB
```

If your user account has no access rights for the device, use `sudo`, or add
[udev rules](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32/get-started/establish-serial-connection.html)
for user access.

One command uploads the bootloader, the partition table, the firmware and the
filesystem image:

```bash
idf.py -B build/arduitouch -p /dev/ttyUSB0 flash monitor
```

When the output shows `Connecting........_____`, press and hold the BOOT
button on the ESP32 until the upload starts.

> **NOTE:** In some cases the RST button works instead of the BOOT button.

The `monitor` argument is optional. It shows the serial log. Press `Ctrl-]` to
exit the monitor.

## Portrait mounting

The Orientation setting can run this board upright at 240x320. The panel
rotation (`port_display.c`) and the XPT2046 mapping (`port_indev.c`) have the
portrait paths, but they are bench-verified in landscape only: if the picture
or the touch comes out mirrored or upside down, the fix is the mirror flags
there -- both of them together, or the touch stops agreeing with the picture.
