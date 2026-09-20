# Updating devices

Devices are updated over the air. `tools/batchupdate.py` updates one or more
devices. It needs nothing but Python 3.

An update changes neither the settings nor the WLAN credentials. OTA writes
only the inactive app partition. `config.json` on the SPIFFS partition and
the credentials in NVS are left alone.

A device updated from firmware older than 0.90 keeps its look and its sensor
settings:

- A `config.json` without a `ui` section gets the Classic theme.
- The BME280 settings are read from their old place under `openhab.sensors`.
- The AutoConnect WLAN credentials are migrated on first boot.

## The device list

The devices are named in a JSON list file. Each entry has the build target
its hardware needs. The target cannot be asked for remotely, and a fleet is
usually mixed. The target is the name of a build directory under `build/`.

Example `myOhEzTouchDevices.json` (see `tools/devices.example.json`):

```json
{
  "devices": [
    { "host": "oheztouch-01", "target": "arduitouch" },
    { "host": "oheztouch-02", "target": "arduitouch28", "comment": "hallway, 2.8 inch" },
    { "host": "192.168.1.50", "target": "lanbon" }
  ]
}
```

The old PlatformIO target names (`ArduiTouch`, `ArduiTouch28`, `Lanbon`) from
pre-0.90 list files are accepted as aliases.

### Discovering devices

`tools/discover.py` scans a subnet for devices and prints what it finds as a
table. With `-o` the result is written in the list file format:

```bash
./tools/discover.py 192.168.1.0/24                      # just look
./tools/discover.py 192.168.1.0/24 -o myOhEzTouchDevices.json
```

Devices on 0.90 or later answer with their version and target. Their entries
come out complete. Devices on older firmware are recognized by their
AutoConnect pages, but they can tell neither version nor target over HTTP.
Their entries come out with `"target": null`. This is deliberate:
`batchupdate.py` refuses such a file until a person has filled in which board
each device is. The target decides which image the device gets. A pre-0.90
device does tell its configured hostname: the scanner reads it from the
device's openhab_settings page into the entry's `"comment"`.

The recorded `"version"` is used too. A device already running the expected
version is skipped. Re-scanning after a partial rollout yields a list that
updates only what is left.

## Usage

```
Usage:
    ./tools/batchupdate.py myOhEzTouchDevices.json [options]
    ./tools/batchupdate.py -t <target> <hostname1> <hostname2> ...

    -p, --parallel N    update N devices at once (default 1)
    -i, --interactive   ask before every device: yes, skip or abort
    --minimal           flash the board-independent recovery image instead
    --release X.Y       take images from release/X.Y (default: the latest)
    --timeout S         per-device reboot and verify wait (default 120)
    --retries N         upload attempts per device (default 1)
    --dry-run           validate the list, the images and reachability only
    -o, --retry-file F  where failed devices are written (default retry.json)
```

For each device the script:

1. Checks that the firmware image exists.
2. Checks that the device answers HTTP.
3. POSTs the image to `/update`.
4. Waits for the device to come back.
5. Reads its status page. The version and the target must match.

Step 5 is necessary because the pre-0.90 firmware answers HTTP 200 even when
the flash write failed. A successful POST proves nothing. Only a device back
up with the expected firmware counts as a success.

While the script runs, a terminal shows one live line per device in flight
(checking, uploading with a percentage, waiting for the reboot, verifying)
above a progress bar. Piped into a file, each state change is a plain line.

Failed devices are written to the retry file in the same JSON format. A re-run
is just `./tools/batchupdate.py retry.json`. The exit code is the number of
failed devices.

## Release builds

One command builds all hardware targets clean and collects the images,
version-labelled, under `release/<version>/`:

```bash
./tools/build_release.py
```

The build stays incremental and in place with `--dirty`. `-t` builds a
subset.

The update tool takes images from the latest release by default. `--release
X.Y` pins an older one. A target missing from the release falls back to its
build directory under `build/`. The source of every image is printed before
anything is flashed.

## Rollout procedure

1. Check that everything is reachable and every image is built. Flash
   nothing:

   ```bash
   ./tools/batchupdate.py --dry-run myOhEzTouchDevices.json
   ```

2. Update one device per target type first:

   ```bash
   ./tools/batchupdate.py -t <target> <hostname1> <hostname2> ...
   ```

3. Update the rest in parallel:

   ```bash
   ./tools/batchupdate.py -p 4 myOhEzTouchDevices.json
   ```

An alternative is the interactive mode. Each device asks before anything is
sent: yes updates it, skip leaves it for the retry file, abort stops the run.

```bash
./tools/batchupdate.py -i myOhEzTouchDevices.json
```

## The minimal recovery firmware

A fifth build, `minimal`, is the OTA rescue path. It is not a panel. It has
no display, no touch, no openHAB, no MQTT, no sensors and no sound. It has
only WLAN (the stored credentials, or the setup access point), a small status
page and the `/update` upload.

It drives nothing board-specific. **One image fits every board.** At roughly
40 % of the full image's size, it is the fast way onto a device whose old
firmware writes an upload to flash a byte at a time. From there, the full
image goes up at the fixed firmware's speed.

Build it like a hardware target:

```bash
idf.py -B build/minimal -DSDKCONFIG=build/minimal/sdkconfig \
       -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.esp32;sdkconfig.defaults.minimal" \
       set-target esp32
idf.py -B build/minimal build
```

`tools/build_release.py` builds it alongside the hardware targets.
`tools/batchupdate.py --minimal` flashes it to every selected device.
Verification expects the recovery build's own target name (`Minimal`). A
device that comes back on its old firmware still fails the run.

The typical rescue procedure:

1. Flash the recovery image with `--minimal`.
2. Run again without `--minimal` for the full image.
