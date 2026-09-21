# MQTT

The client publishes what the panel knows about itself. It subscribes to:

- one wildcard through which every setting can be written,
- one topic that plays a sound,
- and — on a board that has them — the topics that drive the relays and the
  LEDs.

MQTT is off by default. The settings are on the `MQTT` page of the settings
screen and in the web interface.

Every topic starts with `<base topic>/<hostname>`. With the defaults that is
`oheztouch/oheztouch-new`. The hostname is part of the prefix so that a
second new panel does not publish over the first.

> **WARNING:** There is no authentication in front of the MQTT interface.
> The same is true for the web interface. Use both on trusted networks only.

## Published topics

| Topic | Published | Value |
| --- | --- | --- |
| `status` | on connect | `online`, and `offline` as the last will |
| `system/name` | on connect | The hostname |
| `system/target` | on connect | Which board this firmware is for, e.g. `ArduiTouch` |
| `system/version` | on connect | e.g. `0.20` |
| `system/build` | on connect | Compiler date and time |
| `system/git` | on connect | The commit this firmware was built from |
| `system/uptime` | every interval | Seconds since boot |
| `system/heap` | every interval | Free heap in bytes |
| `system/heapblock` | every interval | The largest single free block, in bytes. Absent where the target cannot answer it. Well below `system/heap` means a fragmented heap. |
| `system/fps` | every interval | Frames per second, one decimal. Absent until the screen has drawn. |
| `system/render_us` | every interval | Of a frame, microseconds in the software renderer |
| `system/wait_us` | every interval | Of a frame, microseconds waiting for the panel. See [Where the frame time goes](architecture.md#where-the-frame-time-goes). |
| `system/ip` | every interval | The station address |
| `system/ssid` | every interval | The network, or the interface name on the simulator |
| `system/rssi` | every interval | dBm. Absent where there is no radio. |
| `system/quality` | every interval | The same as a percentage, on the scale the status bar uses |
| `ui/night` | every interval | `ON` while the night variant is in effect |
| `sensor/temperature` | on each reading | Degrees Celsius |
| `sensor/humidity` | on each reading | Percent relative humidity |
| `sensor/pressure` | on each reading | hPa |
| `config/<setting>` | on connect, and after every save | One topic per setting |
| `relay/<n>` | on change, and on connect | `ON` or `OFF`. Only on a board with relays. |
| `led/<name>` | on change, and on connect | `0` to `100`. Only on a board with LEDs. |

Everything is published at QoS 0. Everything is retained unless **Retain
published values** is turned off. Every topic carries the current value of
something. A subscriber that missed an update wants the newest value, not the
missed one. That is what retain gives it.

The sensor topics need **Use BME280 sensor** turned on. They are the only
place a reading goes. The relay and LED topics need a board that has the
hardware. See [Relays and LEDs](#relays-and-leds).

## Writing a setting

`config/<setting>/set` writes the setting and saves it. The `<setting>` names
are the POST argument names in `main/config/config_fields.cpp`: `theme`,
`night_mode`, `bl_normal`, `oh_host` and so on. This is the same list the web
form posts.

```bash
mosquitto_pub -t oheztouch/oheztouch-new/config/theme/set -m LCARS
mosquitto_pub -t oheztouch/oheztouch-new/config/night_mode/set -m auto
mosquitto_pub -t oheztouch/oheztouch-new/config/bl_dim/set -m 20
```

The ranges, the character rules and the option names are the ones the
settings screen and the web form enforce. A number outside its range is
clamped. A hostname with `/` or `:` is dropped. An unknown option name
selects the first option. A checkbox takes `ON`, `true`, `yes` or `1`.
Anything else is off.

Two exceptions:

- The broker password is never published and cannot be set this way.
- A setting marked `*` (read at boot only) is stored, but takes effect after
  a restart.

Nothing is written to flash when the value did not change. A broker that
replays a retained command on every reconnect costs nothing.

## Playing a sound

`sound/set` plays one sound of the theme in force. The payload is the sound's
name. The eighteen names are the vocabulary in `main/ui/ui_beep.hpp`:

```
press        tick         tick_back    toggle_on    toggle_off   change
accept       cancel       link         link_back    screen       screen_out
notify       warning      error        boot         wake         door_chime
```

```bash
mosquitto_pub -t oheztouch/oheztouch-new/sound/set -m door_chime
mosquitto_pub -t oheztouch/oheztouch-new/sound/set -m notify
mosquitto_pub -t oheztouch/oheztouch-new/sound/set -m error
```

The name is matched case-insensitively. An empty payload plays nothing. Any
other unknown name is logged and ignored. What a name sounds like is the
theme's decision. `notify` on a Material panel and `notify` on an LCARS panel
are two different sounds.

`door_chime` is the one sound nothing on the panel plays by itself. No
gesture on a touchscreen means "somebody is at the door". Wire the topic to a
doorbell and the panel answers it in the voice of the active theme.

Two things to know:

- **This topic is an event, not a state.** A *retained* `sound/set` message
  is dropped. Everything else is replayed by the broker after a reconnect on
  purpose. A panel that beeped every time the broker restarted would be a
  panel somebody unplugs. Publish without `-r`.
- **It obeys the beeper settings.** A panel with the beeper off, or the
  volume at zero, stays quiet.

## Relays and LEDs

The [Lanbon L8](hardware/lanbon.md) has three mains relays and an RGB mood
light. The [Cheap Yellow Display](hardware/cyd.md) has the green and blue
channels of its RGB LED. Both are driven over MQTT and nothing else. There is
no setting, no widget on the screen and no openHAB item. A relay answers a
topic. That is the whole interface.

Nothing appears on a board that does not have the hardware. The ArduiTouch
boards publish and subscribe to none of it.

| Topic | Direction | Value |
| --- | --- | --- |
| `relay/<n>/set` | in | `ON`, `OFF` or `TOGGLE` |
| `relay/<n>` | out | `ON` or `OFF`, the state now |
| `led/<name>/set` | in | `0` to `100`, or `ON` / `OFF` |
| `led/<name>` | out | `0` to `100`, the brightness now |

The relays are numbered from 1, as they are on the wall plate: `relay/1` to
`relay/3` on an L8-HS. The LEDs are named: `led/red`, `led/green` and
`led/blue`. They are three independent brightnesses, not one colour. What to
mix from them is a decision for whatever is publishing.

```bash
mosquitto_pub -t oheztouch/oheztouch-new/relay/1/set -m ON
mosquitto_pub -t oheztouch/oheztouch-new/relay/2/set -m TOGGLE
mosquitto_pub -t oheztouch/oheztouch-new/led/red/set -m 100
mosquitto_pub -t oheztouch/oheztouch-new/led/green/set -m 40
```

A `relay/<n>/set` payload is `ON`, `OFF`, `TRUE`, `FALSE`, `YES`, `NO`, `1`
or `0`, case-insensitive, plus `TOGGLE` for a push-button that does not know
the current state. Anything unrecognized is off. That is the direction a
mains switch should fail in.

A `led/<name>/set` payload is a number from 0 to 100 (an openHAB Dimmer
percentage), or `ON` and `OFF` for the two ends. Where the two readings could
disagree, the number wins: `1` is one percent, not "on".

Nothing is saved. The outputs come up off after a reboot. The broker's
retained `set` messages put them back a second after the connection. The
state topics are published on every change and again after a reconnect.

To use a relay or LED from openHAB, bind it as an MQTT Thing channel:

```
Type switch : hallLight "Hall light" [ stateTopic="oheztouch/oheztouch-new/relay/1",
                                       commandTopic="oheztouch/oheztouch-new/relay/1/set" ]
Type dimmer : moodRed    "Mood red"   [ stateTopic="oheztouch/oheztouch-new/led/red",
                                        commandTopic="oheztouch/oheztouch-new/led/red/set" ]
```

Put that item in the sitemap and the panel draws a switch for it like any
other. That is why there is no built-in widget for the local relay.

The simulator has neither relays nor LEDs by default. `OHEZ_OUTPUTS=1` gives
it three relays and a three-channel mood light that exist only as log lines.
See [Simulator](simulator.md#environment-overrides).
