# The MQTT visualizer (mqttviz/)

A canvas view of every OhEzTouch panel on the broker: one Python script,
one page on localhost, nothing but Python 3 installed -- the same rules
the [device manager](devmgr.md) plays by, applied to the other half of
the panel's remote interface. Where devmgr talks to the REST API over HTTP,
mqttviz subscribes to the broker and watches what the panels publish.
The record of how the topology view came to be is in
[mqttviz-notes.md](mqttviz-notes.md).

    python3 mqttviz/mqttviz.py            # then open http://localhost:8089

Each panel found under `<base topic>/<hostname>/` becomes one box on a
canvas: a live picture of its MQTT data, and -- where the panel has
relays, LEDs or sounds -- a remote control for them. The canvas draws
two pictures of the same facts, the classic mesh and the topology of
broker, access points and panels; the switch in the top bar says which.

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
(kept out of the repository, because it contains your broker passwords),
and the arrangement of the canvas has a file of its own beside it,
`mqttviz/data/layouts.json` -- a human-readable backup, rewritten on
every change:

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
* **Positions** -- the places the topology view has been told to keep
  (see below), keyed by `broker`, `ap:<bssid>` or `panel:<hostname>`, as
  fractions of the workspace so they survive a different window size.
* **Layouts** -- whole arrangements kept under a name: the places and
  the picture they belong to, to be brought back whole (see below).
* **The layout backup file** -- `mqttviz/data/layouts.json` holds the
  live arrangement as it stands, every layout the page has saved, and
  what the canvas looked like right before the last layout was
  restored, so trying a layout on never costs the arrangement that was
  there. Whatever a later write corrupts in the state file, the places
  found by hand are still in this one, readable, and the page's
  *Layouts* dialog writes the same content to a file of your own --
  which can be read back in, adding, never wiping.

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
  along its line. (This is the mesh picture's way of laying out; the
  topology view lays out by the network's own hierarchy, further down.)
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
  advertises again. A line whose panel has not reported the beacon for
  the line timeout (a beacon physics slider, 30 to 300 s, 90 s as
  shipped) is gone, and so is a line whose signal is weaker than the
  minimum signal (another one, -100 to -30 dBm, everything as shipped);
  a beacon left with no lines at all leaves the canvas -- the mesh says
  what the radio says, and a radio nobody reports is no radio. Click it
  for every field it advertised and which panel hears it how well.
* **Two pictures of the same facts** -- the *Mesh*/*Topology* switch in
  the top bar. The mesh is the classic view: every panel on its line to
  the broker, the beacons on their leashes. The topology view is the
  network as it is: one access point box per BSSID the panels report
  (drawn from their `system/bssid` and `system/ssid` topics), the
  access points on LAN lines to the broker, the panels on WLAN lines to
  their access point -- the line thicker and brighter the stronger the
  panel's `system/rssi` -- and the beacons placed by their distance
  estimates, metres scaled to pixels. A panel that reports no access
  point hangs on the broker directly. The switch is kept with the
  settings, and switching is a morph: the boxes keep their places and
  the springs move them to the other picture's layout.
 * **Places, kept** -- the stationary objects of the topology view (the
   broker, the access points, the panels) can be pinned: drag one where
   it belongs and let go, and the place is kept -- saved as a fraction of
   the workspace with the settings, so it survives a restart and a resize. A
   pinned box carries a small reticle; click it to let the box float on
   its links again (the detail panel has the same button). Beacons are
   never pinned -- they are mobile, and the radio says where they sit.
 * **The workspace and the zoom** -- the topology view lays out in a
   workspace of four times the canvas's area, twice each way, with the
   canvas's picture in its middle: the switch to it changes nothing the
   eye can see. The *wheel* zooms (the point under the cursor stays
   under the cursor; a trackpad pinch works too), *dragging the empty
   space* moves the workspace, and the buttons in the canvas's corner
   answer: a step in, a step out, and *home* -- a glide back to the
   classic picture at 100%. At half zoom the whole workspace is on the
   screen at once; the camera is kept with the settings, so the page
   comes up looking where it was left. The mesh view is its own fixed
   picture: zoomed to home always, nothing to move.
 * **Arrangements, named** -- the *Layouts* button in the top bar opens a
   dialog that keeps whole arrangements: *save current* writes the
   places as they stand, and the picture they belong to, under a name
   (the same name twice is the newer arrangement winning); *restore*
   brings one back whole -- the springs move the boxes to the places it
   kept, the way a view switch does -- and *delete* forgets one. The
   dialog writes the same content to a file of your own and reads such
   files back in: names the file carries win over the same names here,
   names only here live on -- an import adds, it never wipes. Restoring
   remembers what stood before: the layout backup file
   (`mqttviz/data/layouts.json`) holds it, so trying a layout on costs
   nothing that cannot be taken back.
 * **The light** -- the *FX* switch in the top bar turns the effects
  layer on and off: the drifting dot grid and vignette, the radar sweep
  around the hub, the pulsing core and rotating ring of the broker, the
  breathing halos under every box, links that grow when they are born
  and leave ghosts when they die, WLAN and LAN lines with a current of
  dashes walking toward the broker, beacon trails, particles with longer
  additive tails and a flash where they arrive, boxes that lift under
  the cursor and overshoot into place when they are born. The physics is
  the same either way; off is for the machine that has to, on is the
  shipped default.
* **Controls that publish back** -- the relay badges answer a click with
  `relay/<n>/set` (a toggle, so a push-button that does not know the
  state works too), the LED sliders publish `led/<name>/set`, and the
  detail panel plays any of the eighteen themed sounds through
  `sound/set`. See [MQTT](mqtt.md) for the payloads.
* **Detail** (click a box) -- every topic the panel publishes with its
  value, retain flag and age, plus the controls and a way to remove a
  device from the list. An access point answers the click with its own
  panel: which panels are on it and how well it hears each of them --
  an access point is listened to, not talked to. A panel's or an access
  point's detail carries the pin button of the topology view, saying
  what the box is and doing the other thing.
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
* **View switches** -- the *Broker*, *Beacons* and *FX* buttons in the
  top bar say what the canvas draws and how lit it is. They are view
  switches, not stop buttons:
  the devices keep tracking and the beacons keep learning behind them,
  and everything is back exactly as it was when a switch flips back.
  The choice is kept with the settings, so it survives a restart.
* **Physics** -- the *Physics* button opens a side panel at the right
  edge with the sliders that say how the network moves and the lines
  hang. The panels, the access points, the beacons and the broker are
  set apart: four tabs -- *Panels*, *Access Points*, *Beacons* and
  *Broker* -- each with its own sliders, because the kinds of things in
  the picture deserve physics of their own. (The *Access Points* tab is
  offered in the topology view, which is the one that has them.) The
  kinds that float each have
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
  blowing it outward, and every line answering. The beacons add four
  sliders of their own: the signal pull, how much a line's hearing
  shortens its leash (at zero every line wants the same length, at full
  the radio alone places the beacon), the minimum signal, the weakest
  hearing a line may carry before it is gone (-100 dBm, the default,
  keeps every line), the line timeout, how long a panel's last
  hearing keeps its line alive before the line -- and with the last of
  them the beacon -- is gone, and the metre scale, how many pixels a
  reported metre is worth in the topology view (10 to 200, 60 as
  shipped) -- a beacon's `distance` estimate becomes a leash of metres
  scaled to this, and a beacon that reports no distance keeps
  following its signal strength. The panel never blocks
  the canvas -- the
  fleet keeps moving while the sliders are dragged, and when the device
  detail is open, the physics panel steps to its left. Every slider
  acts on the canvas the moment it is touched -- the feel is the thing
  being edited, and it has to be felt -- the readout prints the
  constant the canvas is really using, and the hover of a row explains
  in plain words what its slider does. One save keeps the feel of all
  four; closing without saving puts it back; *defaults* returns to
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
| `POST /api/settings` | View switches, logging, language (`en`/`de`), the view mode (`view_mode`, `mesh`/`topology`), the topology view's camera (`cam`, `{zoom: 0.5..2.5, x/y: 0..1}` as fractions of the workspace), the physics of each kind (`phys_nodes`, `phys_aps`, `phys_beacons`, `phys_broker`), the profile list (`brokers`), and the active profile (`broker_index`) |
| `POST /api/position` | `{"key": "panel:<host>", "x": 0..1, "y": 0..1}` -- pin one object of the topology view (`broker`, `ap:<bssid>`, `panel:<host>`) |
| `POST /api/position/delete` | `{"key": ...}` -- unpin it again |
| `POST /api/layout/save` | `{"name": ...}` -- keep the arrangement as it stands under a name |
| `POST /api/layout/load` | `{"name": ...}` -- bring a saved arrangement back whole (places and picture); what stood before is remembered in the layout backup file |
| `POST /api/layout/delete` | `{"name": ...}` -- forget one saved arrangement |
| `POST /api/layout/import` | `{"layouts": {...}}` -- read layouts in from a file, adding, never wiping |
| `POST /api/publish` | `{"device": host, "suffix": "relay/1/set", "payload": "TOGGLE"}` |
| `GET /api/log?since=N` | Console entries after cursor N |
| `POST /api/log/clear` | Clear the console |
| `POST /api/device/<host>/delete` | Remove a device from the list |

The `beacons` in `/api/state` carry the address, name, kind and fields
with their ages, the strongest hearing as `rssi`, and a `devices` list
per panel that hears it with that panel's RSSI and, where the beacon
reports one, its distance estimate in metres. Removing a device takes
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
  publishes comes from a click on the page -- and the one setting the
  page writes without a save button is the pin: dropping a stationary
  box in the topology view keeps the place it was given, which is the
  button.
