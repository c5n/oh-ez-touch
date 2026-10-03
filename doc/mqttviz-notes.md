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
- No unit test harness for the server and the page; the checks were
  manual. The SweetHome3D converter has one (`mqttviz/test_sh3d.py`).

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

## Follow-up: the space view (2026-10-02)

Request: the topology view in 3D, the static objects placeable in the
space, and the walls of a SweetHome3D house visible to place them on,
half transparent -- a futuristic laser projection.

Decisions:

- A **third view mode**, `space`, next to mesh and topology. Topology
  keeps its 2D pins; the space view has its own, in metres
  (`positions3d`, same keys), and layouts carry both.
- **three.js, vendored** as one minified ES-module bundle (0.186.1 with
  OrbitControls, CSS2DRenderer and the bloom chain), built by
  `web/vendor/build.sh` with esbuild. One file to serve from a fixed
  whitelist, no import map, nothing fetched from the internet: the page
  keeps working on a LAN, and the machine running mqttviz still needs
  nothing but Python 3.
- **The house is converted on upload**, server-side, with the standard
  library: a `.sh3d` is a zip whose `Home.xml` (SweetHome3D 5.3+) holds
  the walls, rooms, levels and doors/windows in centimetres.
  `mqttviz/sh3d.py` reads only those, converts to metres (plan x -> x,
  plan y -> z, y up), cuts round walls (`arcExtent`) into 10-degree
  pieces that are capped only at the arc's ends, and caps the counts so
  a strange file costs a refusal, not the server. The serialized Java
  `Home` entry of older files is not read; they are refused with the
  cure in the message.
- **The look**: walls as one merged prism geometry per level with a
  small shader -- additive, no depth write, a fresnel rim, scanlines
  and a rising scan band on the upright faces only, a slight flicker --
  plus bright edge lines, faint room floors with outlines and names,
  orange frames for doors and windows, a projector ring and grid under
  the house. Bloom when FX is on. The page's dark is the *scene's*
  background, not the renderer's clear colour: the composer's render
  targets take a clear colour unconverted, and it came out grey.
- **Placement is plan-based, not a raycast onto the walls.** The first
  version raycast onto the wall meshes; with the camera outside the
  house every drag landed on the nearest outer wall -- the walls look
  like light but stopped the pointer like matter. Now the pointer
  moves over the invisible floor plane of the level in view, and within
  35 cm of a wall's face the object sticks to that wall on the
  pointer's side at the height it carries (per-kind defaults, Shift to
  change). The dock gives every object a fixed slot, so taking one in
  never shuffles the others under the hand.
- **Beacons in metres**: a few steps of gradient descent on the
  distance residuals to the placed panels that hear them, warm-started
  from the last solution; one hearer only -> an orbit at that
  distance. The `line_timeout` of the beacon physics still decides
  which hearings count.
- `app.js` stays a classic script and the page; it hands the module
  what it needs as `window.mqttvizHost` and calls
  `MqttvizSpace.update()` after every poll and view switch. The 2D
  draw loop idles while the space view is up; the boxes still follow
  the facts (the access point boxes are kept in the space view too,
  because the detail panel reads them).

Verified: `test_sh3d.py` (levels, units, axes, slopes, round walls and
their caps, the refusals); the routes by curl (import, junk refused,
`/vendor/../` not served, NaN/inf positions refused, a layout saving,
losing and restoring a 3D place, the backup file's `before_load`);
headless Chromium driven over CDP with `#debug` (which also writes the
space view's objects and their screen positions into the page): every
panel dragged in from the dock landed on the intended wall at 1.4 m or
on the intended floor, places survived a reload, a tap opened the
detail panel with *take out of the house*, objects and lines of a
hidden level disappeared with it, beacons triangulated between the
panels; mesh and topology still draw as before.

## Follow-up: the pictures (same day)

Request: update the docs. The open item was the guide's pictures --
it had none.

- `mqttviz/demo/` makes them reproducible, the way
  `doc/openHAB/make-screenshots.sh` does for the panel: `demo.py` runs
  the real mqttviz with `--no-mqtt` and feeds a made-up fleet (four
  panels, two access points, two beacons) into `note_message`, the
  same entry the broker thread uses; `demo_house.py` writes a
  two-storey `Home.xml`; `screenshots.mjs` drives a headless Chromium
  over the DevTools protocol with Node 22's built-in fetch and
  WebSocket -- no npm packages -- and arranges the page through the
  API (pins, layout, house import, places) before each picture.
- The pictures found three bugs, fixed with them: the panel box's UI
  chips were labelled "undefined" -- a local `t` for the topic lookup
  in `drawBox` shadowed the translator; a beacon's lines still reached
  a panel on a hidden level; and with no house loaded the camera
  framed the house's bounds but not the dock in front of it. The
  layouts list now says how many places a layout keeps in the house,
  and the wall slider is hidden while there is no house to dim.

## Follow-up: lengths, beacons that trust the near, the view cube

Requests, over a few days of trying it against the real house: drop the
very old entries from the panels' topic lists, a length in metres on
the 3D lines, the beacon that sat outside the workshop it was in, a
view cube for the camera, and a GUI size for the whole chrome.

- **The picture holds the recent.** `/api/state` now leaves out a
  topic nobody has heard from for an hour and a panel silent for a
  day. The registry keeps everything -- the filter is in the answer,
  not in the store -- so both are back the moment they speak again.
- **Line lengths.** Every link -- panel to access point, access point
  to broker, beacon to panel -- carries its length in metres at its
  middle, in the space view. The label is a CSS2D span inside the
  positioned div: the renderer owns the div's transform, the span gets
  the rotation, measured on the screen where the line is seen and
  flipped upright past 90°.
- **Beacons that trust the near.** The real house showed what the
  demo's clean readings could not: panels on other floors report
  distance estimates of 25 to 35 m for panels five metres away, and an
  unweighted least-squares fit of those puts a workshop beacon thirty
  metres outside the house. The descent also never converged -- forty
  fixed steps stopped halfway. Now every hearing counts with the
  weight of 1/d² -- a strong "2 m" is a claim, a weak "28 m through
  two floors" is a guess -- and the best point is found on a coarse
  sweep over the house's plan first, because two readings' circles
  cross twice and the descent alone finds whichever crossing it starts
  near. The sweep's best cell is polished by the old descent, run only
  when the hearings change, throttled to half a second.
- **A level id cut short.** The beacon, moved upstairs, still sat in
  the basement: the server stored every place's level id cut to forty
  characters (`[:40]`), and the house's ids are forty-two (`level-` +
  a UUID); `levelById` then fell back to `houseLevels[0]`, which is the
  basement, for every placed thing. The cap is sixty-four now, and the
  page resolves an old cut id by its unique prefix, so the places
  already kept still name their levels.
- **The view cube.** A chamfered cube in the space view's lower left
  corner -- six labelled faces for the views straight along an axis,
  twelve edge bevels for the 45° diagonals, eight corner bevels for
  the isometric ones -- drawn after the main pass into its own
  scissored square with the depth cleared, so it floats over the house
  without the bloom. It turns with the camera (the camera's quaternion
  inverted); a tap flies there along the shortest arc, slerped around
  the orbit target with the distance kept; a drag falls through to the
  orbit, so the cube is the orbit's hand too, told apart from the tap
  by the same five pixels a drag is. The poles keep a breath of the
  azimuth they came from, and `maxPolarAngle` had to give up its
  0.495π -- the bottom face is a view from below, and the clamp would
  have snapped it right back.
- **The GUI size.** One setting, 70 to 160 per cent: `--gui-scale` on
  the root, `zoom` on the chrome (the top bar, the panels, the
  dialogs, the space toolbar), `calc()` on the cube's buttons and the
  space view's labels -- which must not be zoomed, their positions are
  the projection's -- and the cube itself, drawn in the canvas, sized
  in page pixels to match. The corner's stack (help text, buttons,
  cube) is measured, not guessed, so it holds at any scale.
- The mesh view's boxes, meanwhile, are found through index maps now
  instead of `boxes.find` over the whole list, and its sparks, sweeps
  and waves got small fixes on the way.

Verified: the geometry and the picking against the vendored three
(26 meshes, every face, edge and corner picking its direction, the
slerp midpoint and the 180° case exact); the beacon's place against
the live `/api/state` of the real house -- outside at (-5.3, -6.9)
before, in the workshop at (1.8, 0.8) after, its floor following the
strongest hearing once the cut level ids resolved.
