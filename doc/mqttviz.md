# The MQTT visualizer (mqttviz/)

A canvas view of every OhEzTouch panel on the broker: one Python script,
one page on localhost, nothing but Python 3 installed -- the same rules
the [device manager](devmgr.md) plays by, applied to the other half of the
panel's remote interface. Where devmgr talks to the REST API over HTTP,
mqttviz subscribes to the broker and watches what the panels publish.

    python3 mqttviz/mqttviz.py            # then open http://localhost:8089

Each panel found under `<base topic>/<hostname>/` becomes one box on a
canvas: a live picture of its MQTT data, and -- where the panel has
relays, LEDs or sounds -- a remote control for them.

| Option          | Default | Meaning                                            |
| --------------- | ------- | -------------------------------------------------- |
| `--port`        | 8089    | Where the page is served                           |
| `--data-dir`    | `data/` | Keep the state elsewhere -- how a second instance or a test run avoids touching the live configuration |
| `--mqtt-host`   | profile | The broker to watch (default `localhost`)          |
| `--mqtt-port`   | profile | The broker's port (default 1883)                   |
| `--user`        | profile | The broker username, if any                        |
| `--password`    | profile | The broker password, if any                        |
| `--base`        | profile | The base topic to watch (default `oheztouch`)     |
| `--no-mqtt`     | off     | Start without connecting to the broker             |

Command line arguments are written into the settings -- into the profile
the tool is connected to, that is -- so what was typed once is what the
next start remembers.

## What it knows, and where it keeps it

Everything the visualizer persists lives in `mqttviz/data/mqttviz.json`
(kept out of the repository, because it contains your broker passwords):

* **Settings** -- the broker profiles and which of them is in use,
  whether the connection is on at all, and how verbosely incoming
  messages are logged.
* **Devices** -- every panel that has ever published under the base
  topic, keyed by its hostname, with the newest value of every topic it
  publishes. A panel whose last will arrived is drawn greyed-out with its
  last-seen time, and stays in the list until you remove it.
* **Beacons** -- every BLE advertiser the panels report, keyed by its
  address, kept in memory only: radio contacts are rebuilt from the
  broker's retained messages on every start, and an advertiser the
  firmware cleared is gone from the list the moment the empty publish
  arrives.

## Brokers

Any number of broker profiles can be kept: each one is a name, a host,
port, credentials and its own base topic -- a complete world of panels.
The list is alphabetical everywhere it is shown, and the entry being
connected follows its profile through renames and re-sorts rather than
its position. The selector in the top bar switches between profiles,
which reconnects with the chosen one; the config dialog (*Config*)
manages the list itself: add a broker, remove one, edit any of them,
and one save makes it all true. The passwords are masked the way devmgr
masks its secrets, and a mask sent back means "leave it alone".

Switching brokers switches worlds honestly: the panels of the broker
being left stay in the list with their last values but go grey -- they
are not known to be online anywhere the tool can see -- and their
beacons are dropped, because radio contacts heard through them are not
facts on the new broker either. The new connection's retained messages
paint the new world at once, exactly as they do at startup.

## The page

* **Boxes on a canvas, held by their own network** -- one per panel.
  The layout is not a plan the boxes obey, it is the links: every line
  drawn on the canvas is a spring, the broker is the hub, and the mesh
  finds its own shape. The panels make a ring around the broker, each
  hanging on its line; every beacon is held by a line to each panel
  that hears it, a strong hearing a short leash and a weak one a long
  one -- so the mesh says what the radio says. Boxes that touch ooze
  apart, a slow convection keeps the whole thing breathing, and all of
  it moves in thick oil: nothing overshoots or oscillates, a shoved box
  oozes rather than glides, and a thrown one is gathered back by its
  links. Every message a panel publishes becomes a particle flowing
  along its line.
* **Lines under gravity** -- the links hang below the straight path
  between their ends, the way cables do: more slack the longer the
  line, taut for short hops. The particles ride the hanging line, a
  busy link a bundle of threads rather than one.
* **Present MQTT data, preset into the box** -- status, board and version
  in the header; uptime, heap and FPS as chips; the station address and a
  signal bar; night mode, backlight, activity and brightness as chips
  that light up; the BME280 readings with a temperature sparkline drawn
  from their history; and the relay and LED states on the boards that
  have them. A field glows briefly when its topic changes.
* **The BLE mesh** -- every beacon is a box of its own, a smaller card in
  the teal of the radio, one however many panels hear it. It floats just
  beyond the panels that hear it, tethered to each by a line whose
  strength is the strength of the hearing: -40 dBm is a fat, bright,
  pulsing stroke, -90 dBm a thread that is barely there. The card shows
  the name, kind and address, a hearing bar with the distance estimate,
  a sparkline of how the hearing has been going, and whatever telemetry
  the beacon carried along (battery, temperature, power). A beacon
  nobody has heard for a while is drawn asleep, grey, until it
  advertises again. Click it for every field it advertised and which
  panel hears it how well.
* **Controls that publish back** -- the relay badges answer a click with
  `relay/<n>/set` (a toggle, so a push-button that does not know the
  state works too), the LED sliders publish `led/<name>/set`, and the
  detail panel plays any of the eighteen themed sounds through
  `sound/set`. See [MQTT](mqtt.md) for the payloads.
* **Detail** (click a box) -- every topic the panel publishes with its
  value, retain flag and age, plus the controls and a way to remove a
  device from the list.
* **Config** -- the broker profile list in a dialog of its own: add a
  broker, remove one, edit any of them (name, host, port, credentials,
  base topic), and one save makes it all true. Saving does not switch
  the connection -- the selector in the top bar does that. The passwords
  are masked the way devmgr masks them, and a mask sent back means
  "leave it alone".
* **Console** -- the drawer at the bottom shows what the visualizer is
  doing: broker connects and drops, devices appearing, going offline and
  coming back, beacons appearing and clearing, every published command,
  and -- switched on in the config panel -- every message that arrives.
* **View switches** -- the *Broker* and *Beacons* buttons in the top bar
  say what the canvas draws. They are view switches, not stop buttons:
  the devices keep tracking and the beacons keep learning behind them,
  and everything is back exactly as it was when a switch flips back.
  The choice is kept with the settings, so it survives a restart.
* **Physics** -- the *Physics* button opens a side panel at the right
  edge with the sliders that say how the network moves and the lines
  hang. The panels, the beacons and the broker are set apart: three
  tabs -- *Panels*, *Beacons* and *Broker* -- each with its own
  sliders, because the three kinds of things in the picture deserve
  physics of their own. The two kinds that float each have
  viscosity (drag), the link spring (how hard every line holds the
  length it wants), the link length (the radius of the ring around the
  broker, and how far the beacons sit from their panels), the home
  pull, the convection and its speed, the firmness of the shove, the
  wall spring, the gravity sag of the lines, and a gravity of each
  object's own: every box pulls on every other box -- positive
  attracts, negative repels -- fading with distance, so a fleet with
  gravity on leans together like a clump held on strings, and the
  springs keep it a clump, not a collapse. The broker never moves, so
  it has the one setting that can act from a standstill: its own
  gravity, positive gathering the whole mesh toward the hub, negative
  blowing it outward, and every line answering. The panel never blocks
  the canvas -- the
  fleet keeps moving while the sliders are dragged, and when the device
  detail is open, the physics panel steps to its left. Every slider
  acts on the canvas the moment it is touched -- the feel is the thing
  being edited, and it has to be felt -- the readout prints the
  constant the canvas is really using, and the hover of a row explains
  in plain words what its slider does. One save keeps the feel of all
  three; closing without saving puts it back; *defaults* returns to
  the thick oil the canvas ships with. The sliders are the whole truth:
  nothing hidden blends underneath them.
* **Language** -- the page speaks English and German, chosen in the
  config dialog and kept with the settings, so every browser that opens
  the tool starts in the right one; the browser remembers its own last
  choice too, and the first poll reconciles the two. Everything the page
  says is translated -- the chrome, the canvas, the detail and physics
  panels, the toasts and the help texts. The console's log entries stay
  as the server wrote them.

## The visualizer's own API

The page is a client of a small JSON API, which scripts may use too:

| Route | Purpose |
| ----- | ------- |
| `GET /api/state` | Settings, broker profiles, broker status, devices, beacons |
| `POST /api/settings` | View switches, logging, language (`en`/`de`), the physics of each kind (`phys_nodes`, `phys_beacons`, `phys_broker`), the profile list (`brokers`), and the active profile (`broker_index`) |
| `POST /api/publish` | `{"device": host, "suffix": "relay/1/set", "payload": "TOGGLE"}` |
| `GET /api/log?since=N` | Console entries after cursor N |
| `POST /api/log/clear` | Clear the console |
| `POST /api/device/<host>/delete` | Remove a device from the list |

The `beacons` in `/api/state` carry the address, name, kind and fields
with their ages, the strongest hearing as `rssi`, and a `devices` list
per panel that hears it with that panel's RSSI. Removing a device takes
its beacons' word with it: a beacon nobody reports any more is no
beacon at all.

Publishing is guarded: only the topics the firmware subscribes to
(`relay/<n>/set`, `led/<name>/set`, `sound/set`, `config/<setting>/set`)
can be written, and only to a device that is in the list.

## Rules worth knowing

* **The MQTT client is built in.** A minimal MQTT 3.1.1 implementation
  (`mqttviz/mqtt_client.py`) speaks connect, subscribe, publish and
  keepalive over plain TCP -- which is the whole of what the firmware
  speaks (QoS 0, no TLS). Nothing to install.
* **The device list is written by the broker's retained messages.** A
  fresh start replays every panel's state at once; a panel that never
  came back stays in the list, greyed out, until you remove it.
* **The visualizer never writes settings on its own.** Everything it
  publishes comes from a click on the page.
