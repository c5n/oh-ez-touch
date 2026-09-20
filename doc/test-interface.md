# The simulator's control interface

The simulator can be driven from a script: touches go in over a UDP socket,
and what is on screen comes back as JSON or as the panel's framebuffer. This
is what makes "did that change break the settings screen?" a question a shell
script -- or a coding agent in a later session -- can answer without a person
looking at a window.

It exists because the simulator could only ever be *set up* from outside.
`OHEZ_ITEM` walks to a control, `OHEZ_SETTINGS` opens a tab and `OHEZ_THEME`
picks a look, all at boot, and after that there was nothing: no way to press a
button, and no way to find out what happened. Those variables still work and
are still the quickest way to start somewhere particular; this is the other
half.

**Simulator only.** A panel has no control socket, and the code is compiled out
of its firmware entirely -- see [On the device](#on-the-device) at the end for
what putting it there would take.

## Starting it

Build and run the simulator as usual; the interface comes up with it.

```bash
. ~/esp/esp-idf/export.sh
idf.py -B build/linux build
OHEZ_OFFLINE=1 ./build/linux/oh-ez-touch.elf &
tools/ohez_ctl.py ping
```

`OHEZ_OFFLINE=1` is not required but is almost always wanted: it serves the
sitemap and the icons from the compiled-in fixtures, so the screen is the same
every run and does not depend on an openHAB server being reachable.

It listens on **127.0.0.1:8781** -- the web interface's 8780 plus one. Loopback
only, deliberately: the channel has no authentication and can press anything on
the panel, so it is not offered to the network.

| variable | effect |
| --- | --- |
| `OHEZ_TESTIF=0` | do not open the socket at all |
| `OHEZ_TESTIF_PORT` | listen somewhere else |

A port already in use costs the interface and not the run: the simulator logs
`bind to 127.0.0.1:8781 ... -- no control interface` and carries on. In practice
ports do not collide, because `port_flash.c` takes an `flock` on the emulated
NVS image and a second simulator refuses to start before it gets that far.

## The command line

`tools/ohez_ctl.py` is the front end. It needs nothing but Python 3 -- no
Pillow, no requests -- and it exits non-zero when a command is refused or the
simulator does not answer, so it reads in a shell script the way
`oh-ez-touch-host-test.elf` does.

```bash
tools/ohez_ctl.py ping
tools/ohez_ctl.py wait-page                  # until the openHAB page has loaded
tools/ohez_ctl.py screen                     # pretty-printed JSON
tools/ohez_ctl.py status
tools/ohez_ctl.py tap 160 120
tools/ohez_ctl.py tap-tile 4
tools/ohez_ctl.py tap-label "Hallway Dimmer"
tools/ohez_ctl.py longpress 160 120
tools/ohez_ctl.py swipe right                # a drag; the panel ignores it
tools/ohez_ctl.py shot /tmp/panel.png --scale 2
tools/ohez_ctl.py set theme lcars
tools/ohez_ctl.py quit
```

`--host`, `--port` and `--web-port` move it. `send` passes a command line
through untouched, so a command added to the firmware is usable without editing
the script:

```bash
tools/ohez_ctl.py send swipe 300 120 20 120 200
```

`tap-label` and `tap-tile` are the ones worth reaching for. They read the
`screen` dump, find the tile's rectangle and tap its centre, so a test says
what it means instead of naming pixels that the next layout change moves.

## The protocol

One request datagram, one reply datagram, plain text, nothing kept between
them. `nc -u 127.0.0.1 8781` is a perfectly good client.

```
[@<id> ]<command> [args...]     request, at most 512 bytes; the newline is optional
[@<id> ]ok [payload]            it worked
[@<id> ]err <reason>            it did not
```

The `@<id>` token is optional and is echoed back on every answer, refusals
included. It is what lets a client retry safely: the reply to a request that
timed out can be recognised and dropped rather than read as the answer to the
next one. `ohez_ctl.py` always sends one.

An argument containing spaces can be double-quoted (`set hostname "two
words"`). There is no escape syntax.

### Touch

Coordinates are **panel pixels: 0..319 by 0..239, origin top left.** They are
not window pixels -- the SDL window is shown at double size -- so something
that looks like it is at (320, 240) on screen is at (160, 120) here. Every
rectangle the `screen` dump reports is in the same space, so a tile from the
dump can be tapped directly. An out-of-range coordinate is refused with
`err range` rather than clamped, because a clamped tap lands on something real
and quietly tests the wrong widget.

| command | what it does |
| --- | --- |
| `tap <x> <y> [hold_ms]` | press, hold (60 ms), release |
| `longpress <x> <y> [ms]` | the same, held 500 ms -- past LVGL's long-press threshold |
| `swipe <dir> [<x> <y>]` | `left`, `right`, `up` or `down`, across the panel |
| `swipe <x1> <y1> <x2> <y2> [ms]` | an explicit drag, over 200 ms by default |
| `press <x> <y>` / `move <x> <y>` / `release` | the primitives, for a drag nothing above describes |

**The panel has no swipe gestures.** `swipe` still sends a real drag, and a
drag on a scrollable widget still scrolls it -- `swipe up` scrolls a settings
list, and dragging a slider still moves it -- but a swipe never navigates, and
it never activates whatever is under it. An item screen is dismissed by its
back bar (`tap 20 20`) or by `settings`/`nav`, not by a gesture.

That was not always so, and the history is worth knowing because a test written
against the old behaviour still passes for the wrong reason. There was never a
gesture handler on a tile page: a swipe "went back" only in the Material theme,
where the back tile sits where a right-swipe begins, and LVGL delivered the
click to it. In LCARS the same swipe opened the settings screen and in JARVIS
it did nothing -- and a swipe across the top row of a page turned a light on.
`main/ui/ui_input.c` is where that was closed off.

`press` leaves the pointer down until something lifts it. `move` carries
whatever state it is in, so it drags after a `press` and merely travels without
one. `tap`, `longpress` and `swipe` each lift the pointer first if it is still
down, because LVGL starts a click on the released-to-pressed edge and a second
press with no release in between would do nothing at all.

### Reading

| command | reply |
| --- | --- |
| `ping` | `ok <uptime_ms>` |
| `screen` | `ok {json}` -- see below |
| `status` | `ok {json}` -- version, uptime, heap, frame time, network, openHAB, MQTT |
| `config [<name>]` | `ok {json}` -- one setting, or all of them |
| `shot` | `ok <url>` -- where the framebuffer is |

### Writing

| command | what it does |
| --- | --- |
| `set <name> <value>` | write a setting, save it, and apply what does not need a reboot |
| `nav <path>` | walk a dot-separated path of tile indices, as `OHEZ_ITEM` does |
| `settings [<page>]` | open the settings screen on a page, or close it |
| `quit` | answer, then exit(0) |

`set` takes the same names the web form posts (`config` with no argument lists
them all) and goes through the same save path, so **it writes a real
`config.json`**. Point a test at one of its own with `OHEZ_CONFIG_DIR`, the way
`test_config_file.cpp` does. An unknown value for an enumerated setting is
refused rather than falling back to the first option -- a misspelt theme name
would otherwise silently select another one.

`nav` takes the `OHEZ_ITEM` syntax: every step but the last follows that tile's
linked page, and the last opens that tile's control. So `nav 5` opens the sixth
tile's control and `nav 0.4` follows the first tile and then opens the fifth
tile of the page behind it. It answers as soon as the walk has *started* -- each
step waits for the page the one before it asked for -- so follow it with
`wait-page`.

`settings` takes the names `OHEZ_SETTINGS` takes: a section (`wlan`, `openhab`,
`mqtt`, `sensors`, `device`, `time`, `theme`, `audio`, `info`) or a menu
(`settings`, also spelled `index`, and `system`). With no argument it closes the
screen, so a script can bracket a visit.

## What `screen` returns

```json
{
  "screen": "item",
  "page": {
    "title": "OhEzTouch Demo",
    "state": "ready",
    "generation": 2,
    "tiles": [
      { "i": 0, "label": "Living Room", "state": "", "type": "group",
        "x": 8, "y": 38, "w": 96, "h": 93 },
      { "i": 5, "label": "Hallway Dimmer", "state": "40.000000", "type": "slider",
        "x": 216, "y": 139, "w": 96, "h": 93 }
    ]
  },
  "item": { "open": true, "type": "slider", "slot": 5 },
  "settings": { "open": false },
  "theme": { "family": "Material", "night": false },
  "backlight": { "brightness": 100, "dimmed": false }
}
```

`screen` is what is on top -- `page`, `item` or `settings`. The `page` object
describes the openHAB tile page underneath either of the other two, so it is
always there.

`banner` appears only when one is up, and carries whichever of the two the
firmware has raised -- the WLAN and setup messages `main.cpp` owns, or
`openhab_ui.cpp`'s "this sitemap will not load". It is an `lv_msgbox`: `topic`
is its header title and `text` its content, which is why they are two fields
and not one `"topic\ntext"` string.

```json
"banner": { "kind": "error", "topic": "SITEMAP ACCESS FAILED",
            "text": "http://...", "hidden": false, "restart": true }
```

`hidden` is true once the box's fold button has been pressed. It is still up,
and still reported, because the only way back to it is the notice glyph the
frame shows while a banner exists -- a bell, a warning triangle or a cross, in
the top right of the status row on every family but LCARS, which lights the
bottom cell of its spine instead. Tapping that glyph unfolds every folded box.

`restart` is true when the box carries a **Restart** button in its footer,
which the two faults the panel cannot recover from by itself do: "WLAN NOT
CONNECTED" and "SITEMAP ACCESS FAILED". There is no confirmation behind it --
the box is the prompt -- so **do not tap it from a script** unless restarting
the target is the point. On the simulator it ends the process.

**`page.state` is the field to wait on.** It is the fetch cycle the tiles come
out of -- `idle`, `request`, `waiting`, `ready` -- and acting on a page before
it says `ready` is the classic way to write a test that passes on a fast
machine and fails on a slow one. `tools/ohez_ctl.py wait-page` is that loop.

`tiles[].type` is the openHAB item type: `group`, `link`, `parent_link`,
`number`, `string`, `setpoint`, `slider`, `selection`, `colorpicker`, `switch`,
`rollershutter` or `player`. A `switch` toggles in place when tapped; most of
the rest open a screen; the link types navigate.

`config` reports a `SETTINGS_F_SECRET` field -- the MQTT password -- as `***`,
for the same reason the MQTT client will not publish it.

## What `status` returns

Most of it names itself. The one object worth a paragraph is `frame`, which is
the only place the panel's rendering speed is reported at all:

```json
"frame": { "valid": true, "age_ms": 240, "window_ms": 3652, "frames": 64,
           "fps": 17.5, "frame_us": 1224, "worst_us": 2665,
           "render_us": 1119, "wait_us": 1,
           "pixels": 20277, "flushes": 2.1 }
```

**`render_us` and `wait_us` are the pair to assert on**, not `fps`. They split
a frame into the time LVGL's software renderer spent drawing it and the time it
spent waiting for the panel to accept the previous strip, which is what decides
whether a change made the panel faster or only moved the cost. On the simulator
`wait_us` is always about zero -- SDL's flush returns when it is done, so there
is nothing to wait for -- and on a device it is the SPI transfer. The
[architecture document](architecture.md#where-the-frame-time-goes) is what to
read them against.

The numbers describe one closed **window**, not the instant they are read.
A window closes after 64 frames or 10 seconds, whichever comes first, so a busy
screen reports a fresh second of drawing and a still one keeps its last real
measurement rather than decaying to zero. `age_ms` says how long ago that
window closed and `window_ms` how long it covered.

**`valid` is false until the first window closes**, and everything else is zero
then. A script that reads `fps` without checking it will assert against a panel
that has not drawn yet. The reliable way to make a window close is to cause
drawing and then wait: `set theme <name>` repaints the whole screen and is the
bluntest instrument available.

## Screenshots

```bash
tools/ohez_ctl.py shot /tmp/panel.png            # 320x240
tools/ohez_ctl.py shot /tmp/panel.png --scale 2  # 640x480, as the window shows it
tools/ohez_ctl.py shot /tmp/panel.fb --raw       # the dump, unconverted
```

The panel sends **raw pixels and never a PNG**. Encoding one needs an output
buffer and a compressor's working set, and the hardware this firmware really
runs on has no RAM to spare for either -- so the conversion happens on the
development machine, in about thirty lines of `zlib` inside `ohez_ctl.py`. That
is also what keeps the device half cheap; see below.

The bytes come off the web server rather than the control socket, because 150
KB does not belong in a datagram:

```bash
curl -o /tmp/panel.fb http://127.0.0.1:8780/screenshot.raw   # 153616 bytes
```

The body is a 16-byte header and then the framebuffer, all little-endian:

| offset | field | value today |
| --- | --- | --- |
| 0 | magic | `OHFB` |
| 4 | `u16` version | 1 |
| 6 | `u16` format | 1 = RGB565 |
| 8 | `u16` width | 320 |
| 10 | `u16` height | 240 |
| 12 | `u32` stride | 640, bytes per row |
| 16 | pixels | stride x height |

What comes out is LVGL's own frame buffer, not a re-render of the widget tree:
it includes the banners on the top layer, and whatever a transition is halfway
through. The capture stops the UI repainting for the few milliseconds it takes
to read, so the frame cannot tear; the hold releases itself after half a second
if a client stops reading.

## Gotchas

These are the ones that will otherwise cost an hour.

- **Tap before `page.state` is `ready` and nothing happens.** Start a script
  with `wait-page`, and use it again after any `nav`.
- **Coordinates are the panel's 320x240, not the doubled window.**
- **A hold under 20 ms is refused**, because a press that short can fall
  between two of LVGL's input reads and be missed entirely. The default 60 ms
  is well clear of that and well under the 400 ms that makes a long press.
- **A swipe does nothing to the UI.** It is not an error and not a no-op at
  the transport level -- the pointer really moves -- but nothing is navigated
  and nothing is pressed. Use `tap-tile 0` to go up a page, the back bar at
  `tap 20 20` to leave an item screen, and `nav` to jump. The two notes below
  are still true of the drag itself, and still matter for scrolling and for
  dragging a slider.
- **A slower swipe is *less* likely to register than a fast one.** LVGL only
  counts movement towards a gesture on reads that moved at least 3 px, and
  needs more than 50 px in total. Spreading a short swipe over a long duration
  drops it under the first threshold, which is the opposite of the intuition.
  Both are checked before anything is queued -- `err gesture too short` and
  `err gesture too slow` -- rather than left to fail silently in the UI.
- **`press` without `release` leaves the UI holding a widget.** The next
  `tap`, `longpress` or `swipe` lifts it first; nothing else does.
- **`set` writes a real `config.json`.** Isolate a test with `OHEZ_CONFIG_DIR`.
- **A tile's rectangle moves.** Read it from `screen` rather than writing it
  down; `tap-label` does this for you.

## How it fits together

```
main/testif/testif.cpp          the socket and the command table
main/testif/testif_parse.c      the tokeniser -- the one piece with unit tests
main/testif/testif_touch.cpp    the synthetic pointer and its queue
main/testif/testif_report.cpp   the dumps, and `set` / `nav` / `settings`
main/testif/testif_shot.cpp     the framebuffer, and the /screenshot.raw route
tools/ohez_ctl.py               the client, and the PNG writer
```

`testif_loop()` is called from `ohez_loop()`, which is the one task that owns
LVGL, and the socket is non-blocking. So a command is carried out on the spot by
the only task allowed to carry it out -- there is no queue and no deferral, and
the latency is the 5 ms that loop turns over in. It also sidesteps the rule that
shaped `webui_transport_linux.cpp`: on the FreeRTOS POSIX simulator only
`select()` is wrapped, so a blocking socket call parks the whole cooperative
scheduler and the screen with it.

The synthetic pointer is a second LVGL input device beside the SDL mouse, so
the window stays clickable by hand while a script drives it. Its queue is
consumed one step per *read* rather than on a clock, which is what makes swipes
reliable: LVGL throws away the distance it has accumulated towards a gesture on
any read that did not move, and a schedule driven by wall-clock time would
sooner or later report the same position twice.

## On the device

Nothing here is compiled into a panel's firmware. Every file is behind
`#if CONFIG_IDF_TARGET_LINUX` with a no-op `#else`, the same arrangement
`main/sim/sim_offline.cpp` uses, so no caller needs a target guard and the
linker drops the lot.

Putting it on a panel would be:

- bind through lwIP instead of POSIX -- the same calls, and the same
  non-blocking rule;
- replace the framebuffer read, because a panel renders into small draw
  buffers and keeps no whole frame. Tee the display's `flush_cb`, invalidate
  the screen and write each flushed area out as it arrives with its own
  `{x1,y1,x2,y2}` header, letting the host reassemble it. Constant RAM, no
  allocation, and the reason the header above carries geometry rather than
  assuming it;
- put the whole thing behind a Kconfig that is off by default. An
  unauthenticated command channel that can press buttons is not something to
  ship enabled on a device that sits on someone's home network.

No PNG encoder and no large buffer is needed at any point, which is the whole
reason the wire format is raw pixels.
