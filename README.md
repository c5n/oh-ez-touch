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

This project is built with [ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32/),
which is also what builds the desktop simulator.

For now, only Linux instructions are available.

### Prerequisites

ESP-IDF **v6.1**. The version is pinned rather than "recent enough": the
`linux` target the simulator uses is officially a preview feature, so an IDF
upgrade is a change to verify rather than a routine update.

```bash
mkdir -p ~/esp
git -C ~/esp clone -b v6.1 --depth 1 --recursive https://github.com/espressif/esp-idf.git
~/esp/esp-idf/install.sh esp32
```

`. ~/esp/esp-idf/export.sh` puts `idf.py` on the PATH, and has to be run once
per shell.

#### Debian/Ubuntu based distributions

```bash
sudo apt install libsdl2-dev libbsd-dev pkg-config ninja-build
```

`libsdl2-dev` is the simulator's window. `libbsd-dev` is not optional and its
absence is not obvious: IDF's own `components/linux/linux_include/string.h`
includes `<bsd/string.h>` unconditionally, so without it every translation
unit of the simulator fails to compile -- and IDF's CMake only warns about it.

### Get code

The two libraries this project vendors are submodules, so clone recursively:

```bash
git clone --recurse-submodules https://github.com/c5n/oh-ez-touch.git
```

In an existing working copy, `git submodule update --init --recursive`.

### Configuration

Defaults for the hostname, NTP, appearance, backlight, beeper and the OpenHAB
server can be set before compilation in ```data/config.json```. They are the
values a pristine device starts with; everything there can also be changed
later in the web interface. `idf.py flash` writes that file to the device's
filesystem along with the firmware -- there is no separate upload step any
more.

The built-in defaults in `Config::loadConfig()` apply to anything the file does
not mention, so a partial `config.json` is fine and a missing one leaves a
complete, working configuration.

WLAN credentials are not part of that file. They are kept in the ESP32's NVS,
which survives both an OTA update and a filesystem reflash -- a config file
would be overwritten by the latter. Credentials stored by older firmware, which
used AutoConnect, are migrated automatically on the first boot of this one.

A device with no credentials raises an open access point named after its
hostname and shows that name and its address on screen. You can connect to it
with a phone. See chapter [Usage](#usage) for more details.

### Build

Each board is a build of its own, selected by a defaults file. The build
directory is what identifies it afterwards, including to the update tool.

```bash
cd oh-ez-touch
. ~/esp/esp-idf/export.sh

idf.py -B build/arduitouch -DSDKCONFIG=build/arduitouch/sdkconfig \
       -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.esp32;sdkconfig.defaults.arduitouch" \
       set-target esp32
idf.py -B build/arduitouch build
```

The three boards are `sdkconfig.defaults.arduitouch` (2.4"),
`sdkconfig.defaults.arduitouch28` (2.8") and `sdkconfig.defaults.lanbon`
(Lanbon L8). Omitting the board file gives the ArduiTouch 2.4", which is the
Kconfig default. `sdkconfig.defaults.arduitouch_jtag` is the 2.4" with the two
pins an attached esp-prog needs moved out of its way.

`-DSDKCONFIG` is not optional when more than one target is in play: `idf.py`
otherwise writes the generated `sdkconfig` to the project root, where the
builds overwrite each other's and the second one refuses to start. Keeping it
inside the build directory means `-B` alone identifies a build.

Note that a defaults file only seeds a **new** `sdkconfig`. After editing one,
delete that build's `sdkconfig` -- or the whole build directory -- and re-run
`set-target`.

`idf.py -B build/arduitouch menuconfig` reaches everything else, including the
board choice, the JTAG pin remap and the per-module debug output under
**OhEzTouch**.

### Simulator

The user interface can also be built and run on the development machine, in an
SDL2 window, without any hardware. This is handy for working on the layout and
the styling -- and, since it is the same code, for working on the openHAB
client and the web interface too.

```bash
idf.py -B build/linux -DSDKCONFIG=build/linux/sdkconfig \
       -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.linux" \
       --preview set-target linux
idf.py -B build/linux build
./build/linux/oh-ez-touch.elf
```

An "OhEzTouch" window opens showing a 320x240 screen at double size
(`port_display_init()` in `main/port/linux/port_display.c`). Mouse clicks act as
touch input; closing the window ends the program.

The simulator is not a separate build of a reduced program. It is the same
sources, the same LVGL version, the same `lv_conf.h`, fonts and styles; what
differs is confined to `main/port/`, which has one implementation per target.
So the panel really does fetch its sitemap over HTTP, really does serve its
web interface, and really does store its settings.

- **openHAB**: set the server with the config file, or with
  `OHEZ_OPENHAB_HOST`, `OHEZ_OPENHAB_PORT` and `OHEZ_SITEMAP`. Operating a
  widget POSTs a command, exactly as the panel does.
- **Web interface**: on port 8080 rather than 80, since an unprivileged process
  cannot bind 80. `OHEZ_WEBUI_PORT` overrides that.
- **Settings**: stored in `$XDG_CONFIG_HOME/oh-ez-touch/config.json` (or
  `~/.config/oh-ez-touch/config.json`), which is a real file that can be edited
  by hand. `OHEZ_CONFIG_DIR` moves it.
- **WLAN credentials**: stored in an emulated NVS image under
  `$XDG_STATE_HOME/oh-ez-touch/flash.bin`. `OHEZ_STATE_DIR` moves it. Two
  simulator instances cannot share one, and the second to start says so rather
  than corrupting it.
- **No radio, no backlight, no buzzer, no sensor and no OTA.** These report
  "there is none" rather than pretending: the WLAN tab's **Scan** answers from a
  canned list, the sensor publishes nothing, and `POST /update` answers 501.

#### Offline mode

`OHEZ_OFFLINE=1` serves the sitemap and the widget icons from compiled-in
fixtures instead of the network. That gives a stable, reproducible screen for
comparing rendering changes, and lets the UI be worked on with no openHAB
anywhere:

```bash
OHEZ_OFFLINE=1 ./build/linux/oh-ez-touch.elf
```

The fixture in `main/sim/sitemap_fixture.cpp` is a small demo sitemap -- a home
page with two sub pages -- covering every widget type the UI supports, and it
goes through the very same parser as a real server response. Edit that file to
reproduce a particular sitemap. Item states are read from it and not written
back, so operating a widget changes it locally only.

The widget icons are not part of this repository -- the openHAB classic icon set
is licensed under the EPL-2.0, which is incompatible with this project's
GPL-3.0 -- so fetch them once into your working copy:

```bash
tools/fetch_sim_icons.py
```

This downloads the icons the demo sitemap uses, rasterizes them and writes
`main/sim/icon_fixture_data.h`, which is ignored by git. It needs network access
and one of `inkscape`, `rsvg-convert` or ImageMagick. Until it has been run,
offline mode draws the widgets without icons, just as the firmware does when an
icon request fails.

#### Environment overrides

The `OHEZ_*` variables are applied after the config file, so it can be inspected
without being edited. They work on the device too, which simply has no
environment to read them from.

```bash
OHEZ_THEME=lcars ./build/linux/oh-ez-touch.elf
OHEZ_THEME=jarvis OHEZ_NIGHT=on ./build/linux/oh-ez-touch.elf
```

`OHEZ_THEME` takes `default`, `lcars` or `jarvis` and `OHEZ_NIGHT` takes `off`,
`on` or `auto`; anything unrecognised means the default. `OHEZ_NIGHT_FROM` and
`OHEZ_NIGHT_TO` set the hours the `auto` window spans (22 and 6 by default).

The clock follows the *configured* GMT offset rather than the host's timezone,
because that is what the panel would show -- including an offset the user got
wrong. `OHEZ_NIGHT=auto` can be watched crossing its boundary.

`OHEZ_MQTT`, `OHEZ_MQTT_HOST`, `OHEZ_MQTT_PORT` and `OHEZ_MQTT_TOPIC` point the
MQTT client at a broker for one run, which is the quickest way to watch what it
publishes:

```bash
OHEZ_MQTT=on OHEZ_MQTT_HOST=localhost ./build/linux/oh-ez-touch.elf
```

`OHEZ_MQTT` takes `on` or `off`; anything that is not `off` or `0` enables it.

`OHEZ_BLE_FIXTURE=1` serves four compiled-in BLE advertisements -- an iBeacon,
an Eddystone-UID, an Eddystone-TLM frame from the same advertiser, and a plain
named device -- instead of the Bluetooth the host does not have. They go through
the very same parsers a real advertisement does, so the beacon table, the
averaging, the expiry and the topics can all be watched on a desktop:

```bash
OHEZ_BLE_FIXTURE=1 ./build/linux/oh-ez-touch.elf
```

Off by default, for the reason the BME280 invents nothing on the host: these
readings are published to a broker, and a simulator that quietly wrote fiction
into someone's presence history would be worse than one that did nothing.

`OHEZ_SETTINGS` opens the settings screen at boot, on the tab it names --
`wlan`, `openhab`, `mqtt`, `sensors`, `other` or `info`:

```bash
OHEZ_SETTINGS=wlan ./build/linux/oh-ez-touch.elf
```

Touching the status bar opens it here too, but on the host there is no radio to
leave unconfigured, so the screen never comes up on its own the way it does on a
pristine device.

### Fonts

The LVGL font sources in `components/lvgl/fonts/` are generated and committed,
so a normal build needs no font tooling. Regenerate them with
`tools/build_fonts.sh` after changing a face, a size or a glyph range; it needs
`lv_font_conv` (an npm tool) and, for the LCARS face, network access to fetch
Antonio from Google Fonts.

### Tests

The unit tests are an ESP-IDF project of their own, on the same `linux` target
as the simulator:

```bash
cd test/host
idf.py --preview set-target linux
idf.py build && ./build/oh-ez-touch-host-test.elf
```

It exits with the number of failures, so it can be used in a script as it
reads.

`test_item_setters` covers the string setters of `Item`, which copy labels,
states, patterns and mappings of unknown length straight out of the sitemap
JSON that openHAB serves. Those copies have to truncate cleanly, and the tests
place a canary after the object to catch one that does not.

`test_ui_theme` covers the theme name lookups in `main/ui/ui_theme.hpp`. They are
the only funnel between a theme's name and its enum, and four callers pass
through them -- the config file, the web form, the environment overrides and the
compiled-in defaults -- none of which checks the result, so the fallback to the
default theme has to hold for a typo, an empty string and a NULL alike.

`test_config_fields` covers the settings table in `main/config/config_fields.cpp`
-- the one description of every setting, walked by the web form, the panel's
settings screen and the MQTT client alike. What it pins down is the table rather
than the accessors: that no two rows share a name, that a name is usable both as
a POST argument and as an MQTT topic segment, that every tab has rows on it, and
that the rows flagged as needing a restart are the ones that really do.

`test_ble_beacon` covers the advertisement parsers in
`main/ble/ble_beacon.cpp`, and is the suite that earns its keep most easily. The
iBeacon and Eddystone layouts are specified by Apple and by Google and cannot be
checked by reading the code that consumes them; the bytes arrive off the air
from devices nobody here wrote, so malformed input is the normal case rather than
the exception; and there is nowhere else to exercise any of it, because a desktop
has no Bluetooth and the device firmware has never been run on hardware. So the
suite walks a valid advertisement truncated at every possible length, feeds it
lengths that run past the end of the payload, and pins down each format's
identity, reference power and telemetry byte by byte. It found two bugs in its
own fixtures and one in the URL character ranges on the first run.

Those two suites are also the only ones that link anything out of `main/`, and
only the two files: `config_fields.cpp` and `ble_beacon.cpp` touch neither LVGL
nor the network. For the beacon parsers that is not a happy accident but the
reason `main/port/port_ble.h` yields raw advertisement bytes and leaves the
parsing above the port layer. The other two suites cover header-only code and
link nothing, which is what keeps the test app worth having.

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

If you have no user rights to access the /dev/ttyUSB device, one option is to
add a ```sudo```. Another would be to add
[udev rules](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32/get-started/establish-serial-connection.html)
to allow user access.

One command uploads everything -- the bootloader, the partition table, the
firmware and the filesystem image built from ```data/```:

```bash
idf.py -B build/arduitouch -p /dev/ttyUSB0 flash monitor
```

Upon the output of ```Connecting........_____``` press and hold the BOOT button
on the ESP32 until the upload process starts.
!! In some circumstances (don't know why) the RST needs to be pushed short time to connect instead of BOOT !!

`monitor` is optional and shows the serial log; `Ctrl-]` leaves it.

### Update tool
To update one or more devices over the air, a simple script is provided in the tools folder.

```
Usage:
    ./tools/batchupdate.sh [-p] -t <target> <hostname1> <hostname2> ...
    ./tools/batchupdate.sh [-p] -l <listfile>

    -p              Parallel multi process update

    -t <target>     Name of a build directory under build/, which is what
                    identifies a board now that each one is a separate
                    ESP-IDF build. e.g. arduitouch28

    -l <listfile>   Text file with list of target and hostnames.
                    Each line has target hostname, separated by tabs or spaces.
```

If you have more than one ArduiTouch device, it makes sense to create a ```listfile``` with all of your devices.

Example ```myOhEzTouchDevices.txt```:
```
arduitouch      oheztouch-01
arduitouch28    oheztouch-02
arduitouch      oheztouch-03
```
!!! Please be aware of, a Carriage Return after last device in list is needed !!!

It is possible to update all devices in parallel by using the ```-p``` option.

#### Update project folder
```
git pull --recurse-submodules
```
#### Rebuild targets
```
idf.py -B build/arduitouch build
idf.py -B build/arduitouch28 build
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
Touching the upper bar opens the settings screen. It has six tabs, and the
buttons across the top carry a symbol each rather than a name -- six words do
not fit 320 pixels -- so the bar along the bottom names the tab you are on. That
bar also holds the tab's buttons and the **X** that leaves the screen; there is
no title bar, so that all of the 240 pixels that are not chrome go to settings.

Tab                     | Contents
----------------------- | --------
WLAN                    | Network and password, plus a **Scan** button that lists the access points in range with their signal strength. Touch one to fill in its name and go straight to the password. **Save** stores the credentials and reconnects.
openHAB (house symbol)  | Host, port and sitemap
MQTT (upload symbol)    | Broker, port, credentials, and what to publish -- see [MQTT](#mqtt)
Sensors (eye symbol)    | The BME280 rows, and the BLE beacon scanner
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
Open ```http://<hostname>/``` -- everything is on that one page: a status block, the WLAN section, all of the settings below, and buttons for the firmware update and a restart. The same settings are on the panel itself, on the settings screen above; both read one table in ```main/config/config_fields.cpp```, so they cannot drift apart.

Settings marked ```*``` are only read while the device boots, so they take effect after a restart. Everything else applies as soon as it is saved -- including the openHAB server, the MQTT broker, the backlight levels and the beeper, which used to need one without saying so.

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

##### MQTT Broker

Setting         | Default       | Description
--------------- | ------------- | -------------
Enable MQTT     | off           | Connect to the broker below and publish to it
Host            | mosquitto     | Hostname of the MQTT broker
Port            | 1883          | Port. Plain TCP only -- there is no TLS support
User            |               | Leave empty for an anonymous broker
Password        |               | Sent with the user name. Never shown in clear, and never published

##### MQTT Publishing

Setting            | Default   | Description
------------------ | --------- | -------------
Base topic         | oheztouch | First segment of every topic; the hostname follows it
Publish interval   | 60        | Seconds between two rounds of system information
Retain published values | On   | Publish with the retain flag, so a subscriber that connects later sees the current values at once

The MQTT settings all take effect as soon as they are saved: the client
reconnects itself, and does so only when something it is actually using
changed.

##### Sensors

Setting                 | Default | Description
----------------------- | ------- | -------------
Use BME280 sensor ```*```| off     | Read the optional BME280 and publish it to OpenHAB and, if it is enabled, to MQTT
Update interval         | 180     | Seconds between two readings
Temperature item        |         | Name of the OpenHAB item the temperature is sent to
Humidity item           |         | Name of the OpenHAB item the humidity is sent to
Pressure item           |         | Name of the OpenHAB item the pressure is sent to

##### Bluetooth LE Beacons

Setting                       | Default | Description
----------------------------- | ------- | -------------
Scan for BLE beacons ```*```  | off     | Listen for BLE advertisements and publish them over MQTT -- see [Bluetooth LE beacons](#bluetooth-le-beacons)
Scan every                    | 30      | Seconds between the starts of two scan windows
Scan for                      | 5       | Seconds each window lasts. Not continuous, because the radio is shared with WiFi
Ignore weaker than            | -90     | Advertisements below this RSSI are dropped, which is what keeps the table to things nearby
Forget after                  | 120     | Seconds of silence before a beacon is dropped and its topics cleared
Publish non-beacon devices    | off     | Publish plain BLE devices too, not only recognised beacons

### MQTT

The client publishes what the panel knows about itself and subscribes to one
wildcard through which every setting can be written. It is off by default; the
settings are on the ```MQTT``` tab of the settings screen and in the web
interface.

Every topic starts with ```<base topic>/<hostname>```, so with the defaults that
is ```oheztouch/oheztouch-new```. Two segments rather than one because the
default has to be safe: a second panel out of the box would otherwise publish
over the first.

Topic                        | Published        | Value
---------------------------- | ---------------- | -----
```status```                 | on connect       | ```online```, and ```offline``` as the last will
```system/name```            | on connect       | The hostname
```system/target```          | on connect       | Which board this firmware is for, e.g. ```ArduiTouch```
```system/version```         | on connect       | e.g. ```0.20```
```system/build```           | on connect       | Compiler date and time
```system/git```             | on connect       | The commit this firmware was built from
```system/uptime```          | every interval   | Seconds since boot
```system/heap```            | every interval   | Free heap in bytes
```system/ip```              | every interval   | The station address
```system/ssid```            | every interval   | The network, or the interface name on the simulator
```system/rssi```            | every interval   | dBm. Absent where there is no radio
```system/quality```         | every interval   | The same as a percentage, on the scale the status bar uses
```ui/night```               | every interval   | ```ON``` while the night variant is in effect
```sensor/temperature```     | on each reading  | Degrees Celsius
```sensor/humidity```        | on each reading  | Percent relative humidity
```sensor/pressure```        | on each reading  | hPa
```config/<setting>```       | on connect, and after every save | One topic per setting

Everything is published at QoS 0, and retained unless ```Retain published
values``` is turned off. Nothing here is an event -- every topic carries the
current value of something -- so a subscriber that missed an update wants the
newest one and not the one it missed, which is what retain gives it.

The sensor topics need ```Use BME280 sensor``` turned on, and appear alongside
whatever the OpenHAB item names are set to: one reading goes to both.

#### Writing a setting

```config/<setting>/set``` writes the setting and saves it. The ```<setting>```
names are the POST argument names in ```main/config/config_fields.cpp``` --
```theme```, ```night_mode```, ```bl_normal```, ```oh_host``` and so on -- which
is the same list the web form posts, because it is the same table.

```bash
mosquitto_pub -t oheztouch/oheztouch-new/config/theme/set -m LCARS
mosquitto_pub -t oheztouch/oheztouch-new/config/night_mode/set -m auto
mosquitto_pub -t oheztouch/oheztouch-new/config/bl_dim/set -m 20
```

The ranges, the character rules and the option names are the ones the settings
screen and the web form already enforce: a number outside its range is clamped,
a host name containing ```/``` or ```:``` is dropped, and an option name that
does not exist selects the first one. A checkbox takes ```ON```, ```true```,
```yes``` or ```1```; anything else is off.

Two exceptions. The broker password is never published and cannot be set this
way -- it has no business travelling through the broker it authenticates to. And
a setting marked ```*``` above is stored and saved, but only takes effect after
a restart, exactly as it does from the web form.

Nothing is written to flash when the value did not change, so a broker replaying
a retained command on every reconnect costs nothing.

There is no authentication in front of any of this, which is also true of the
web interface. Both belong on a network you trust.

### Bluetooth LE beacons

Off by default. With ```Scan for BLE beacons``` turned on the panel listens for
BLE advertisements and publishes what it hears to the same broker the MQTT
client uses, under a ```ble/``` subtree. It is a receiver only: nothing is
advertised, nothing is connected to, and no pairing is possible.

Turning it on needs a restart, and it is the one setting where that is not just
a matter of where it is read: bringing the Bluetooth controller up claims tens
of kilobytes of RAM that stopping it does not give back, so a panel that is not
scanning must never have started it.

#### Topics

Under the MQTT prefix, so with the defaults these read
```oheztouch/oheztouch-new/ble/...```. The key is the advertiser's hardware
address, lower case hex, no separators.

Topic                       | Published        | Value
--------------------------- | ---------------- | -----
```count```                 | every window     | How many advertisers are currently published
```dropped```               | every window     | Reports lost to a full queue since boot -- normally 0
```<addr>/type```           | on discovery     | ```iBeacon```, ```Eddystone-UID```, ```Eddystone-URL``` or ```device```
```<addr>/id```             | on discovery     | The beacon's own identity, or empty for a device that has none
```<addr>/name```           | on discovery     | The advertised name, when there is one
```<addr>/power```          | on discovery     | dBm: the power at one metre a beacon declares, or a plain device's transmit power
```<addr>/rssi```           | every window     | dBm, averaged over the window
```<addr>/distance```       | every window     | Metres, estimated. Beacons only -- see below
```<addr>/battery```        | every window     | mV, Eddystone-TLM only
```<addr>/temperature```    | every window     | Degrees Celsius, Eddystone-TLM only

Three formats are recognised, which is what a beacon is in practice: **iBeacon**
(identity is the proximity UUID, the major and the minor), **Eddystone-UID**
(a namespace and an instance) and **Eddystone-URL**. **Eddystone-TLM** is not an
identity but telemetry, and beacons interleave it between their identity frames,
so its battery and temperature are merged onto the entry the identity frames
built. Anything else in range -- a phone, a watch, a thermostat -- is a
```device```, with whatever name and transmit power it advertised, and is only
published when ```Publish non-beacon devices``` is on.

When an advertiser has not been heard for ```Forget after``` seconds, all of its
topics are cleared with a zero-length retained publish, which is how a retained
message is removed. Without that a beacon carried out of the building would sit
in the broker at its last RSSI forever.

#### Two things to know before wiring it up

**The address is the key, and the identity is a value**, which is the other way
round from how it is usually drawn. It has to be: an iBeacon's UUID is shared on
purpose -- a shop's hundred tags carry one UUID and differ only in the minor --
so it is not unique, while the address always is. The consequence is that a
beacon which randomises its address, as most phones and many tags do for exactly
the privacy reason that makes this awkward, will come and go under a new key
every few minutes. A beacon meant to be tracked advertises a stable address.

**The distance is an estimate and reads short.** It is the log-distance path loss
model with the exponent at 2.0, which is free space; indoors it is nearer 3, so
walls and furniture make it optimistic. It is published because "about two
metres or about twenty" is useful and a raw RSSI is not, and it should not be
read more precisely than that. It is published *only* for beacons, because only
a beacon states a power calibrated at a known distance -- a plain device's
transmit power says how loudly its radio speaks, not how loud it is a metre
away, and treating one as the other puts something three metres off at a hundred
and forty.

#### What it costs

Bluetooth is NimBLE in observer role, with the roles trimmed as far as they go.
It costs about 185 KB of flash, which leaves the app partition 22% free, and it
moves the WiFi library's hot paths out of IRAM to make room for the controller
-- without that IRAM comes out at 98.4% full with nothing left over. The RAM the
controller allocates is claimed at start-up and only when the setting is on.

The radio is shared with WiFi. Software coexistence interleaves them, so neither
stops working, but a scan takes airtime from the openHAB polling and the web
interface for as long as it runs -- which is why the scan is a window every
thirty seconds rather than a continuous one. A beacon advertises several times a
second, so five seconds is many reports from everything in range.

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

### Source layout

```
main/                 main.cpp -- the entry point
main/config/          the settings, and the one field table that both the
                      panel's settings screen and the web form walk
main/ui/              the LVGL user interface: the openHAB page, the settings
                      screen, the styles and themes
main/openhab/         the openHAB client: the task every request waits on,
                      and the sitemap model and parser it feeds
main/mqtt/            the MQTT client: what the panel tells a broker, and the
                      one way the broker can talk back
main/ble/             the BLE beacon scanner: the advertisement parsers, and
                      the table of what is in range
main/web/             the web interface: one renderer, one transport per target
main/net/             WLAN credentials, and the radio state machine
main/control/         policy on top of the port layer: when to dim, and the
                      queue that plays a chime
main/sim/             the simulator's offline fixtures
main/port/            the platform boundary -- one implementation directory per
                      target, and the whole of what differs between a panel and
                      a desktop
components/           LVGL and ArduinoJson as submodules, lodepng vendored
test/host/            the unit tests, as an IDF project of their own
```

Everything above `main/port/` is shared. If a change needs a `#if` on the
target outside that directory, it probably wants a new port instead.

### The openHAB client task

Generating the UI means asking openHAB for things: a sitemap page per
navigation level, an icon per tile, and an item state per tile every five
seconds. All of it used to happen on the task that also drives LVGL, so a
page switch -- one page GET followed by up to six icon GETs, back to back --
held the screen for as long as the server took, and against an unreachable
one for the full five second timeout each. The panel was dead to the touch
for the duration.

The requests now wait on a task of their own (`main/openhab/openhab_client.cpp`).
The UI submits a URL and carries on drawing; the answer arrives on a queue
that `openhab_ui_loop()` takes one result from per iteration. What this looks
like from the front is that a page appears as soon as its JSON parses, with
every tile showing its label, its state and a placeholder watermark, and the
icons filling in behind them over the next few frames. The clock keeps
ticking and the touch keeps responding throughout, including while openHAB is
unreachable.

The worker calls no `lv_*`, and reads no `Item`, `Sitemap` or `Config`. URLs
go in and bytes come back; every decision about what a body means, and every
LVGL call, stays on the task that owns the screen. `main/openhab/openhab_connector.cpp`
is consequently a model and a parser with no idea that a network exists, which
is what lets the host tests cover the sitemap parser.

Requests are stamped with a generation, bumped whenever a page is fetched, so
that the icons and states belonging to a page navigated away from are dropped
rather than applied to whatever now occupies their tile. Commands are exempt:
one the user asked for is still worth delivering after they have moved on.

There is one worker and one queue, not a pool. Two would fetch a page's icons
faster and would also be free to deliver two taps on the same item out of
order. The connection to openHAB is held open between requests, so a page's
six icons share one TCP handshake.

### Contributing

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
- [x] build: Replace ```-O0```. ```CONFIG_COMPILER_OPTIMIZATION_SIZE``` saves 138 KB, at the predicted end of the estimate; C++ exceptions and RTTI are off by default under ESP-IDF.
- [x] ota: Wrap ```src/ota/basic_ota.cpp``` in ```#if USE_ARDUINO_BASIC_OTA``` -- deleted outright instead, together with the Arduino framework.
- [ ] main: The device firmware built here has not been run on hardware. The display, touch, backlight, beeper and BME280 drivers are translations checked against the vendor sources, not measurements.
- [ ] sensors: Sensors should submit update instead of command
- [ ] sensors: Support DS18B20 onewire sensors
- [x] mqtt: Add an MQTT client -- sensor readings, the theme, system information, and every setting readable and writable, see [MQTT](#mqtt)
- [ ] mqtt: Support TLS. ```CONFIG_MQTT_TRANSPORT_SSL``` is off and the client speaks plain TCP; turning it on needs a certificate to store and a setting to configure it from.
- [ ] mqtt: Home Assistant style discovery, so the topics above do not have to be wired up by hand
- [x] ble: Scan for BLE beacons and publish them over MQTT -- iBeacon, Eddystone UID/URL/TLM, see [Bluetooth LE beacons](#bluetooth-le-beacons)
- [ ] ble: Show the beacons in range on the panel. The table is there; nothing draws it yet.
- [ ] ble: The BLE scanner has not been run on hardware either. The NimBLE port is checked against the IDF observer examples and the parsers against the format specifications, not against a real tag.

## License
[GNU General Public License v3.0](LICENSE.md)

## Greetings to 3rd party projects and libraries
This project was created using the following projects and libraries. A big thank you to all of them and the ones I missed:

- https://docs.espressif.com/projects/esp-idf/
- https://lvgl.io/
- https://arduinojson.org/
- https://lodev.org/lodepng/

No longer used, but this project was built on them for a long time and would
not exist without them:

- https://platformio.org/
- https://github.com/Hieromon/AutoConnect (origin of the OTA update handler)
- https://github.com/Bodmer/TFT_eSPI
- https://github.com/YiannisBourkelis/Uptime-Library
- https://github.com/adafruit/Adafruit_BME280_Library

Embedded fonts:
- [Roboto](https://fonts.google.com/specimen/Roboto) (Apache-2.0), the default UI face
- [Antonio](https://fonts.google.com/specimen/Antonio) (SIL OFL 1.1), the condensed face of the LCARS theme
