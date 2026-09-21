# System architecture

OhEzTouch is an ESP32 firmware built with ESP-IDF. The same source code also
builds a desktop simulator for Linux. This document gives an overview of the
layers, the main tasks and the performance limits.

## Layers

```
+---------------------------------------------------+
| UI (LVGL)          pages, item screens, settings  |
+---------------------------------------------------+
| Application        openHAB client, MQTT, BLE,     |
|                    sensors, web interface         |
+---------------------------------------------------+
| Control            dimming policy, beeper queue   |
+---------------------------------------------------+
| Port layer         one implementation per target  |
| (main/port/)       esp32 | linux                  |
+---------------------------------------------------+
| Hardware           display, touch, backlight,     |
|                    beeper, relays, LEDs, radios   |
+---------------------------------------------------+
```

Everything above `main/port/` is shared between the targets. The port layer
is the whole of what differs between a panel and a desktop. If a change needs
an `#if` on the target outside that directory, it probably wants a new port
instead.

## Source layout

```
main/                 main.cpp -- the entry point
main/config/          the settings, and the one field table that both the
                      panel's settings screen and the web form walk
main/ui/              the LVGL user interface: the openHAB page, the settings
                      screen, the styles, themes and motion
main/ui/frames/       one per theme family: the chrome it draws around the
                      tiles, and where it lets them sit
main/ui/items/        one per openHAB item type: the screen its tile opens
main/openhab/         the openHAB client: the task every request waits on,
                      the sitemap model and parser it feeds, and the caches of
                      which servers are on the network and which sitemaps they
                      offer
main/mqtt/            the MQTT client: what the panel tells a broker, and the
                      one way the broker can talk back
main/ble/             the BLE beacon scanner: the advertisement parsers, and
                      the table of what is in range
main/peripherals/     the sensors: the BME280, and the timer that decides when
                      the next reading is taken
main/web/             the web interface: one renderer, one transport per target
main/net/             WLAN credentials, the radio state machine, and the one
                      mDNS question this firmware asks
main/control/         policy on top of the port layer: when to dim, and the
                      queue that plays a chime
main/sim/             the simulator's offline fixtures
main/testif/          the simulator's control interface: the UDP command
                      socket, the synthetic pointer, and the screen dumps
main/port/            the platform boundary: one implementation directory per
                      target
components/           LVGL and ArduinoJson as submodules, lodepng vendored
test/host/            the unit tests, as an IDF project of their own
```

## The settings table

Every setting is described once, in `main/config/config_fields.cpp`. The web
form, the panel's settings screen, the MQTT client and the config file all
walk that table. Each row carries the name, the label, the kind, the default,
the range or options, the storage place in `config.json` and a flag for
"read at boot only". This is why the three front ends cannot drift apart.

## The openHAB client task

Generating the UI means asking openHAB for things: a sitemap page per
navigation level, an icon per tile, and an item state per tile every five
seconds.

All requests wait on a task of their own
(`main/openhab/openhab_client.cpp`). The UI submits a URL and carries on
drawing. The answer arrives on a queue. `openhab_ui_loop()` takes one result
from the queue per iteration. A page appears as soon as its JSON parses.
Every tile shows its label, its state and a placeholder watermark first. The
icons fill in over the next few frames. The clock keeps ticking and the touch
keeps responding, also while openHAB is unreachable.

The worker calls no `lv_*` function. It reads no `Item`, `Sitemap` or
`Config`. URLs go in and bytes come back. Every decision about what a body
means, and every LVGL call, stays on the task that owns the screen.
`main/openhab/openhab_connector.cpp` is a model and a parser with no idea
that a network exists. That is what lets the host tests cover the parser.

Two more requests share the worker:

- **The sitemap list.** `main/openhab/openhab_sitemaps.cpp` fetches the list
  of sitemaps a server offers. Both settings front ends show it beside the
  Sitemap field. One cache serves both.
- **Server discovery.** `main/openhab/openhab_discover.cpp` sends one mDNS
  query (`main/net/mdns_query.c`) from its own UDP socket and reads the
  answers without blocking. One 44-byte datagram, sent twice, and a
  two-and-a-half-second window. There is no `espressif/mdns` component behind
  it. The whole feature costs about 4 KB of flash and runs on the host for
  the tests.

Requests are stamped with a generation. The generation is bumped whenever a
page is fetched. Icons and states for a page the user navigated away from are
dropped. Commands are exempt: a command the user asked for is still delivered
after they have moved on.

There is one worker and one queue, not a pool. A pool could deliver two taps
on the same item out of order. The connection to openHAB is held open between
requests. A page's six icons share one TCP handshake.

A page request goes to the front of the queue. The worker is one task and a
request in flight cannot be cancelled, so a page submitted behind a queue of
state polls waited for all of them. The polls it overtakes belong to the page
being left and are dropped unmade anyway.

The connection is dropped when the link changes. A TCP connection that spanned
a reassociation is dead, and the WLAN state machine is the only thing on the
panel that knows the link went away, so it says so (`openhab_http_reset()`).
Without that, the first request of every episode writes into the dead socket,
which succeeds, and then waits the full timeout for an answer that cannot come.

Bodies are read into one buffer, allocated once and sized for the largest
class: a 12 KB sitemap page. A buffer allocated per page load needed 12 KB of
heap in one piece, and on a weak link the WiFi driver holds its transmit
buffers for as long as a frame is being retried -- so a page fetch asked for
the largest block the heap had at the moment it had least to give. The symptom
was a panel whose tiles kept updating while every sub page it navigated to
failed. An item state does not even need that: it rides inside the result, so a
poll costs no allocation at all.

A body cut short by the network is a failure, not a short answer. The
distinction is made in `main/openhab/openhab_http.cpp`, and it has to be made
by hand: `esp_http_client_read_response()` reports a read timeout as a
non-negative byte count, exactly as it reports a complete body. A state poll
that timed out therefore used to arrive as a *successful* poll with an empty
body, which the item then stored -- a switch showing nothing and a temperature
reading zero, on a panel whose server was perfectly healthy and whose signal
was merely poor.

## Screens, frames and motion

The UI is built from three ideas.

**One pushed screen.** `ui_screen` owns a root and at most one thing covering
it: an item control or the settings screen. It is not a general stack. The
panel has exactly those two cases.

**A frame per theme family.** `main/ui/frames/` is where a theme stops being
a palette. Each family builds its own chrome and answers `content_area()`
with the rectangle the tile grid may have. This lets LCARS put a spine down
the left edge, Material have no chrome at all, and Reticle draw two
hairlines. The page knows nothing about it. Classic uses the shared frame in
`ui_frame_common.cpp`. The grid is solved by `ui_geometry.hpp`, which is free
of `<lvgl.h>` so the host tests can check it.

**Two orientations.** A panel mounted upright sets Orientation to `portrait`
and the display is created at 240x320 rather than 320x240 -- hardware, not
software rotation: the panel's MADCTL simply keeps its own axes, so a frame
costs exactly what it costs in landscape. The setting is boot-only because
the MADCTL and the touch calibration are init-time decisions. Every theme
table entry carries a second grid for it (two columns of three tiles), every
frame measures its chrome against the resolution LVGL reports, and the page
asks `ui_style_grid()` which of the two to pack. The host geometry tests
check both orientations against the same finger floor.

A frame is not made of style properties, so `lv_obj_report_style_change()`
cannot reach it. A live theme change tears the old family's chrome down
*before* `ui_style_select()` and builds the new one after.

**A screen per item type.** `main/ui/items/` has one file per openHAB item
type, found through a registry. Each carries a `refresh` hook, so an open
control follows the server.

### What the panel can afford to animate

The binding constraint is not the CPU. A 320x240 RGB565 panel on a 40 MHz SPI
bus moves 5 MB/s. A full-screen repaint is **30.7 ms of pure transfer**. The
render costs about a quarter of that. Frame time is SPI time. The budget is a
*pixel* budget: roughly 24,000 px per frame for anything sustained, 40,000
for a one-shot.

This is why there are no screen transitions on paths the user walks often.
`lv_screen_load_anim()` moves the screen object. Every frame invalidates
320x240 twice. A 200 ms slide is six frames of full repaint. The settings
screen is the one exception, and it is rare. Everything else keeps the screen
still and staggers its *contents*.

`ui_motion.hpp` documents the two constructs that look free and are not:
`opa_layered` and `transform_scale`. Plain `lv_obj_set_style_opa()` fades a
whole subtree with no layer. `transform_width` and `transform_height` deform
a plate the same way.

One rule prevents most crashes here: **every animation's `var` is the
`lv_obj_t` it animates**, never a context struct. `lv_obj_delete()` cancels
animations keyed on the object it deletes.

## Where the frame time goes

There is no 2D accelerator on an ESP32. Every pixel is written by one of the
software loops in LVGL's `src/draw/sw/`. The finished strip is then pushed to
the panel over SPI. These two costs bound everything the UI can do.

**The SPI side is arithmetic.** The bus runs at 40 MHz. On the Lanbon that is
the ESP32's own ceiling (its SPI pins route through the GPIO matrix). On the
ArduiTouch boards it is as far as an ILI9341 is worth pushing. A full-screen
repaint is 320 x 240 x 2 bytes at 40 Mbit/s: about 31 ms, however fast the
CPU is. A partial repaint costs in proportion to its area. This is why the UI
is built out of tiles that invalidate one at a time.

**The CPU side is where the settings apply.** `main/port/esp32/port_display.c`
renders into two DMA-capable buffers and alternates them. The render of one
strip overlaps the transfer of the last. Keeping the render under the 3 ms
that transfer takes makes the SPI figure the real floor. Five things go into
that:

- **Core 1, not core 0.** `CONFIG_ESP_MAIN_TASK_AFFINITY_CPU1` in
  `sdkconfig.defaults.esp32`. IDF starts the main task on core 0, where it
  also pins the WiFi task, the Bluetooth controller, the NimBLE host and the
  esp_timer task. The ESP32 has one 32 KB cache per core. On core 0 the radio
  code evicted the render loops and the font glyphs.
- **240 MHz, not 160.** `CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_240`. A mains-powered
  wall panel does not save the power.
- **LVGL is compiled `-O2`.** The rest of the project uses `-Os`.
  `components/lvgl/CMakeLists.txt` makes the exception. It costs 19 KB of a
  1920 KB app partition.
- **`LV_USE_ASSERT_OBJ` is off on the device** and on in the simulator. Its
  check walks every object of every screen at the top of nearly every public
  LVGL call.
- **The loop sleeps for as long as LVGL asks**, not a fixed 5 ms.
  `ohez_loop()` takes `lv_timer_handler()`'s answer and waits that long,
  capped at 10 ms so the other loops still run.

Two things look like levers and are not. The byte swap in `flush_cb()` is
about 4 % of a flush; rendering in `LV_COLOR_FORMAT_RGB565_SWAPPED` would
move the cost onto every blended pixel. And `LV_DRAW_LAYER_SIMPLE_BUF_SIZE`
is dead config here: `ui_motion.hpp` rules out every property that would
promote an object to a layer.

### Measuring it

The panel measures itself. The same code runs in the simulator.

`main/ui/ui_frame_probe.c` hangs seven event callbacks on the display. LVGL
brackets a frame with them: `REFR_START`/`REFR_READY` around the refresh,
`RENDER_START`/`RENDER_READY` around the drawing, `FLUSH_START` with the
strip's area, and `FLUSH_WAIT_START`/`FLUSH_WAIT_FINISH` around the wait for
the previous transfer. `main/ui/ui_frame_stats.c` averages them over a window
of 64 frames or ten seconds. The answer is published in three places: the
**Frame** rows on the web status page, `system/fps`, `system/render_us` and
`system/wait_us` over MQTT, and the `frame` object of the test interface's
`status`.

**`render_us` against `wait_us` decides what to do next.** If the wait
dominates, the panel is SPI-bound at the 31 ms above. Only the bus clock
helps. If the renderer dominates, the CPU-side levers below are worth their
risk.

### What is left, and what it needs

Four levers remain. Each needs a board rather than an argument:

- **80 MHz SPI on the ArduiTouch boards.** The only lever that touches the
  31 ms itself. The display pins are an exact IOMUX match. An ILI9341 is
  already past its datasheet write cycle at 40 MHz, so this is a measurement
  question. It fails as visible corruption, not as a brick.
- **Flash in QIO mode.** `CONFIG_ESPTOOLPY_FLASHMODE_DIO` today. LVGL's draw
  code and 1.3 MB of font glyphs are read straight out of flash. QIO roughly
  halves what a cache miss costs. It fails as a module that will not boot.
- **`LV_ATTRIBUTE_FAST_MEM` as `IRAM_ATTR`.** It is empty in `lv_conf.h`. It
  would cover the hot blend loops. About 33 KB of IRAM is free, but that pool
  was freed for the Bluetooth controller. Read the map before spending it.
- **The strip height, `DRAW_BUFFER_LINES`.** Taller strips walk the object
  tree fewer times per frame. This only helps if the render is losing the
  race against the transfer. The buffers come out of the heap before the
  radios start.

One more thing: LVGL waits for the DMA in a busy spin
(`while(disp->flushing);`). The spin ends when the transfer does, so no frame
is late. But the CPU looks fully occupied while it has nothing to do. The
CPU-side levers buy less than their share of a frame suggests.

## The simulator as architecture proof

The simulator is not a reduced build. It is the same sources, the same LVGL,
the same fonts and styles. Only `main/port/` differs. This is a deliberate
design property: everything above the port layer is tested and used daily on
the host. See [Simulator](simulator.md) and [Testing](testing.md).
