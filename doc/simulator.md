# Simulator

The user interface can be built and run on the development machine in an SDL2
window. No hardware is necessary. The simulator is the same source code, the
same LVGL version, the same `lv_conf.h`, the same fonts and the same styles.
Only the files in `main/port/` differ. The simulator fetches the sitemap over
HTTP, serves its web interface and stores its settings, as the panel does.

## Build and run

```bash
idf.py -B build/linux -DSDKCONFIG=build/linux/sdkconfig \
       -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.linux" \
       --preview set-target linux
idf.py -B build/linux build
./build/linux/oh-ez-touch.elf
```

An "OhEzTouch" window opens. It shows a 320x240 screen at double size
(`port_display_init()` in `main/port/linux/port_display.c`). Mouse clicks act
as touch input. Closing the window ends the program.

## Differences from the panel

- **openHAB**: set the server with the config file, or with
  `OHEZ_OPENHAB_HOST`, `OHEZ_OPENHAB_PORT` and `OHEZ_SITEMAP`. Operating a
  widget POSTs a command, as on the panel.
- **Web interface**: the simulator uses port 8780, not port 80. An
  unprivileged process cannot bind port 80. `OHEZ_WEBUI_PORT` overrides the
  port.
- **Settings**: stored in `$XDG_CONFIG_HOME/oh-ez-touch/config.json` (or
  `~/.config/oh-ez-touch/config.json`). The file can be edited by hand.
  `OHEZ_CONFIG_DIR` moves it.
- **WLAN credentials**: stored in an emulated NVS image under
  `$XDG_STATE_HOME/oh-ez-touch/flash.bin`. `OHEZ_STATE_DIR` moves it. Two
  simulator instances cannot share one image. The second instance reports the
  conflict instead of corrupting the image.
- **No radio, no backlight, no sensor and no OTA.** These features report
  "there is none". The WLAN tab's **Scan** answers from a canned list. The
  sensor publishes nothing. `POST /update` answers 501.
- **The beeper works.** The simulator synthesizes the pulse train of the
  panel's PWM channel and plays it through SDL. See
  [Hearing the panel](#hearing-the-panel).

## Offline mode

`OHEZ_OFFLINE=1` serves the sitemap and the widget icons from compiled-in
fixtures instead of the network:

```bash
OHEZ_OFFLINE=1 ./build/linux/oh-ez-touch.elf
```

This gives a stable, reproducible screen. It lets you work on the UI without
an openHAB server.

The fixture in `main/sim/sitemap_fixture.cpp` is a small demo sitemap. It has
a home page with two sub pages and covers every supported widget type. It
goes through the same parser as a real server response. Edit that file to
reproduce a particular sitemap. Item states are read from the fixture and are
not written back. Operating a widget changes the state locally only.

### Widget icons

The widget icons are not part of this repository. The openHAB classic icon
set is licensed under the EPL-2.0. This project is GPL-3.0. Fetch the icons
once into your working copy:

```bash
tools/fetch_sim_icons.py
```

The script downloads the icons that the demo sitemap uses, rasterizes them
and writes `main/sim/icon_fixture_data.h`. That file is ignored by git. The
script needs network access and one of `inkscape`, `rsvg-convert` or
ImageMagick. Without the icons, offline mode draws the widgets without icons.
The firmware does the same when an icon request fails.

To look at the icon set itself, use `tools/fetch_openhab_icons.py`:

```bash
tools/fetch_openhab_icons.py                    # all icons, 32x32, into openhab-icons/
tools/fetch_openhab_icons.py --size 64          # bigger
tools/fetch_openhab_icons.py light heating      # named icons and their state variants
```

State variants land next to their default (`light.png`, `light-on.png`,
`light-off.png`), the way openHAB serves them. The output directory is
ignored by git for the same licensing reason.

## Environment overrides

The `OHEZ_*` variables are applied after the config file. The config file can
be inspected without being edited. The variables also work on the device,
which simply has no environment to read them from.

```bash
OHEZ_THEME=lcars ./build/linux/oh-ez-touch.elf
OHEZ_THEME=jarvis OHEZ_NIGHT=on ./build/linux/oh-ez-touch.elf
```

`OHEZ_THEME` takes `material`, `lcars`, `jarvis` or `classic`. `OHEZ_NIGHT`
takes `off`, `on` or `auto`. An unrecognized value selects the default.
`OHEZ_NIGHT_FROM` and `OHEZ_NIGHT_TO` set the hours of the `auto` window
(22 and 6 by default).

The clock follows the *configured* GMT offset, not the host's timezone. That
is what the panel would show. `OHEZ_NIGHT=auto` can be watched crossing its
boundary.

`OHEZ_MQTT`, `OHEZ_MQTT_HOST`, `OHEZ_MQTT_PORT` and `OHEZ_MQTT_TOPIC` point
the MQTT client at a broker for one run:

```bash
OHEZ_MQTT=on OHEZ_MQTT_HOST=localhost ./build/linux/oh-ez-touch.elf
```

`OHEZ_MQTT` takes `on` or `off`. Anything that is not `off` or `0` enables
it.

`OHEZ_BLE_FIXTURE=1` serves four compiled-in BLE advertisements instead of
real Bluetooth: an iBeacon, an Eddystone-UID, an Eddystone-TLM frame from the
same advertiser, and a plain named device. They go through the same parsers
as a real advertisement:

```bash
OHEZ_BLE_FIXTURE=1 ./build/linux/oh-ez-touch.elf
```

The fixture is off by default. The readings are published to a broker. A
simulator that wrote invented data into a presence history would be worse
than one that does nothing.

`OHEZ_OUTPUTS=1` gives the simulator three relays and a three-channel mood
light. They exist only as log lines. This is enough to exercise the MQTT
topics against a real broker on a desktop.

`OHEZ_SETTINGS` opens the settings screen at boot, on the named page. The
value is a section (`theme`, `audio`, `info`, `wlan`, `openhab`, `mqtt`,
`sensors`, `device`, `time`) or a menu (`settings`, also `index`, and
`system`):

```bash
OHEZ_SETTINGS=wlan ./build/linux/oh-ez-touch.elf
```

`OHEZ_ITEM` opens one control. The value is a dot-separated path of tile
indices. Every step but the last follows that tile's linked page. The last
step opens that tile's control. `5` opens the sixth tile of the home page.
`0.4` follows the first tile and opens the fifth tile of the page behind it:

```bash
OHEZ_ITEM=0.4 OHEZ_THEME=lcars OHEZ_NIGHT=on ./build/linux/oh-ez-touch.elf
```

Touching the status bar opens the settings screen in the simulator too. On
the host there is no radio to configure, so the screen never opens on its own
as it does on a new device.

## Hearing the panel

The simulator makes sound. See [The beeper](beeper.md) for what it makes.
`main/port/linux/port_beeper.c` opens an SDL audio device and synthesizes the
pulse train that the panel's LEDC channel would produce. There is no separate
renderer and no second copy of the synthesis.

The simulator does not follow the driver's `port_beeper_tone()` calls. It
takes the whole chime and walks the selected engine's frame function itself
at the sample rate. The reason: this target's FreeRTOS tick is 4 ms. A
two-millisecond interleave slot cannot be honoured from a task here.
Following the calls would render every chord four times coarser than the
panel plays it.

The engine's *parameters* (swept pitch, envelope, the two LFOs) are
re-evaluated on the engine's own five-millisecond grid, not per sample. Only
the oscillator runs faster.

The oscillator is sampled eight times per output sample and averaged. Without
that, harmonics above half the sample rate fold back to unrelated
frequencies. The panel has no such defect: LEDC drives the pin with a real
square wave and nothing is sampled.

The beeper must be enabled in the settings, as on the panel.
`SDL_AUDIODRIVER=disk` with `SDL_DISKAUDIOFILE` writes the raw stream to a
file instead of playing it.

**What it will tell you**: rhythm, contour, intervals, whether two chimes are
confusable, whether one outstays the gesture it answers, whether the voices
of a chord clash, and how audible the interleaving grain is.

**What it will not tell you is how loud anything is.** The ArduiTouch's
transducer has a sharp mechanical resonance around 2-4 kHz. Nothing models
that, the ringing after the drive stops, the case, or LEDC's frequency
quantization. Judge structure in the simulator. Judge loudness on a panel.

## Driving it from a script

The simulator listens on **127.0.0.1:8781** for commands: send a tap or a
drag, ask what is on screen, read the telemetry, pull the framebuffer.

```bash
OHEZ_OFFLINE=1 ./build/linux/oh-ez-touch.elf &
tools/ohez_ctl.py wait-page
tools/ohez_ctl.py tap-label "Hallway Dimmer"
tools/ohez_ctl.py shot /tmp/panel.png --scale 2
```

`tools/ohez_ctl.py` is the client. It needs nothing but Python 3. The screen
comes back as JSON: which screen is up, and each tile's label, state, type
and rectangle. A check can assert on a value instead of pixels. A tile can be
tapped by its label instead of coordinates.

Screenshots travel as raw pixels and become a PNG on the host. The panel
never encodes a PNG, because the hardware has no RAM to spare for an encoder.

`OHEZ_TESTIF=0` turns the socket off. `OHEZ_TESTIF_PORT` moves it. None of
this is compiled into a panel's firmware.

**[doc/test-interface.md](test-interface.md) is the full reference**: the
command table, the JSON, the framebuffer header and the gotchas.

## Testing against a real openHAB

The compiled-in fixtures draw a screen without a server. They cannot say what
openHAB really sends. `test/openhab/` holds sitemaps and items for a real
server. See [doc/openhab-fixtures.md](openhab-fixtures.md) for the
installation and the details.
