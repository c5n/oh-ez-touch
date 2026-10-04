# ArduiTouch 2,8 Zoll

Das ArduiTouch 2,8 Zoll ist die größere Variante des [ArduiTouch
2,4 Zoll](arduitouch_de.md). Es besitzt dasselbe ESP32-Modul, denselben
ILI9341-Display-Controller, denselben XPT2046-Touch-Controller und denselben
Signalgeber.

- Build-Zielname: `arduitouch28`
- Defaults-Datei: `sdkconfig.defaults.arduitouch28`

## Unterschiede zum 2,4-Zoll-Board

- Displaygröße: 2,8 Zoll statt 2,4 Zoll. Die Auflösung bleibt 320x240.
- Das Panel ist anders herum montiert. Die Firmware dreht auf diesem Board die
  Zeigerkoordinaten nicht um.

## Verkabelung, Flashen und Stromversorgung

Pinbelegung, UART-Flash-Vorgehen und Stromversorgung entsprechen dem
2,4-Zoll-Board. Siehe [ArduiTouch 2,4 Zoll](arduitouch_de.md).

## Hochkant-Montage

Wie beim 2,4-Zoll-Board, mit einem zusätzlichen Hinweis für dieses Board:
Sein Panel ist bereits anders herum montiert (siehe oben). Welche Kante bei der
Hochkantmontage als „oben“ gilt, muss daher am Prüfstand geklärt werden; die
Antwort lässt sich mit denselben beiden Dateien ermitteln.
