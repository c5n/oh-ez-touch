# Building and flashing

This project is built with [ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32/).
The same toolchain builds the firmware and the desktop simulator.

Only Linux instructions are available.

## Prerequisites

Install ESP-IDF **v6.1**. The version is pinned. The `linux` target that the
simulator uses is a preview feature. An IDF upgrade must be verified.

```bash
mkdir -p ~/esp
git -C ~/esp clone -b v6.1 --depth 1 --recursive https://github.com/espressif/esp-idf.git
~/esp/esp-idf/install.sh esp32
```

Run `. ~/esp/esp-idf/export.sh` once per shell. It puts `idf.py` on the PATH.

On Debian/Ubuntu based distributions, also install:

```bash
sudo apt install libsdl2-dev libbsd-dev pkg-config ninja-build
```

`libsdl2-dev` is the simulator's window. `libbsd-dev` is not optional. IDF's
`components/linux/linux_include/string.h` includes `<bsd/string.h>`. Without
`libbsd-dev` every translation unit of the simulator fails to compile, and
IDF's CMake only warns about it.

## Get the code

The project contains two libraries as submodules. Clone recursively:

```bash
git clone --recurse-submodules https://github.com/c5n/oh-ez-touch.git
```

In an existing working copy, run `git submodule update --init --recursive`.

To update the project folder later, run `git pull --recurse-submodules`.

## Configuration before the build

The file `data/config.json` holds the defaults for hostname, NTP, appearance,
backlight, beeper and the openHAB server. A new device starts with these
values. Every value can be changed later in the web interface or on the
panel. `idf.py flash` writes the file to the device filesystem together with
the firmware.

The built-in defaults apply to every value the file does not mention. A
partial `config.json` is valid. A missing file leaves a complete, working
configuration. The defaults are in `main/config/config_fields.cpp`. Each
setting has a range there. A value outside the range is clamped. A hostname
with `/` or `:` is refused and the default is kept.

WLAN credentials are not in that file. They are stored in the ESP32's NVS.
NVS survives an OTA update and a filesystem reflash. Credentials stored by
firmware older than 0.90 (AutoConnect) are migrated on the first boot.

A device without credentials opens an access point named after its hostname.
The screen shows the name and the address. See
[Configuration](configuration.md#wlan-setup).

The built-in icon set is a generated file, `main/icons/icon_set_data.h`. It
is not in the repository. Run `tools/build_icon_set.py` once per working
copy, and again when the boot log shows less than 576 bytes per icon. See
[Icon set](icon-set.md).

## Build

Each board is a build of its own. A defaults file selects the board. The build
directory identifies the build afterwards, including to the update tool.

| Board | Defaults file | Build directory |
| --- | --- | --- |
| [ArduiTouch 2.4"](hardware/arduitouch.md) | `sdkconfig.defaults.arduitouch` | `build/arduitouch` |
| [ArduiTouch 2.8"](hardware/arduitouch28.md) | `sdkconfig.defaults.arduitouch28` | `build/arduitouch28` |
| [Lanbon L8](hardware/lanbon.md) | `sdkconfig.defaults.lanbon` | `build/lanbon` |
| [Cheap Yellow Display](hardware/cyd.md) | `sdkconfig.defaults.cyd` | `build/cyd` |
| ArduiTouch 2.4" with JTAG pins | `sdkconfig.defaults.arduitouch_jtag` | `build/arduitouch_jtag` |

Example for the ArduiTouch 2.4":

```bash
cd oh-ez-touch
. ~/esp/esp-idf/export.sh

idf.py -B build/arduitouch -DSDKCONFIG=build/arduitouch/sdkconfig \
       -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.esp32;sdkconfig.defaults.arduitouch" \
       set-target esp32
idf.py -B build/arduitouch build
```

For an ArduiTouch 2.4" with an esp-prog attached, use the JTAG defaults and its
own build directory. Flash the target over its UART adapter (usually
`/dev/ttyUSB0`); the JTAG interface does not replace that serial port:

```bash
idf.py -B build/arduitouch_jtag -DSDKCONFIG=build/arduitouch_jtag/sdkconfig \
       -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.esp32;sdkconfig.defaults.arduitouch_jtag" \
       set-target esp32
idf.py -B build/arduitouch_jtag build
idf.py -B build/arduitouch_jtag -p /dev/ttyUSB0 flash monitor
```

This build remaps touch CS and the backlight away from JTAG's GPIO 12–15. If
you already configured this build directory without the JTAG defaults, remove
`build/arduitouch_jtag/sdkconfig` and rerun `set-target esp32` before building;
defaults only seed a new `sdkconfig`.

Without a board defaults file, the Kconfig default applies: the ArduiTouch
2.4".

`-DSDKCONFIG` is necessary when you build more than one target. Without it,
`idf.py` writes the generated `sdkconfig` to the project root. The builds
would overwrite each other's file. With the file inside the build directory,
`-B` alone identifies a build.

A defaults file only seeds a **new** `sdkconfig`. After you edit a defaults
file, or pull a change to one, delete the build's `sdkconfig` (or the whole
build directory) and run `set-target` again. This applies to the heap
settings in `sdkconfig.defaults.esp32` too: a build directory configured
before them keeps the old task stacks and WiFi buffers.

A build directory records the absolute path of the project. After you move
or rename the checkout, `idf.py` refuses the old build directories with
"configured for project ... not ...". Run `idf.py -B <dir> fullclean` and
configure the build again with `set-target`. Save the build's `sdkconfig`
first if it has settings that are not in the defaults files: `fullclean`
deletes it. In ESP-IDF v6, `fullclean` also deletes the project's
`managed_components/`. The next configure of any build downloads the
components again, at the versions in `dependencies.lock`.

`idf.py -B build/arduitouch menuconfig` gives access to everything else: the
board choice, the beeper engine, the JTAG pin remap and the per-module debug
output under **OhEzTouch**.

To build all boards at once, use `tools/build_release.py`. See
[Update tool](update.md#release-builds).

### Home Assistant builds

A build contains exactly one server connector: openHAB or Home Assistant.
The connector is a build option (`CONFIG_OHEZ_CONNECTOR`), not a setting.
Without further options, a build is an openHAB build.

For a Home Assistant build, add `sdkconfig.defaults.homeassistant` after the
board's defaults file. Use a build directory of its own, named after the
board with `_ha` appended:

```bash
idf.py -B build/arduitouch_ha -DSDKCONFIG=build/arduitouch_ha/sdkconfig \
       -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.esp32;sdkconfig.defaults.arduitouch;sdkconfig.defaults.homeassistant" \
       set-target esp32
idf.py -B build/arduitouch_ha build
```

The same applies to the other boards: `build/arduitouch28_ha`,
`build/lanbon_ha` and `build/cyd_ha`. The simulator's Home Assistant build
is `build/linux_ha`, with
`-DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.linux;sdkconfig.defaults.homeassistant"`.

A Home Assistant build reports its target name with `-HA` appended, for
example `ArduiTouch-HA`. `tools/batchupdate.py` and the device manager
compare this name before an update. They do not put a Home Assistant image
on a panel that runs openHAB, or the reverse.

How to set up Home Assistant for the panel is in
[Home Assistant](homeassistant.md). The connector has been tested against
Home Assistant 2026.11.1 in the simulator, not yet on a panel. Until then,
`tools/build_release.py` builds the `_ha` targets only when `-t` names
them.

## Flash

One command uploads the bootloader, the partition table, the firmware and the
filesystem image built from `data/`:

```bash
idf.py -B build/arduitouch -p /dev/ttyUSB0 flash monitor
```

`flash` also resets the OTA data, so the panel boots the new firmware.
`idf.py app-flash` and `esptool.py write-flash 0x10000` write only the
firmware, to the first app partition (`app0`). After an OTA update, the
panel boots the second one (`app1`) and ignores what was written to `app0`.
The boot log then shows the old build. Erase the OTA data once before such
a flash: `idf.py -B build/arduitouch -p /dev/ttyUSB0 erase-otadata`.

`flash` also writes the filesystem image, which replaces the panel's
`config.json` with the default one from `data/`. To update a configured
panel and keep its configuration, write only the OTA data and the firmware.
This boots `app0` in every case and does not change the NVS or the
filesystem:

```bash
esptool.py -p /dev/ttyUSB0 write-flash \
    0xe000 build/arduitouch/ota_data_initial.bin \
    0x10000 build/arduitouch/oh-ez-touch.bin
```

The ArduiTouch boards have no USB port. They need a UART adapter. See
[ArduiTouch 2.4 inch](hardware/arduitouch.md#flashing-with-a-uart-adapter).

## Simulator

The user interface can also run on the development machine without hardware.
See [Simulator](simulator.md).

## Fonts

The LVGL font sources are generated and committed. A normal build needs no
font tooling. See [Fonts](fonts.md) for regeneration.
