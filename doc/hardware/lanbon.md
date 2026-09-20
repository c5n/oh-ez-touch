# Lanbon L8

The Lanbon L8 is a wall switch and a touch panel in one device. Behind the
glass it has a 320x240 ST7789 display, a capacitive FT5X06 touch panel, three
mains relays and an RGB "mood light".

- Build target name: `lanbon`
- Defaults file: `sdkconfig.defaults.lanbon`

## Pin map

### Display

| Signal | GPIO |
| --- | --- |
| SCLK | 19 |
| MOSI | 23 |
| MISO | 25 |
| CS | 22 |
| DC | 21 |
| Reset | 18 |
| Backlight | 5, active high |

These pins match neither IOMUX host of the ESP32. The signals route through
the GPIO matrix. The SPI master is limited to 40 MHz in this configuration.
The firmware runs the bus at 40 MHz and logs the actual clock at startup.

### Touch panel

The FT5X06 is on an I2C bus at 400 kHz: SDA on GPIO 4, SCL on GPIO 0.

An optional BME280 must share this bus. It cannot use the usual pins 33 and
32. On this board those pins are the green and blue channels of the RGB LED.
A sensor configured with the defaults would drive the LED as an I2C bus.

### Relays

| Relay | GPIO |
| --- | --- |
| 1 | 12 |
| 2 | 14 |
| 3 | 27 |

The relays are active high.

> **NOTE:** GPIO 12 is MTDI, the strapping pin that selects the flash voltage
> at reset. The pin is only sampled during reset. The firmware keeps it low at
> boot, which is the level the strapping wants.

> **NOTE:** The old PlatformIO environment named the relay pins 12, 24 and 37.
> GPIO 24 does not exist on an ESP32. GPIO 37 is input-only. The correct pins
> are 12, 14 and 27.

### Mood light

| Channel | GPIO |
| --- | --- |
| Red | 26 |
| Green | 32 |
| Blue | 33 |

The light is three independent PWM channels, not one colour channel.

> **NOTE:** Newer L8 units drive the same light through a WS2811 on GPIO 26.
> On those units only the red channel responds.

## No beeper

This board has no buzzer. The beeper settings have no effect.

## Relays and LEDs over MQTT

The relays and the mood light are driven over MQTT. The panel UI does not use
them. The relays are numbered from 1, as on the wall plate: `relay/1` to
`relay/3` on an L8-HS. The LED topics are named: `led/red`, `led/green` and
`led/blue`. See [MQTT](../mqtt.md#relays-and-leds) for the topics and
payloads.
