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

Without a board defaults file, the Kconfig default applies: the ArduiTouch
2.4".

`-DSDKCONFIG` is necessary when you build more than one target. Without it,
`idf.py` writes the generated `sdkconfig` to the project root. The builds
would overwrite each other's file. With the file inside the build directory,
`-B` alone identifies a build.

A defaults file only seeds a **new** `sdkconfig`. After you edit a defaults
file, delete the build's `sdkconfig` (or the whole build directory) and run
`set-target` again.

`idf.py -B build/arduitouch menuconfig` gives access to everything else: the
board choice, the beeper engine, the JTAG pin remap and the per-module debug
output under **OhEzTouch**.

To build all boards at once, use `tools/build_release.py`. See
[Update tool](update.md#release-builds).

## Flash

One command uploads the bootloader, the partition table, the firmware and the
filesystem image built from `data/`:

```bash
idf.py -B build/arduitouch -p /dev/ttyUSB0 flash monitor
```

The ArduiTouch boards have no USB port. They need a UART adapter. See
[ArduiTouch 2.4 inch](hardware/arduitouch.md#flashing-with-a-uart-adapter).

## Simulator

The user interface can also run on the development machine without hardware.
See [Simulator](simulator.md).

## Fonts

The LVGL font sources are generated and committed. A normal build needs no
font tooling. See [Fonts](fonts.md) for regeneration.
