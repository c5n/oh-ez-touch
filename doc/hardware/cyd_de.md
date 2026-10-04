# Cheap Yellow Display (CYD)

Das Cheap Yellow Display ist das ESP32-2432S028R. Es verfügt über ein 320x240-ILI9341-Display, ein resistives XPT2046-Touchpanel, eine RGB-LED und einen Lautsprecheranschluss.

- Build-Zielname: `cyd`
- Defaults-Datei: `sdkconfig.defaults.cyd`

## Pinbelegung

### Display

Das Display ist mit GPIO 14, 13 und 12 verbunden. Diese Pins entsprechen exakt dem IOMUX für SPI2.

| Signal | GPIO |
| --- | --- |
| SCLK | 14 |
| MOSI | 13 |
| MISO | 12 |
| CS | 15 |
| DC | 2 |
| Reset | 4 |
| Hintergrundbeleuchtung | 21, aktiv high |

Der Displaytakt beträgt 40 MHz.

### Touchpanel

Das XPT2046 nutzt auf diesem Board nicht denselben Bus wie das Display. Es hat einen eigenen SPI-Bus:

| Signal | GPIO |
| --- | --- |
| SCLK | 25 |
| MOSI | 32 |
| MISO | 39 |
| CS | 33 |

Diese Pins entsprechen keinem IOMUX-Host. Die Signale werden über die GPIO-Matrix geführt. Bei den vom XPT2046 unterstützten 2 MHz kostet das nichts. Der Interrupt-Pin (GPIO 36) wird nicht verwendet. Die Firmware fragt den Controller regelmäßig ab.

### Signalgeber

Das Board hat einen JST-Lautsprecheranschluss an GPIO 26.

### I2C (optionaler BME280)

Verwenden Sie den P3-Header: SDA an GPIO 27, SCL an GPIO 22, dazu 3V3 und GND.

> **HINWEIS:** Bei manchen Board-Revisionen ist GPIO 21 der dritte Pin des P3-Headers. Dieser Pin dient hier als Display-Hintergrundbeleuchtung.

### RGB-LED

| Kanal | GPIO |
| --- | --- |
| Grün | 16 |
| Blau | 17 |

Die LED-Kanäle sind aktiv low. Der rote Kanal ist nicht verfügbar. Rot liegt an GPIO 4, der auch die Reset-Leitung des Displays ist. Eine Ansteuerung als LED würde das Panel im Reset halten. Das Board unterstützt `led/green` und `led/blue` über MQTT. Siehe [MQTT](../mqtt_de.md#relais-und-leds).

## Touch-Kalibrierung

Resistive Panels unterscheiden sich von Gerät zu Gerät. Die Firmware verwendet die Bereiche, auf die sich die veröffentlichten CYD-Beispiele einigen: Roh-X liegt ungefähr zwischen 240 und 3860, Roh-Y ungefähr zwischen 200 und 3900.

Wenn ein Board an den Rändern um einige Pixel abweicht, kalibrieren Sie es am Panel: **System**, **Touch**, **Kalibrieren**. Drücken Sie die vier Ecken; das Ergebnis zeigt an, was geändert wird, bevor die Änderung übernommen wird. Siehe [Touchscreen kalibrieren](../configuration_de.md#touchscreen-kalibrieren). Die Konstanten in `main/port/esp32/board_pins.h` bleiben der Fallback und werden durch den Wert `0` in allen vier Einstellungen verwendet.

## Hochkantmontage

Mit der Einstellung Ausrichtung kann dieses Board hochkant mit 240x320 betrieben werden. Für die Panelrotation (`port_display.c`) und die XPT2046-Zuordnung (`port_indev.c`) sind Hochkantpfade vorhanden, sie wurden jedoch nur im Querformat auf dem Prüfstand verifiziert. Die oben angegebenen Kalibrierungsspannen bleiben den Achsen zugeordnet, auf denen sie gemessen wurden; ein gespiegeltes oder auf dem Kopf stehendes Ergebnis wird mit den Spiegelungs- und Flip-Flags in diesen beiden Dateien korrigiert, nicht mit den Kalibrierungswerten.
