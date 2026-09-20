# The device manager (devmgr/)

A web-based management tool for a fleet of OhEzTouch panels: one Python
script, one page on localhost, nothing but Python 3 installed. It finds
devices, watches them, configures them one at a time or in batches, updates
their firmware with a progress display, and plays a panel's door chime when
the panel's position is the thing you have forgotten.

    python3 devmgr/devmgr.py            # then open http://localhost:8088

Devices are managed over the firmware's REST API, so they must run **0.91 or
later**. Older firmware has no API to manage; update it first, the way
tools/batchupdate.py always did it.

| Option          | Default | Meaning                                              |
| --------------- | ------- | ---------------------------------------------------- |
| `--port`        | 8088    | Where the manager's page is served                   |
| `--device-port` | 80      | Where devices serve their API. The Linux simulator uses 8780, which is why this option exists. |

## What it knows, and where it keeps it

Everything the manager persists lives in `devmgr/data/devmgr.json` (kept out
of the repository by .gitignore, because it contains your real addresses):

* **Settings** -- the subnet to scan, the refresh interval (10 s default,
  5/10/30/60 s in the settings panel), whether the regular refresh runs at
  all (off by default; scanning and the manual refresh work regardless), and
  the log level.
* **Devices** -- every device ever found, keyed by its **MAC**, the one
  property DHCP cannot change. A device that answers a refresh is online and
  its address, hostname, version, signal strength and uptime are re-learnt;
  one that does not is shown greyed-out with its last-seen time, and stays
  in the list until you remove it. Vanished must not mean forgotten.

## The page

* **Scan** -- probes every address of the configured subnet with one
  `GET /api/status` each, in parallel, two seconds of patience per answer.
  A scan runs in the background; the progress bar is live. Devices that
  answer are merged into the list by MAC, so a re-scan updates rather than
  duplicates.
* **Refresh** -- the scheduler re-reads every known device's status at the
  configured interval. Transitions are what matter: "went offline" and "is
  back" are logged, a device that is simply still there is not.
* **Configure** (per device, gear button) -- reads the device's own
  `GET /api/config`, which carries every setting with label, kind, current
  value and range or options, grouped into the same tabs the touch screen
  uses (Device, Time, Theme, Audio, openHAB, MQTT, Sensors). Only the fields
  you actually change are sent; the Save button stays grey until there is
  something to send. Secret fields (the MQTT password) arrive masked as
  `***` and are only re-sent if you type a new value.
* **Bulk edit** (select devices, then "Bulk edit") -- the same form, with a
  checkbox per field opting it into the change. The changed fields are sent
  to every selected device in parallel, and the result is reported per
  device.
* **Firmware update** -- per device or for a selection. The image is either
  picked automatically per target (newest `release/X.Y/oh-ez-touch-X.Y-
  <target>.bin`, falling back to the build tree) or uploaded as a `.bin`,
  which then goes to every selected device. Each update is checked, streamed
  with a live percent, and -- the part that matters -- **verified** after
  the reboot: the device must answer `/api/status` with the expected version
  and target. A 200 from the upload alone proves nothing; the old firmware
  answers 200 even when the flash failed.
* **Chime** (bell button) -- plays the device's door chime *forced*, past a
  muted beeper, because the panel you are looking for is exactly the one
  whose settings you do not know. The mute setting itself is not touched;
  the chime after yours is quiet again.
* **Restart** -- per device or for a selection.
* **Comment and delete** -- the pencil attaches a note ("hallway, 2.8
  inch") that survives restarts; the cross removes a device from the list.
* **Console** -- the drawer at the bottom shows what the manager is doing in
  the background: scans, refreshes, every request to a device with its
  outcome, saves, update phases. Filterable by level and source, pausable,
  copyable for bug reports. While it is closed, a badge on the Console
  button counts warnings and errors you have not seen.

Deep links: `http://localhost:8088/#config=<mac>` opens a device's settings
dialog directly.

## The manager's own API

The page is a client of a small JSON API, which scripts may use too:

| Route | Purpose |
| ----- | ------- |
| `GET /api/state` | Settings, devices, scan progress, update progress |
| `POST /api/settings` | Change subnet / interval / refresh / log level (persisted) |
| `POST /api/scan` | Start a background scan: `{"subnet": "192.168.1.0/24"}` |
| `POST /api/refresh` | Refresh all devices now |
| `GET /api/log?since=N&level=L` | Console entries after cursor N |
| `POST /api/log/clear` | Clear the console |
| `GET /api/device/<mac>/config` | The device's settings schema and values (proxied) |
| `POST /api/device/<mac>/config` | Save changed fields to one device |
| `POST /api/devices/config` | Bulk save: `{"macs": [...], "fields": {...}}`, per-device results |
| `POST /api/device/<mac>/restart` | Restart a device |
| `POST /api/device/<mac>/chime` | Play the door chime, forced |
| `POST /api/device/<mac>/comment` | Set the note |
| `POST /api/device/<mac>/delete` | Remove from the list |
| `GET /api/update/images` | The images available per target |
| `POST /api/update/upload` | Store a `.bin` for the next run (raw body, `X-Filename` header) |
| `POST /api/update` | Start updates: `{"macs": [...], "image": "auto"}` or `{"image": "upload", "name": "x.bin"}` |

## Rules worth knowing

* **Absent means untouched.** The device's `POST /api/config` applies only
  the fields it is given, and one rejected value rolls the whole request
  back -- nothing is persisted, on that device, for that request. This is
  deliberately unlike the device's web form (`/save`), where an unchecked
  checkbox is simply absent from the body and would read as "off".
* **Secrets are write-only.** `GET /api/config` masks them as `***`; sending
  the mask back is a no-op, sending a real value sets it.
* **Forced chimes are one-shot.** The force flag travels with the single
  queued sound and is gone with it; the panel's mute setting is never
  changed.
* **The console logs the manager, not the panels.** What a device says on
  its own serial line does not travel over HTTP; the console shows every
  request the manager makes and what came back, which is the part that is
  otherwise invisible.
