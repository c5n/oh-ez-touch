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
pick the network, type the password, then leave the page. No second device
and no browser are necessary.

> **NOTE:** The station speaks WPA2, not WPA3. `CONFIG_ESP_WIFI_ENABLE_WPA3_SAE`
> is off in `sdkconfig.defaults.esp32`. This saves 37 KB of flash. A
> mixed-mode WPA2/WPA3 access point accepts the panel as a WPA2 client. An
> access point configured for WPA3 *only* refuses the panel. The symptom is a
> panel that opens its own access point and keeps retrying. Turn the option
> on and rebuild for such a network.

## Settings on the screen

Press the upper bar to open the settings screen. In LCARS that is the bar
with the page name and the clock, and the cell with the wifi symbol on the
left spine opens it too. The screen is a menu of
large cells. Each cell has a pictogram and a name. Press a cell to open that
section. The bar across the top of every page is the way back. From the first
menu, the **X** closes the screen. What a page can do (**Scan** where it
applies, **Test** on the Audio page) is in a bar along the bottom.

| Page | Contents |
| --- | --- |
| Theme (eye symbol) | Theme family, the night variant and its schedule, the backlight levels and the dim timeout |
| Audio (speaker symbol) | The beeper: an on/off switch, a volume slider, and a **Test** button that plays the theme's boot chime at the level being edited |
| System (gear symbol) | A menu of the seven sections below. Behind the System PIN, if one is set: see [PINs](#pins). |
| &nbsp;&nbsp;WLAN | Network and password, plus a **Scan** button that lists the access points in range with their signal strength. Press one to fill in its name. Leaving the page stores the credentials and reconnects. |
| &nbsp;&nbsp;openHAB (house symbol) | Two lists: the openHAB servers on the network, and the sitemaps of the selected server. Press one to select it. **Scan** asks again. **Manual** opens a page with host, port and sitemap as fields. |
| &nbsp;&nbsp;MQTT (upload symbol) | Broker, port, credentials, and what to publish. See [MQTT](mqtt.md). |
| &nbsp;&nbsp;Sensors (location symbol) | The BME280 rows, and the BLE beacon scanner. |
| &nbsp;&nbsp;Device (pencil symbol) | The hostname. It is also the name of the setup access point. **PINs** in the bottom bar sets, changes and removes the two PINs. |
| &nbsp;&nbsp;Touch (keyboard symbol) | The four touchscreen calibration numbers, and a **Calibrate** button. See [Calibrating the touchscreen](#calibrating-the-touchscreen). |
| &nbsp;&nbsp;Time (sync symbol) | The NTP host, the GMT offset and daylight saving. |
| Info (list symbol) | The system information, one topic per page -- the firmware, the network addresses, the radio link, the sensors and relays -- turned by the arrows in the bottom bar. |

Every setting is a row with the name on the left and the value's control
beside it: an on/off switch for the two-state settings, a slider for values
over a range that fits one (the volume, the brightnesses, the night hours),
a button showing the current value for everything else. Press a value button
to open an on-screen keyboard, or to step an option through its choices.
A section whose rows do not fit on the screen is split into pages of its
own logical groups, turned by the arrows in the bottom bar; nothing scrolls.

**Leaving a page saves it.** There is no Save button: a value changed is
stored, and applied at once where it can be, when you leave the page -- by
the back bar, by opening another section, or by closing the screen.

If a changed setting is one of the few that are only read at boot (the
hostname, the BME280 and BLE on/off, the orientation), the panel asks
whether to restart once it has left the page.

## PINs

The panel has two PINs, and both are optional. Neither is set on a new device.

| PIN | What it protects |
| --- | --- |
| System PIN | The **System** entry of the settings screen, and so WLAN, openHAB, MQTT, Sensors, Device, Touch and Time. Theme, Audio and Info stay open. |
| Item PIN | Sitemap tiles whose openHAB item has the tag `ohez-pin` |

The two are separate on purpose. Someone who may switch a protected light
does not have to be someone who may reconfigure the panel. A PIN is 4 to 8
digits. The panel will not set one PIN to the other's value, because then
knowing one would mean knowing both.

**Setting them.** Open System › Device and press **PINs** in the bottom
bar. Each PIN has **Set**, or **Change** and **Remove** once it exists. A new
PIN is typed twice. The Item PIN's line also says how many tiles on the page
under the settings carry the tag, which is a quick way to check that the tag
in openHAB took effect.

**Using them.** A tap on something protected brings up a keypad. After the
correct PIN, the tap goes through. The PIN then stays unlocked until the
panel goes idle: the backlight dims, or a minute passes without a touch,
whichever comes first. A System page that is open at that moment goes back
to the settings menu, and its changes are saved, the same as leaving any
page. The two PINs unlock separately: the Item PIN opens tagged tiles and
nothing else.

Five wrong entries lock the keypad for 30 seconds. Each further five
doubles the wait, up to five minutes. The count lives in memory, so a
restart resets it.

**Protecting an item.** Add the tag in openHAB, for example in a `.items`
file:

```
Switch Garage_Door "Garage door" ["ohez-pin"]
```

The tag is exact and case-sensitive. It protects whatever a tap on the tile
does: a switch's toggle, a slider's or colour picker's control screen, and,
on a Group or a Text widget with an item, the sub-page it opens. A plain
`Text label="..." { ... }` sub-page has no item, so it cannot carry a tag.
Read-only tiles (numbers and strings) are never gated, because a tap on
them does nothing.

**Forgot a PIN?** Remove it from the web interface: the `/` page has a
**Remove System PIN** or **Remove Item PIN** button while a PIN is set, and
`POST /api/pin` does the same (see [REST API](#rest-api)). The PINs are
stored in NVS as salted SHA-256 hashes, never as digits. They survive a
firmware flash and an update, and they never appear in `/api/config`, MQTT
or the settings file.

> **NOTE:** The PINs keep people at the panel out, not the network. The web
> interface is unauthenticated (see the warning under
> [REST API](#rest-api)), and anyone who can reach it can remove a PIN.

## Calibrating the touchscreen

The resistive panels vary from unit to unit. The firmware ships with the
constants the board's published examples agree on. If your panel is a few
pixels out at the edges, calibrate it.

Open **System** and then **Touch**, and press **Calibrate**. The screen shows
a cross in one corner. Press the centre of it. It does this four times.

Press the centre of each cross as you see it, not where you think the panel
will read it. The calibration in force is still the wrong one during the
procedure. The screen does not use the position it reports; it uses the raw
reading and the place the cross was drawn. This is why the procedure works on
a panel that is too far out to operate normally.

After the fourth cross the screen reports the result.

- A sentence gives the worst error the old calibration had at the four places
  you pressed, and the worst the new one still has.
- A diagram draws the panel. A cross marks each place you were asked to press.
  A dot marks where the old calibration put your finger. The line between them
  is the error. The dots are offset at true size on a panel drawn smaller, so
  that errors of a few pixels are visible.
- A table gives the four constants: the old value, the new value and the
  difference.

**Keep** stores the new calibration, applies it at once and writes it to the
file. **Retry** starts again. **Discard** changes nothing.

A press that slides more than ten pixels is not counted. Press again if a cross
does not advance: a second press where the last one was is dropped rather than
counted twice, so pressing again never spoils a measurement.

The procedure refuses a result it cannot trust and says why. Nothing is stored
in that case:

| Message | Meaning |
| --- | --- |
| Taps too close together | The four presses did not cover the screen. |
| Two taps disagree. Try again | Two presses at the same end of an axis read very differently. One of them landed somewhere else. |
| Panel is mirrored. See board_pins.h | An axis runs backwards. That is how the panel is wired, not a calibration. Use `OHEZ_TOUCH_FLIP` in `main/port/esp32/board_pins.h`. |
| Readings out of range | The panel did not report usable values. |

The capacitive panel of the Lanbon L8 reports the pixel grid it is bonded to.
It has no calibration and the **Calibrate** button is not shown.

### If the panel is too far out to reach the button

The four numbers are ordinary settings. They are in the web form, in
`GET/POST /api/config` as `touch_x_org`, `touch_x_span`, `touch_y_org` and
`touch_y_span`, and on the MQTT configuration topic. Set all four to `0` to
return to the constants the firmware was built with.

## Web interface

Open `http://<hostname>/`. Everything is on that one page: a status block,
the WLAN section, all settings, and buttons for the firmware update and a
restart.

| Route | Purpose |
| --- | --- |
| `/` | Status, the WLAN section and all settings |
| `/save` | Stores the settings and redirects back to `/` |
| `/wifi` | Stores WLAN credentials and reconnects |
| `/pin/clear` | Removes a PIN (`scope=system` or `scope=item`) and redirects back to `/` |
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
| `GET /api/pin` | `{"system":{"set":true},"item":{"set":false}}`: whether each PIN is set. Never the PIN. |
| `POST /api/pin` | `{"clear":"system"}` or `{"clear":"item"}` removes that PIN. There is no way to set one over the network; that is done at the panel. |

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

### Touch panel

Raw ADC counts. `0` means the constants this board was built with, which is
what every panel starts on. The second number of each pair is a **span**: the
raw distance across the whole screen, not the reading at its far edge. See
[Calibrating the touchscreen](#calibrating-the-touchscreen).

| Setting | Default | Description |
| --- | --- | --- |
| X origin (raw) | 0 | The reading at one edge of the screen's x axis. |
| X span (raw) | 0 | The raw distance across the screen's x axis. |
| Y origin (raw) | 0 | The reading at one edge of the screen's y axis. |
| Y span (raw) | 0 | The raw distance across the screen's y axis. |

### NTP Time

| Setting | Default | Description |
| --- | --- | --- |
| Host | pool.ntp.org | Host which serves the time. For example pool.ntp.org or your router. |
| GMT Offset | 1 | Offset of your timezone from Greenwich Mean Time. |
| Daylight Saving | 1 | On: the EU rule switches automatically -- one hour ahead from the last Sunday in March to the last Sunday in October, both at 01:00 UTC. Off for a zone with no daylight saving. |

### Appearance

| Setting | Default | Description |
| --- | --- | --- |
| Theme | Material | Look of the user interface: `Material`, `LCARS`, `JARVIS` or `Classic`. |
| Night mode | off | `off`, `on`, or `auto` to follow the clock. |
| Orientation `*` | landscape | `landscape` is the 320x240 every panel has always had. `portrait` mounts the panel upright at 240x320: all four themes re-lay-out, the six tiles become two columns of three. |
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
| Show time and date when dimmed | Off | Show the clock screen when the display dims: the time, and under it the weekday and date or the home page's clock items. The next touch wakes the panel on the home page. |
| Clock text (day) | `#ffffff` | Text colour of the clock screen while the theme's day variant is in effect. |
| Clock background (day) | `#000000` | Its background colour, by day. |
| Clock text (night) | `#ffffff` | Text colour while the night variant is in effect (see Night mode). |
| Clock background (night) | `#000000` | Its background colour, by night. |
| Clock items frame | `Clock` | The label of the `Frame` on the sitemap's home page whose items the clock screen shows. Empty for none. |

When the activity timeout fires, the panel goes back to the sitemap's home
page. This happens whether or not the clock screen is on. An open item window
is closed. The settings screen is left alone.

With the clock screen on, the dim goes through black. The backlight fades
out over 1.5 s, the clock screen goes up in the dark, and the backlight fades
in to the dim level. A touch reverses it quickly: a 0.1 s dip to black, then
the page, then the normal level. The waking touch never reaches a tile. A
touch during the slow fade-out turns it straight back. Nothing is swapped then.

**Colours** are `#rrggbb` everywhere: in `config.json`, the web form's colour
picker, `/api/config` and MQTT. The panel's settings screen offers a palette of
twelve named colours. A value set elsewhere that is not one of them shows as
"Custom". The day and night pairs follow the theme variant in effect, so with
Night mode `auto` the clock screen changes with the schedule.

**Clock items.** Under the time, the clock screen shows the weekday and the
date. If the home page has a `Frame` with the configured label, the first
three items of that frame are shown instead: icon, value and name. They are
not drawn as tiles on the page. Icons from the built-in set take the text
colour. Icons served by openHAB keep their own colours.

```
sitemap home label="Home" {
    Frame label="Clock" {
        Text item=Outside_Temperature label="Outside" icon="temperature"
        Text item=Living_Humidity     label="Humidity" icon="humidity"
        Switch item=gLights           label="Lights"   icon="light"
    }
    // ... the tiles
}
```

Any other `Frame` is treated as a heading: its items become tiles of the page
in order.
See [The clock frame](sitemap.md#the-clock-frame) for the rules.

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

Item states reach the panel without polling. The panel keeps
`GET /rest/events` open for the items on screen, and openHAB sends each change
as it happens. The panel asks for no token, so that endpoint, like the sitemap
and the item states, has to be open to an anonymous client. That is openHAB's
default ("implicit user role"). While the stream is unavailable the panel
polls every tile every five seconds instead. While it is up, the panel still
checks each tile once a minute, and five seconds after you operate it. Nothing
has to be configured either way.

On the panel the sitemap list is fetched from the host and port *as they are
being edited*. The web form fetches from the saved endpoint. There the order
is: pick a server, **Save**, then pick a sitemap from the reloaded page.

### Doorbell

| Setting | Default | Description |
| --- | --- | --- |
| Ring item | | The item that turns ON (a Switch) or OPEN (a Contact) when the bell is pressed. Empty turns the doorbell off. |
| Picture item | | The Image item with the doorbell's snapshot. |
| Show picture for | 30 | Seconds until the picture goes away again. A touch keeps it up. 0 keeps it up until the back bar. |

On the panel these three are behind **Manual** on the openHAB page, after the
server's fields.

When the ring item changes to ON or OPEN, the panel wakes up, chimes and shows
the picture item's picture over whatever is on screen. A second ring while it
is up fetches the picture again. The ring item does not have to be on any
sitemap page: the panel keeps a second `GET /rest/events` open for that one
item, as long as both names are set. It costs about 8 KB of RAM.

Many doorbell bindings report a press on a trigger channel, not on an item.
Turn it into a Switch with a rule, or link the channel to a Switch item with
a profile that sets it ON, and switch it OFF again after a few seconds (for
example with `expire="5s,command=OFF"` in the item's metadata). Only a change
rings, so an item that stays ON rings once.

How the picture is fetched and drawn is in
[Image](sitemap.md#image-a-doorbell-or-a-camera). The demo has a doorbell to
try it on: see [the Door page](openHAB/README.md#door-a-doorbells-picture).

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
| Active scan | off | Ask scannable advertisers for their scan response, which is where a phone or a watch carries its name and most of its data. Off by default: the ask costs transmit airtime, which comes out of WiFi's share. Read at the start of each window, so a change applies without a restart. |
| Ignore weaker than | -90 | Advertisements below this RSSI are dropped. |
| Forget after | 120 | Seconds of silence before a beacon is dropped and its topics cleared. |
| Publish non-beacon devices | off | Publish plain BLE devices too, not only recognized beacons. |
