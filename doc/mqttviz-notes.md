# mqttviz -- conversation note

Date: 2026-09-29. Short record of the mqttviz work so far, in
simplified technical English. The full feature documentation is in
[mqttviz.md](mqttviz.md).

## What mqttviz is

A small tool that watches the MQTT broker and draws every OhEzTouch
panel as a box on a canvas. One Python script, one web page, no extra
dependencies. It reads what the panels publish and can publish back to
the topics the firmware listens to.

## State before this session

- One picture: the **mesh view**. The broker sits in the middle, the
  panels form a ring around it, every line is a spring.
- Beacons are mobile boxes, leashed to each panel that hears them.
  The leash length follows the RSSI value.
- Physics sliders in three tabs: Panels, Beacons, Broker. The sliders
  are saved in the settings file.
- View switches for the broker node and the beacons.

## Requirements from this session

New physics system for a mesh network topology display:

- **Broker** -- fixed, central node.
- **WLAN access point** -- new object type, keyed by BSSID, fixed. It
  connects to the broker over LAN.
- **Panels** -- fixed, BLE scanners. Each panel connects to one access
  point (BSSID, signal strength from its own topics).
- **BLE beacons** -- mobile.

Goals:

1. Fixed objects distribute in a sensible way. The position must be
   storable.
2. Mobile objects arrange by signal strength and, if present, by the
   MQTT distance topic.
3. Physics simulation for all node movement.
4. The movement must feel organic and natural. Animations and effects
   are wanted. Futuristic look.

## Decisions

- The new topology view comes **in addition** to the existing mesh
  view. A switch in the top bar selects the picture. Nothing moves for
  existing users; the default stays `mesh`.
- **Pinning**: drag a stationary object and let go -- the position is
  pinned and saved at once. A small reticle on the box un-pins it.
  Positions are stored as fractions of the canvas (0..1), so they
  survive a different window size. Beacons are never pinned.
- **Access points**: own box type, derived from the `system/bssid` and
  `system/ssid` topics of the panels. Own physics tab. A panel without
  a BSSID falls back to a direct line to the broker.
- **Beacon placement**: the `ble/<addr>/distance` topic wins, scaled
  by a new slider (px per metre, 10..200, default 60). RSSI mapping
  stays as fallback. With two or more scanners, the spring network
  triangulates the beacon.
- **Effects**: on by default, with an *FX* switch in the top bar to
  turn them off. The physics is the same either way.

## What was built

Server (`mqttviz/mqttviz.py`):

- Beacon registry stores the distance per scanner.
- New settings: `view_mode`, `show_fx`, `positions`, `phys_aps`, and
  the `metre_px` slider.
- New routes: `POST /api/position` and `POST /api/position/delete`.

Client (`mqttviz/web/`):

- New object types: access point boxes and a real broker box.
- Topology physics: LAN spring broker to AP, WLAN spring AP to panel
  (rest length follows the WLAN signal), distance leash for beacons,
  pinned boxes as fixed walls in the overlap pass.
- Mode switch is a morph: the springs move the boxes into the other
  layout.
- Effects layer: background dot grid and vignette, radar sweep,
  pulsing broker core with rotating ring, breathing halos, birth and
  death animations, growing links with dash flow, beacon trails,
  particles with additive tails, arrival flashes, hover lift.
- AP detail panel, pin button in the detail panel, new strings in
  English and German.

## Verification

- Syntax checks pass (`py_compile`, `node --check`).
- API tests with curl: pin, un-pin, clamp, persistence over restart,
  view mode, FX switch, `phys_aps` sliders.
- Headless browser layout check: mesh ring intact, topology settles
  without overlaps, pinned panel sits at the exact stored position,
  beacon converges to the distance leash (about 90 px for 1.5 m at
  60 px/m, not the 248 px of the RSSI fallback).

## Open points

- Screenshots in the docs are not updated yet (see todo list).
- No unit test harness for mqttviz so far; the checks were manual.

## Follow-up: layouts (same day)

Request: save and restore multiple layouts, a backup file, and the
current arrangement must never get lost.

Decisions:

- A layout is a whole arrangement: the pinned places **and** the
  picture they belong to (mesh or topology). Restoring one moves the
   canvas to that picture, the way a view switch does.
- Saved under names in the *Layouts* dialog (new button in the top
  bar). Same name twice: the newer arrangement wins.
- The server keeps a backup file beside the state,
  `mqttviz/data/layouts.json`, rewritten on every change: the live
  arrangement, all saved layouts, and what stood right before the last
  restore. A restore that goes wrong costs nothing.
- The dialog writes the same content to a file of the user's own
  (download), and reads such files back in (import). Import merges:
  names in the file win over the same names here, nothing else is
  touched.
- Before any of it started, the live state file was copied by hand to
  `mqttviz/data/backups/` -- the arrangement from before the feature
  exists on disk twice.

New API: `POST /api/layout/save|load|delete|import`.

Verified by hand: pin, save, re-pin, restore (positions exactly back,
`before_load` in the backup file), delete, import (merge, garbage
entries dropped, not fatal), persistence over restart, real data
directory untouched (hashes equal). The live instance on port 8089
keeps running the old code until its next restart -- nothing changes
for it, and the new settings key is simply absent until then.

## Follow-up: zoom (same day)

Request: a zoom for the topology view; the workspace should offer
four times the area.

Decisions:

- The **stage is the workspace**: twice the canvas each way, four
  times the area, with the classic canvas picture in its middle.
  Physics and drawing speak workspace pixels; nothing in the physics
  had to change.
- A **camera** (zoom 0.5 to 2.5, pan) shows the workspace through the
  viewport. Home is zoom 1, centred -- the switch to topology changes
  nothing the eye can see. At 0.5 the whole workspace is visible at
  once.
- Input: wheel zooms (anchored at the cursor, trackpad pinch works),
  dragging the empty space pans, three buttons in the canvas corner
  (step in, step out, glide home). The mesh view keeps the camera
  home always -- it is its own fixed picture.
- The camera eases like the boxes do (exponential toward the target),
  and is kept with the settings as fractions (`cam`), saved when it
  settles, adopted once per session.
- **Migration**: positions were fractions of the canvas once; a file
  from before the workspace is converted once at load time into
  fractions of the workspace (`new = 0.25 + 0.5 * old`, both axes,
  positions and every layout's positions), guarded by a `pos_space`
  marker in the settings. Every pin lands exactly where the eye left
  it -- verified: a legacy pin at canvas fraction 0.3/0.3 sits at the
  same screen pixel at home.

Verified: migration formula and idempotence over restart, cam
clamping (zoom 9 -> 2.5, garbage -> home), headless geometry (broker
at the legacy screen position, workspace exactly 2x, camera adopted
from settings, mesh forces home, zoom buttons only in topology). The
same live-instance note applies: the migration runs on its data at
the next restart, once, and the eye sees no difference.

