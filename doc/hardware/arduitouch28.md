# ArduiTouch 2.8 inch

The ArduiTouch 2.8 inch is the larger variant of the [ArduiTouch
2.4 inch](arduitouch.md). It has the same ESP32 module, the same ILI9341
display controller, the same XPT2046 touch controller and the same beeper.

- Build target name: `arduitouch28`
- Defaults file: `sdkconfig.defaults.arduitouch28`

## Differences from the 2.4 inch board

- Display size: 2.8 inch instead of 2.4 inch. The resolution stays 320x240.
- The panel is mounted the other way up. The firmware does not flip the
  pointer coordinates on this board.

## Wiring, flashing and power supply

The pin map, the UART flashing procedure and the power supply are the same as
for the 2.4 inch board. See [ArduiTouch 2.4 inch](arduitouch.md).
