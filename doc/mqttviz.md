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
| `--mqtt-host`   | setting | The broker to watch (default `localhost`)          |
| `--mqtt-port`   | setting | The broker's port (default 1883)                  |
| `--user`        | setting | The broker username, if any                       |
| `--password`    | setting | The broker password, if any                       |
| `--base`        | setting | The base topic to watch (default `oheztouch`)      |
| `--no-mqtt`     | off     | Start without connecting to the broker             |

Command line arguments are written into the settings, so what was typed
once is what the next start remembers.

## What it knows, and where it keeps it

Everything the visualizer persists lives in `mqttviz/data/mqttviz.json`
(kept out of the repository, because it contains your broker password):

* **Settings** -- the broker (host, port, username, password), the base
  topic to watch, whether the connection is on at all, and how verbosely
  incoming messages are logged.
* **Devices** -- every panel that has ever published under the base
  topic, keyed by its hostname, with the newest value of every topic it
  publishes. A panel whose last will arrived is drawn greyed-out with its
  last-seen time, and stays in the list until you remove it.

## The page

* **Boxes on a canvas** -- one per panel, floating around the broker node
  with soft spring physics: they drift a little while idle, repel one
  another, and can be picked up and thrown. Every message a panel
  publishes becomes a particle flowing from the broker to its box.
* **Present MQTT data, preset into the box** -- status, board and version
  in the header; uptime, heap and FPS as chips; the station address and a
  signal bar; night mode, backlight, activity and brightness as chips
  that light up; the BME280 readings with a temperature sparkline drawn
  from their history; and the relay and LED states on the boards that
  have them. A field glows briefly when its topic changes.
* **Controls that publish back** -- the relay badges answer a click with
  `relay/<n>/set` (a toggle, so a push-button that does not know the
  state works too), the LED sliders publish `led/<name>/set`, and the
  detail panel plays any of the eighteen themed sounds through
  `sound/set`. See [MQTT](mqtt.md) for the payloads.
* **Detail** (click a box) -- every topic the panel publishes with its
  value, retain flag and age, plus the controls and a way to remove a
  device from the list.
* **Config** -- the broker settings in a panel of their own: saving
  reconnects with the new host, port, credentials or base topic. The
  password is masked the way devmgr masks it, and sending the mask back
  means "leave it alone".
* **Console** -- the drawer at the bottom shows what the visualizer is
  doing: broker connects and drops, devices appearing, going offline and
  coming back, every published command, and -- switched on in the config
  panel -- every message that arrives.

## The visualizer's own API

The page is a client of a small JSON API, which scripts may use too:

| Route | Purpose |
| ----- | ------- |
| `GET /api/state` | Settings, broker status, devices with every topic |
| `POST /api/settings` | Change broker / base topic / logging (persisted) |
| `POST /api/publish` | `{"device": host, "suffix": "relay/1/set", "payload": "TOGGLE"}` |
| `GET /api/log?since=N` | Console entries after cursor N |
| `POST /api/log/clear` | Clear the console |
| `POST /api/device/<host>/delete` | Remove a device from the list |

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
