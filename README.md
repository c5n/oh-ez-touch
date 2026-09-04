# OhEzTouch

## Introduction

![arduitouch_main](doc/img/arduitouch_main.jpeg)

OhEzTouch is a simple always-on touch control device for home automation systems driven by [OpenHAB](https://www.openhab.org/).

The touch buttons and graphics are dynamically generated. The structure is defined by an individual OpenHAB sitemap on the server side.

## Partlist

- [ESP32 NodeMCU](https://www.az-delivery.de/products/esp32-developmentboard) or [ESP32 Dev Kit C V4](https://www.az-delivery.de/products/esp-32-dev-kit-c-v4), /for both, no SDCard is needed)
- [ArduiTouch](https://www.az-delivery.de/products/az-touch-wandgehauseset-mit-touchscreen-fur-esp8266-und-esp32)

(optional)
## Option A
- [DC Socket e.g.](https://www.amazon.de/dp/B0975TSZRV?psc=1&ref=ppx_yo2ov_dt_b_product_details)
- For Flashing: USB zu TTL Serial Adapter, best experience with: [FT232-AZ USB zu TTL Serial Adapter für 3,3V und 5V](https://www.az-delivery.de/products/ftdi-adapter-ft232rl)
## Option B
- Switch (https://www.amazon.de/dp/B0966WQRH6?psc=1&ref=ppx_yo2ov_dt_b_product_details)
- DC/AC Transformator (https://www.az-delivery.de/products/copy-of-220v-zu-5v-mini-netzteil)

## Installation

This projekt uses PlatformIO to build and upload the firmware.

For now, only Linux instructions are available.

### Prerequisites
In order to build and upload this project, PlatformIO is required.

#### Debian/Ubuntu based distributions
```bash
sudo apt install python3-pip
sudo pip install -U platformio
```

### Get code
```
git clone https://github.com/c5n/oh-ez-touch.git
```

### Configuration
Defaults for the hostname, NTP, appearance, backlight, beeper and the OpenHAB server can be set before compilation in ```data/config.json```. They are the values a pristine device starts with; everything there can also be changed later in the web interface.

WLAN credentials are not part of that file. They are kept in the ESP32's NVS, which survives both an OTA update and ```pio run -t uploadfs``` -- a config file would be overwritten by the latter. Credentials stored by older firmware, which used AutoConnect, are migrated automatically on the first boot of this one.

A device with no credentials raises an open access point named after its hostname and shows that name and its address on screen. You can connect to it with a phone. See chapter [Usage](#usage) for more details.

### Build
```
cd oh-ez-touch
pio run
```

### Simulator

The user interface can also be built and run on the development machine, in an
SDL2 window, without any hardware. This is handy for working on the layout and
the styling.

Additional prerequisite (Debian/Ubuntu):
```bash
sudo apt install libsdl2-dev
```

Build and run:
```bash
pio run -e linux -t exec
```

An "OhEzTouch" window opens showing a 320x240 screen at double size
(`lv_sdl_window_set_zoom()` in `src/main.cpp`). Mouse clicks act as touch input;
closing the window ends the program.

The simulator uses the same LVGL version, `lv_conf.h`, fonts and styles as the
firmware; the display and mouse come from LVGL's own SDL driver, enabled by
`LV_USE_SDL`. Everything hardware specific (WLAN, SPIFFS, TFT_eSPI, beeper,
backlight, sensors, OTA) is excluded via the `SIMULATOR` build flag; the small
Arduino compatibility layer it needs instead lives in `hal/sdl2`.

Since there is no HTTP client on the host, the sitemap is not fetched from a
server but comes from a canned fixture in `src/sim/sitemap_fixture.cpp`. It
provides a small demo sitemap -- a home page with two sub pages -- covering
every widget type the UI supports, and goes through the very same parser as a
real server response. Edit that file to reproduce a particular sitemap.

Item states are read from the fixture and are not written back, so operating a
widget changes it locally only.

There is no config file and no web server on the host either, so the theme comes
from the environment. That means all six variants can be compared without a
rebuild:
```bash
OHEZ_THEME=lcars pio run -e linux -t exec
OHEZ_THEME=jarvis OHEZ_NIGHT=on pio run -e linux -t exec
```
`OHEZ_THEME` takes `default`, `lcars` or `jarvis` and `OHEZ_NIGHT` takes `off`,
`on` or `auto`; anything unrecognised, and an unset variable, means the default.
`OHEZ_NIGHT_FROM` and `OHEZ_NIGHT_TO` set the hours the `auto` window spans
(22 and 6 by default). Unlike the firmware, the simulator answers
`getLocalTime()` from the host clock (`hal/sdl2`), so the header shows the real
time and `OHEZ_NIGHT=auto` can be watched crossing its boundary.

`OHEZ_SETTINGS` opens the settings screen at boot, on the tab it names --
`wlan`, `openhab`, `sensors`, `other` or `info`:
```bash
OHEZ_SETTINGS=wlan pio run -e linux -t exec
```
Touching the status bar opens it here too, but on the host there is no radio to
leave unconfigured, so the screen never comes up on its own the way it does on a
pristine device. The WLAN tab's **Scan** answers from a canned list of networks,
next to the canned sitemap; `Save` stores nothing, since there is no filesystem.

Widget icons are fetched from openHAB over HTTP by the firmware, which the
simulator cannot do either, so they are compiled in as well. They are not part
of this repository -- the openHAB classic icon set is licensed under the
EPL-2.0, which is incompatible with this project's GPL-3.0 -- so fetch them
once into your working copy:
```bash
tools/fetch_sim_icons.py
```
This downloads the icons the demo sitemap uses, rasterizes them and writes
`src/sim/icon_fixture_data.h`, which is ignored by git. It needs network access
and one of `inkscape`, `rsvg-convert` or ImageMagick. Until it has been run the
simulator draws the widgets without icons, just as the firmware does when an
icon request fails.

### Fonts

The LVGL font sources in `src/fonts/` are generated and committed, so a normal
build needs no font tooling. Regenerate them with `tools/build_fonts.sh` after
changing a face, a size or a glyph range; it needs `lv_font_conv` (an npm tool)
and, for the LCARS face, network access to fetch Antonio from Google Fonts.

### Tests
The unit tests run on the host, in the same `linux` environment as the
simulator, so they need the SDL2 development files too:
```bash
pio test -e linux
```
`test/test_item_setters` covers the string setters of `Item`, which copy
labels, states, patterns and mappings of unknown length straight out of the
sitemap JSON that openHAB serves. Those copies have to truncate cleanly, and
the tests place a canary after the object to catch one that does not. They are
host-only because the setters are inline in `src/openhab_connector.hpp`,
so nothing from `src/` has to be linked.

`test/test_ui_theme` covers the theme name lookups in `src/ui_theme.hpp`. They
are the only funnel between a theme's name and its enum, and four callers pass
through them -- the config file, the web form, the simulator's environment and
the compiled-in defaults -- none of which checks the result, so the fallback to
the default theme has to hold for a typo, an empty string and a NULL alike.

### Upload
Example for Connecting UART TTL Adapters for flashing works for me: 
- Put UART TTL Adapter on 5V with jumper
- UART VCC connection to ESP32 5V 
- UART GND connection to ESP32 GND (6. PIN same Row 5V)
- UART RX connection to ESP32 TXD
- UART TX connection to ESP32 RXD
- Connect ESP GND to ESP G0 for Flashing Mode

Connect the ESP32 board to your computer. A ttyUSB device should appear. It will likely be /dev/ttyUSB0 if no other USB-serial adapters are connected.
Use following Command after connecting to identify just connected adapters 
```
dmesg | grep /dev/ttyUSB
```

2 files have to be uploaded: The filesystem image and the firmware.

If you have no user rights to access the /dev/ttyUSB device, one option is to add a ```sudo```. Another would be to [add udev rules](https://docs.platformio.org/en/latest/faq.html#platformio-udev-rules) to allow user access.

Execute the following command to upload firmware.
Upon the output of ```Connecting........_____``` press and hold BOOT button on ESP32 until the upload process starts.
!! In some circumstances (don't know why) the RST needs to be pushed short time to connect instead of BOOT !!

Upload the filesystem image.

```
pio run -t uploadfs --upload-port /dev/ttyUSB0
```

#### ArduiTouch
For the version with the 2.4" display.
```
pio run -t upload -e ArduiTouch --upload-port /dev/ttyUSB0
```

#### ArduiTouch 2.8"
For the version with the 2.8" display.
```
pio run -t upload -e ArduiTouch28 --upload-port /dev/ttyUSB0
```

### Update tool
To update one or more devices over the air, a simple script is provided in the tools folder.

```
Usage:
    ./tools/batchupdate.sh [-p] -t <target> <hostname1> <hostname2> ...
    ./tools/batchupdate.sh [-p] -l <listfile>

    -p              Parallel multi process update

    -t <target>     Target should be one of the available build targets.
                    e.g. ArduiTouch28

    -l <listfile>   Text file with list of target and hostnames.
                    Each line has target hostname, separated by tabs or spaces.
```

If you have more than one ArduiTouch device, it makes sense to create a ```listfile``` with all of your devices.

Example ```myOhEzTouchDevices.txt```:
```
ArduiTouch      oheztouch-01
ArduiTouch28    oheztouch-02
ArduiTouch      oheztouch-03
```
!!! Please be aware of, a Carriage Return after last device in list is needed !!!

It is possible to update all devices in parallel by using the ```-p``` option.

#### Update project folder
```
git pull
```
#### Rebuild targets
```
pio run
```
#### Roll out update
Example for update of all of your devices by using the listfile:
```
./tools/batchupdate.sh -p -l myOhEzTouchDevices.txt
```

## Usage

### OpenHAB Sitemap

Sitemaps for the OhEzTouch can contain the following elements:
- Colorpicker
- Selection
- Setpoint
- Slider
- Switch
- Text
- Default

Example sitemap (```oheztouch.sitemap```):
```
sitemap oheztouch label="OhEzTouch Test"
{
    Switch      item=OHEZTOUCH_Switch
    Selection   item=OHEZTOUCH_Switch mappings=[OFF="Off", ON="On"]
    Selection   item=OHEZTOUCH_Select mappings=["SEL1"="Selection 1", "SEL2"="Selection 2", "SEL3"="Selection 3"]
    Default     item=OHEZTOUCH_Player
    Default     item=OHEZTOUCH_Rollershutter

    Text label="Submenu" icon="settings"
    {
        Setpoint    item=OHEZTOUCH_Number label="Setpoint"  minValue=-10 maxValue=10 step=0.5
        Slider      item=OHEZTOUCH_Number label="Slider"    minValue=-10 maxValue=10 step=1
        Text        item=OHEZTOUCH_Number label="Text"
        Colorpicker item=OHEZTOUCH_Color
    }
}

```
Items for example sitemap:
```
String          OHEZTOUCH_Select        "Selection"         <fan>
String          OHEZTOUCH_String        "String"            <text>
Switch          OHEZTOUCH_Switch        "Switch"            <switch>
Number          OHEZTOUCH_Number        "Number [%.1f °C]"  <temperature>
Color           OHEZTOUCH_Color         "Color [%s]"        <colorlight>
Player          OHEZTOUCH_Player        "Player"            <receiver>
Rollershutter   OHEZTOUCH_Rollershutter "Rollershutter"     <blinds>

```

### Power up
Connect the ArduiTouch to an appropriate power suppy (e.g. 12 V, 300 mA).

### Configuration
Once WLAN credentials have been entered, the ArduiTouch connects and tries to load the sitemap right away, using the defaults from ```data/config.json``` for everything else. On a device that has been through the portal before, you can skip the following steps.

#### Configure the WLAN
On pristine devices, no WLAN is configured. The ArduiTouch immediately raises an open AccessPoint named after its hostname -- ```oheztouch-new``` by default -- and shows that name and the address to open on its own screen.

Connect your smartphone to that unsecured WLAN and open ```http://192.168.4.1/```. There is no captive-portal popup, which is why the device tells you the address itself.

Fill in your network and password in the WLAN section and press Connect. The ArduiTouch reconnects right away; the AccessPoint closes a few seconds later.

If the device is already provisioned but cannot reach its network, it raises the same AccessPoint for ten minutes and then keeps retrying quietly. It is an open network and the firmware update endpoint is unauthenticated, which is why it does not stay up indefinitely.

#### Web interface
Everything the device serves, on port 80:

Route            | Purpose
---------------- | -------
```/```          | Status, the WLAN section and all settings
```/save```      | Stores the settings and redirects back to ```/```
```/wifi```      | Stores WLAN credentials and reconnects
```/restart```   | Reboots the device
```/update```    | Firmware upload, also used by ```tools/batchupdate.sh```

None of these is authenticated, and the setup AccessPoint is open, so anyone who can reach the device can reconfigure it or flash it. That has always been true; treat the device as trusted-network-only.

#### Settings on the screen
Touching the upper bar opens the settings screen. It has five tabs, and the
buttons across the top carry a symbol each rather than a name -- five words do
not fit 320 pixels -- so the title bar names the tab you are on.

Tab                     | Contents
----------------------- | --------
WLAN                    | Network and password, plus a **Scan** button that lists the access points in range with their signal strength. Touch one to fill in its name and go straight to the password. **Save** stores the credentials and reconnects.
openHAB (house symbol)  | Host, port and sitemap
Sensors (eye symbol)    | The BME280 rows
Other (gear symbol)     | Hostname, NTP, appearance, backlight and beeper
Info (list symbol)      | The Systeminfo table -- uptime, version, and the IP your DHCP server handed out -- and a **Restart** button

Touching a row opens an on-screen keyboard for the text and number settings, and
toggles or steps the switches and the drop-down-style ones in place. Nothing is
stored until you press **Save** on that tab, so leaving the screen throws away
whatever you were in the middle of typing.

If a setting you changed is one of the two that are only read while the device
boots -- the hostname, and the BME280 on/off -- **Save** offers to restart. Every
other setting applies immediately, on this screen and in the web interface
alike.

**A device with no WLAN credentials brings this screen up by itself at boot**, on
the WLAN tab. That is the whole setup procedure: scan, pick the network, type the
password, Save. No second device and no browser needed.

#### OpenHAB Settings
Open ```http://<hostname>/``` -- everything is on that one page: a status block, the WLAN section, all of the settings below, and buttons for the firmware update and a restart. The same settings are on the panel itself, on the settings screen above; both read one table in ```src/settings_fields.cpp```, so they cannot drift apart.

Settings marked ```*``` are only read while the device boots, so they take effect after a restart. Everything else applies as soon as it is saved -- including the openHAB server, the backlight levels and the beeper, which used to need one without saying so.

##### General

Setting         | Default       | Description
--------------- | --------------| -----------
Hostname ```*```| oheztouch-new | Set the hostname of this device according to your naming convention. Also the name of the setup AccessPoint.


##### NTP Time

Setting         | Default       | Description
--------------- | ------------- | -------------
Host            | pool.ntp.org  | Host which serves the time. e.g. pool.ntp.org or your router.
GMT Offset      | 1             | Offset of your timezone from Greenwich Mean Time
Daylight Saving | 0             | Daylight saving +1 hour

##### Appearance

Setting         | Default       | Description
--------------- | ------------- | -------------
Theme           | Default       | Look of the user interface: ```Default```, ```LCARS``` or ```JARVIS```
Night mode      | off           | ```off```, ```on```, or ```auto``` to follow the clock
Night from      | 22            | Hour the night variant starts, when night mode is ```auto```
Night to        | 6             | Hour the night variant ends, when night mode is ```auto```

The theme takes effect as soon as it is saved, on the screen as well as in the
browser. ```auto``` needs the clock, so it only starts working once NTP has
answered.

##### LCD Backlight Dimming

Setting           | Default     | Description
----------------- | ----------- | -------------
Activity timeout  | 60          | After the timeout defined in seconds since last touch the display will dim down
Normal Brightness | 100         | Normal brightness level in percent
Dim Brightness    | 40          | Dim brightness level in percent

##### Beeper

Setting         | Default       | Description
--------------- | ------------- | -------------
Enable Beeper   | On            | Enable blips and bleeps

##### Openhab Server

Setting         | Default       | Description
--------------- | ------------- | -------------
Host            | openhabian    | Hostname of the OpenHAB server
Port            | 8080          | Port
Sitemap         | oheztouch     | Name of the sitemap you've setup for this ArduiTouch device

##### Sensors

Setting                 | Default | Description
----------------------- | ------- | -------------
Use BME280 sensor ```*```| off     | Read the optional BME280 and publish it to OpenHAB
Update interval         | 180     | Seconds between two readings
Temperature item        |         | Name of the OpenHAB item the temperature is sent to
Humidity item           |         | Name of the OpenHAB item the humidity is sent to
Pressure item           |         | Name of the OpenHAB item the pressure is sent to

### Fix icons

Some of the original openhab-webui icons are exceptionally large for no reason. Since the ESP32 has limited RAM resources, we have to take file sizes into account. Re-encoding of the PNG graphic files using '''convert''' is the solution for now.

```
sudo apt install imagemagick
git clone https://github.com/openhab/openhab-webui.git
cd openhab-webui/bundles/org.openhab.ui.iconset.classic/src/main/resources/icons
mkdir output
for f in *.png; do convert $f -strip output/$f; done
```

Move the files from ```output``` folder to your ```openhab2-conf/icons/classic/``` folder.

I know, this is not very convenient. Finding a solution has top priority on my todo list.

## Development

The project is still under development, but is already very usable.

If you have any comments, suggestions or even code to submit, please let me know. I'm happy to hear from you. :)

Contact: c5n AT posteo DOT de

## ToDo list

- [ ] openhab_ui: Fix icon loading. Some of the original icon file sizes are too large and have to be reencoded.
- [ ] openhab_ui: Auto close item manipulation window after timeout
- [ ] openhab_ui: Auto back to homescreen after timeout
- [ ] openhab_ui: Prefer widget label text instead of item label text
- [ ] openhab_ui: Add secured sections with PIN protection
- [x] openhab_ui: Improve selection, setpoint and slider elements
- [x] ac: Improve OTA firmware update --> batchupdate.sh
- [x] main: Show portal active icon
- [x] openhab_ui: Add theme support
- [ ] main: Add screen calibration
- [x] main: Add setup wizard with WLAN credential input instead of portal procedure -- on the panel too, see [Settings on the screen](#settings-on-the-screen)
- [ ] doc: Retake the web interface screenshots -- ```doc/img/browser_*.png``` still show the removed AutoConnect pages
- [ ] build: Replace ```-O0``` in ```[common] build_flags```. It applies to about 402 KB of compiled text (LVGL, TFT_eSPI, the Arduino libraries, ```src/```) while the prebuilt ESP-IDF archives are already ```-Os```; ```-Os``` should free 100-150 KB, and ```-fno-exceptions``` a slice of the 70 KB of exception tables in those units. Measure before believing it.
- [ ] ota: Wrap ```src/ota/basic_ota.cpp``` in ```#if USE_ARDUINO_BASIC_OTA```. It is disabled in every environment, but its unconditional references keep ArduinoOTA, ESPmDNS and mdns linked -- about 4 KB of flash for dead code.
- [ ] sensors: Sensors should submit update instead of command
- [ ] sensors: Support DS18B20 onewire sensors

## License
[GNU General Public License v3.0](LICENSE.md)

## Greetings to 3rd party projects and libraries
This project was created using the following projects and libraries. A big thank you to all of them and the ones I missed:

- https://platformio.org/
- https://lvgl.io/
- https://arduinojson.org/
- https://github.com/Hieromon/AutoConnect (origin of ```src/ota/HTTPUpdateServer.*```, the OTA update handler)
- https://github.com/Bodmer/TFT_eSPI
- https://github.com/YiannisBourkelis/Uptime-Library

Embedded fonts:
- [Roboto](https://fonts.google.com/specimen/Roboto) (Apache-2.0), the default UI face
- [Antonio](https://fonts.google.com/specimen/Antonio) (SIL OFL 1.1), the condensed face of the LCARS theme
