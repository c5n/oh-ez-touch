# Lanbon L8

Der Lanbon L8 vereint einen Wandschalter und ein Touchpanel in einem Gerät. Hinter dem Glas befinden sich ein 320x240-ST7789-Display, ein kapazitives FT5X06-Touchpanel, drei Netzspannungsrelais und ein RGB-„Stimmungslicht“.

- Build-Zielname: `lanbon`
- Defaults-Datei: `sdkconfig.defaults.lanbon`

## Pinbelegung

### Display

| Signal | GPIO |
| --- | --- |
| SCLK | 19 |
| MOSI | 23 |
| MISO | 25 |
| CS | 22 |
| DC | 21 |
| Reset | 18 |
| Hintergrundbeleuchtung | 5, aktiv high |

Diese Pins entsprechen keinem IOMUX-Host des ESP32. Die Signale werden über die GPIO-Matrix geführt. Der SPI-Master ist in dieser Konfiguration auf 40 MHz begrenzt. Die Firmware betreibt den Bus mit 40 MHz und protokolliert beim Start den tatsächlichen Takt.

### Touchpanel

Das FT5X06 befindet sich auf einem I2C-Bus mit 400 kHz: SDA an GPIO 4, SCL an GPIO 0.

Ein optionaler BME280 muss denselben Bus verwenden. Er kann nicht die üblichen Pins 33 und 32 verwenden. Auf diesem Board sind diese Pins die grünen und blauen Kanäle der RGB-LED. Ein Sensor mit Standardeinstellungen würde die LED als I2C-Bus ansteuern.

### Relais

| Relais | GPIO |
| --- | --- |
| 1 | 12 |
| 2 | 14 |
| 3 | 27 |

Die Relais sind aktiv high.

> **HINWEIS:** GPIO 12 ist MTDI, der Strapping-Pin, der beim Reset die Flash-Spannung auswählt. Der Pin wird nur während des Resets abgetastet. Die Firmware hält ihn beim Start auf low, wie es der Strapping-Pin erfordert.

> **HINWEIS:** In der alten PlatformIO-Umgebung hießen die Relais-Pins 12, 24 und 37. GPIO 24 gibt es auf einem ESP32 nicht. GPIO 37 ist nur ein Eingang. Die richtigen Pins sind 12, 14 und 27.

### Stimmungslicht

| Kanal | GPIO |
| --- | --- |
| Rot | 26 |
| Grün | 32 |
| Blau | 33 |

Das Licht besteht aus drei unabhängigen PWM-Kanälen und nicht aus einem einzelnen Farbkanal.

> **HINWEIS:** Neuere L8-Geräte steuern dasselbe Licht über einen WS2811 an GPIO 26 an. Bei diesen Geräten reagiert nur der rote Kanal.

## Kein Signalgeber

Dieses Board hat keinen Summer. Die Einstellungen des Signalgebers haben keine Wirkung.

## Relais und LEDs über MQTT

Die Relais und das Stimmungslicht werden über MQTT angesteuert. Die Benutzeroberfläche des Bedienfelds verwendet sie nicht. Die Relais sind wie auf der Wandplatte ab 1 nummeriert: `relay/1` bis `relay/3` bei einem L8-HS. Die LED-Topics heißen `led/red`, `led/green` und `led/blue`. Siehe [MQTT](../mqtt_de.md#relais-und-leds) für Topics und Payloads.

## Hochkantmontage

Mit der Einstellung Ausrichtung kann dieses Panel hochkant mit 240x320 betrieben werden. Das eigene Raster des FT5X06 ist bereits hochkant, daher entspricht die Touch-Zuordnung nahezu der Identität -- die Kombination mit den Spiegelungsflags des ST7789 wurde jedoch nur im Querformat auf dem Prüfstand verifiziert. Wenn Bild und Touch nicht übereinstimmen, liegt die Korrektur in `main/port/esp32/port_display.c` und `port_indev.c`.
