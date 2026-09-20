# Configuration

A device can be configured in three places: on the panel's settings screen,
in the web interface, and over MQTT. All three read the same settings table
in `main/config/config_fields.cpp`. They cannot drift apart.

Settings marked `*` are only read while the device boots. They take effect
after a restart. Every other setting applies as soon as it is saved.

## WLAN setup

A new device has no WLAN credentials. It opens an open access point named
after its hostname (`oheztouch-new` by default). The screen shows the access
point name and the address to open.

1. Connect your phone to that open WLAN.
2. Open `http://192.168.4.1/`. There is no captive-portal popup. That is why
   the device shows the address itself.
3. Enter your network name and password in the WLAN section.
4. Press **Connect**. The device reconnects immediately. The access point
   closes a few seconds later.

A device with credentials that cannot reach its network opens the same access
point for ten minutes. Then it keeps retrying quietly. The access point is an
open network and the firmware update endpoint is unauthenticated. That is why
it does not stay up indefinitely.

**A device without WLAN credentials also opens the settings screen by itself
at boot**, on the WLAN page. The full setup can be done on the panel: scan,
pick the network, type the password, **Save**. No second device and no
browser are necessary.

> **NOTE:** The station speaks WPA2, not WPA3. `CONFIG_ESP_WIFI_ENABLE_WPA3_SAE`
> is off in `sdkconfig.defaults.esp32`. This saves 37 KB of flash. A
> mixed-mode WPA2/WPA3 access point accepts the panel as a WPA2 client. An
> access point configured for WPA3 *only* refuses the panel. The symptom is a
> panel that opens its own access point and keeps retrying. Turn the option
> on and rebuild for such a network.

## Settings on the screen

Press the upper bar to open the settings screen. The screen is a menu of
large cells. Each cell has a pictogram and a name. Press a cell to open that
section. The bar across the top of every page is the way back. From the first
menu, the **X** closes the screen. A section's own buttons (**Save**, and
**Scan** or **Restart** where they apply) are in a bar along the bottom.

| Page | Contents |
| --- | --- |
| Theme (eye symbol) | Theme family, the night variant and its schedule, the backlight levels and the dim timeout |
| Audio (speaker symbol) | The beeper: on or off, volume, and a **Test** button that plays the theme's boot chime at the level being edited |
| System (gear symbol) | A menu of the six sections below |
| &nbsp;&nbsp;WLAN | Network and password, plus a **Scan** button that lists the access points in range with their signal strength. Press one to fill in its name. **Save** stores the credentials and reconnects. |
| &nbsp;&nbsp;openHAB (house symbol) | Two lists: the openHAB servers on the network, and the sitemaps of the selected server. Press one to select it. **Scan** asks again. **Manual** opens a page with host, port and sitemap as fields. |
| &nbsp;&nbsp;MQTT (upload symbol) | Broker, port, credentials, and what to publish. See [MQTT](mqtt.md). |
| &nbsp;&nbsp;Sensors (location symbol) | The BME280 rows, and the BLE beacon scanner. |
| &nbsp;&nbsp;Device (pencil symbol) | The hostname. It is also the name of the setup access point. |
| &nbsp;&nbsp;Time (sync symbol) | The NTP host, the GMT offset and daylight saving. |
| Info (list symbol) | The system information table: uptime, version, IP address. And a **Restart** button. |

Press a row to open an on-screen keyboard for text and number settings.
Switches and drop-down settings toggle in place. Nothing is stored until you
press **Save** on that page. Leaving the screen discards your changes.

If a changed setting is one of the two that are only read at boot (the
hostname and the BME280 on/off), **Save** offers a restart.

## Web interface

Open `http://<hostname>/`. Everything is on that one page: a status block,
the WLAN section, all settings, and buttons for the firmware update and a
restart.

| Route | Purpose |
| --- | --- |
| `/` | Status, the WLAN section and all settings |
| `/save` | Stores the settings and redirects back to `/` |
| `/wifi` | Stores WLAN credentials and reconnects |
| `/restart` | Reboots the device |
| `/update` | Firmware upload, also used by `tools/batchupdate.py` |

### REST API

Since 0.91 there is also a REST API. It is meant for tooling. The
[device manager](devmgr.md) is its first client.

| Route | Purpose |
| --- | --- |
| `GET /api/status` | Version, target, uptime, network and heap as JSON. No side effects (no sitemap fetch, no mDNS scan). Safe to poll. |
| `GET /api/config` | Every setting with label, kind, value and range or options as JSON. Secrets are masked as `***`. |
| `POST /api/config` | A JSON object of changed settings. Absent fields are untouched. One rejected value rolls the whole request back. |
| `GET /api/sounds` | The sound vocabulary of the theme in force. |
| `POST /api/sound` | `{"name":"door_chime","force":true}` plays a sound. `force` plays it past the beeper mute, for locating a panel. |

> **WARNING:** None of these routes is authenticated. The setup access point
> is open. Anyone who can reach the device can reconfigure it or flash it.
> Use the device on trusted networks only.

### Device manager

`devmgr/` is a web-based fleet manager. It scans a subnet for devices,
refreshes them on an interval, edits settings on one device or on a
selection, updates firmware with a progress display, and plays a panel's door
chime to find it in the field. It needs nothing but Python 3:

```bash
python3 devmgr/devmgr.py            # then open http://localhost:8088
```

See [doc/devmgr.md](devmgr.md) for the full description.

## Settings reference

### Device

| Setting | Default | Description |
| --- | --- | --- |
| Hostname `*` | oheztouch-new | The hostname of this device. Also the name of the setup access point. |

### NTP Time

| Setting | Default | Description |
| --- | --- | --- |
| Host | pool.ntp.org | Host which serves the time. For example pool.ntp.org or your router. |
| GMT Offset | 1 | Offset of your timezone from Greenwich Mean Time. |
| Daylight Saving | 0 | Daylight saving +1 hour. |

### Appearance

| Setting | Default | Description |
| --- | --- | --- |
| Theme | Material | Look of the user interface: `Material`, `LCARS`, `JARVIS` or `Classic`. |
| Night mode | off | `off`, `on`, or `auto` to follow the clock. |
| Night from | 22 | Hour the night variant starts, when night mode is `auto`. |
| Night to | 6 | Hour the night variant ends, when night mode is `auto`. |

The theme takes effect as soon as it is saved, on the screen and in the
browser. `auto` needs the clock. It starts working once NTP has answered.

`Classic` is the blue-on-silver look of the original firmware. `Material` was
called `Default` before. Only the name changed. A panel that upgrades from an
older firmware keeps its look: an unknown theme name selects Material.
Anything that *writes* the name (an MQTT `config/theme` payload, an
`OHEZ_THEME` in a script) should use the new name. The old spelling works
only by fallback.

### LCD Backlight Dimming

| Setting | Default | Description |
| --- | --- | --- |
| Activity timeout | 60 | Seconds since the last touch before the display dims. |
| Normal Brightness | 100 | Normal brightness level in percent. |
| Dim Brightness | 40 | Dim brightness level in percent. |

### Beeper

| Setting | Default | Description |
| --- | --- | --- |
| Enable Beeper | On | Enable blips and bleeps. |
| Volume | 25 | 0 to 100. 0 is silent. |

Volume is PWM duty cycle. On a piezo, duty cycle is loudness only roughly.
The drive is a square wave. Its fundamental goes as `sin(pi x duty)`. 100
means the 50 % duty that is the loudest a pulse train can be. The default of
25 reproduces the duty every beep of this firmware has ever used. See the
file comment in `main/port/esp32/port_beeper.c`.

The panel has one piezo on one GPIO and one LEDC timer behind it. Only one
tone is available at a time. Two engines spend it differently. The default
engine plays a single note with an envelope, a glide and an ornament. The
other engine interleaves up to three voices at two milliseconds each to make
a real chord. Which engine is compiled in is a menuconfig choice,
`CONFIG_OHEZ_BEEPER_ENGINE`. See [The beeper](beeper.md).

**The Lanbon L8 has no buzzer.** Both settings do nothing there. The
simulator has a beeper. See [Hearing the panel](simulator.md#hearing-the-panel).

The **Demo** button on this page plays a thirty-second piece. It runs through
every envelope, every effect, both sweeps, the repeat count and the full
level range. It uses the volume being edited. The button becomes **Stop**
while it runs. The button exists only on a build with the default engine.

### openHAB Server

| Setting | Default | Description |
| --- | --- | --- |
| Host | openhabian | Hostname of the openHAB server. |
| Port | 8080 | Port. |
| Sitemap | oheztouch | Name of the sitemap for this device. |

The host and the port do not have to be looked up. openHAB announces itself
on the local network over mDNS (`_openhab-server._tcp`). Loading this page
sends the query. Every server that answers appears under the Host field as a
button. The button fills in the host and the port. The panel shows the same
servers as a list. The stored value is the address in digits. The panel has
no way to resolve the `.local` name a server gives for itself.

Discovery finds what announces itself. An access point that filters
multicast, or an openHAB in a Docker bridge network, is not heard. That is
what **Manual** is for.

The sitemap does not have to be typed from memory either. Loading this page
asks the selected server what it serves (`GET /rest/sitemaps`). The field
offers the sitemaps as a drop-down list. The panel holds twelve sitemaps and
says so when there are more.

On the panel the sitemap list is fetched from the host and port *as they are
being edited*. The web form fetches from the saved endpoint. There the order
is: pick a server, **Save**, then pick a sitemap from the reloaded page.

### MQTT Broker

| Setting | Default | Description |
| --- | --- | --- |
| Enable MQTT | off | Connect to the broker below and publish to it. |
| Host | mosquitto | Hostname of the MQTT broker. |
| Port | 1883 | Port. Plain TCP only. There is no TLS support. |
| User | | Leave empty for an anonymous broker. |
| Password | | Sent with the user name. Never shown in clear, and never published. |

### MQTT Publishing

| Setting | Default | Description |
| --- | --- | --- |
| Base topic | oheztouch | First segment of every topic. The hostname follows it. |
| Publish interval | 60 | Seconds between two rounds of system information. |
| Retain published values | On | Publish with the retain flag. A subscriber that connects later sees the current values at once. |

The MQTT settings take effect as soon as they are saved. The client
reconnects itself, and only when something it uses changed.

### Sensors

| Setting | Default | Description |
| --- | --- | --- |
| Use BME280 sensor `*` | off | Read the optional BME280 and publish it to MQTT. |
| Update interval | 180 | Seconds between two readings. |

A reading goes to the broker and nowhere else. See
[Published topics](mqtt.md#published-topics). An openHAB installation that
wants the readings subscribes to the three topics through its own MQTT
binding.

### Bluetooth LE Beacons

| Setting | Default | Description |
| --- | --- | --- |
| Scan for BLE beacons `*` | off | Listen for BLE advertisements and publish them over MQTT. See [Bluetooth LE beacons](ble.md). |
| Scan every | 30 | Seconds between the starts of two scan windows. |
| Scan for | 5 | Seconds each window lasts. Not continuous, because the radio is shared with WiFi. |
| Ignore weaker than | -90 | Advertisements below this RSSI are dropped. |
| Forget after | 120 | Seconds of silence before a beacon is dropped and its topics cleared. |
| Publish non-beacon devices | off | Publish plain BLE devices too, not only recognized beacons. |
