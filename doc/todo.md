# ToDo list

- [ ] openhab_ui: Fix icon loading. Some of the original icon file sizes are
      too large and have to be reencoded. See [Icon sizes](sitemap.md#icon-sizes).
- [ ] openhab_ui: Auto close item control screen after timeout.
      Straightforward now that there is a screen stack: one timer and an
      `ui_screen_pop()`.
- [ ] openhab_ui: Auto back to homescreen after timeout.
- [ ] openhab_ui: Prefer widget label text instead of item label text.
- [ ] openhab_ui: Add secured sections with PIN protection.
- [x] openhab_ui: Improve selection, setpoint and slider elements.
- [x] openhab: Take item states pushed over `/rest/events` instead of polling
      every tile every five seconds. See
      [The event stream](architecture.md#the-event-stream).
- [ ] openhab: Send an API token. The panel sends none, so `/rest/events`,
      the sitemap and the item states have to be open to an anonymous client.
- [ ] openhab: Follow sitemap changes as well. `/rest/sitemaps/events` pushes
      formatted labels, colours and visibility, which `/rest/events` does not.
- [x] ac: Improve OTA firmware update --> batchupdate.py. See
      [Updating devices](update.md).
- [x] main: Show portal active icon.
- [x] openhab_ui: Add theme support.
- [x] openhab_ui: Give each theme family its own chrome, tiles, typeface and
      chime rather than one geometry in three palettes. See
      [Screens, frames and motion](architecture.md#screens-frames-and-motion).
- [x] ui, control: Make the panel polyphonic. See
      [Hearing the panel](simulator.md#hearing-the-panel).
- [x] openhab_ui: Make the item controls screens of their own, laid out for
      a finger, and let them follow the server while they are open.
- [x] main, port, ui: Add screen calibration. Four corner taps, solved into the
      origin and span the pointer converts with, and a result that draws the
      correction it is about to make. See
      [Calibrating the touchscreen](configuration.md#calibrating-the-touchscreen).
- [x] main: Add setup wizard with WLAN credential input instead of portal
      procedure. See [Settings on the screen](configuration.md#settings-on-the-screen).
- [ ] doc: Retake the screenshots. `doc/img/browser_*.png` still show the
      removed AutoConnect pages, and `doc/img/arduitouch_main.jpeg` shows the
      pre-overhaul UI.
- [x] build: Replace `-O0`. `CONFIG_COMPILER_OPTIMIZATION_SIZE` saves 138 KB.
- [x] build: Give the renderer the CPU it was short of: 240 MHz, LVGL at
      `-O2`, `LV_USE_ASSERT_OBJ` off on the device, and a loop that sleeps for
      as long as LVGL asks. See
      [Where the frame time goes](architecture.md#where-the-frame-time-goes).
- [x] build, ui: Give the renderer a core of its own and a way to prove it.
      The main task moved to core 1, and the frame measures itself. See
      [Where the frame time goes](architecture.md#where-the-frame-time-goes).
- [ ] build: Four display levers are written up and none is taken, because
      each one needs a panel rather than an argument. See
      [What is left, and what it needs](architecture.md#what-is-left-and-what-it-needs).
- [x] ui, test: Bring the original look back as a fourth family, `Classic`.
- [x] ota: Delete the Arduino basic OTA, together with the Arduino framework.
- [ ] main: The device firmware built here has not been run on hardware. The
      display, touch, backlight and BME280 drivers are translations checked
      against the vendor sources, not measurements.
- [x] control, ui: Give the beeper melodies, envelopes and effects, and keep
      the chord mixer behind a Kconfig switch. See [The beeper](beeper.md).
- [ ] control: The beeper has been measured in the numbers it hands the pin,
      but it has not been heard on hardware. Nothing models the transducer.
      See [The beeper](beeper.md).
- [ ] sensors: Support DS18B20 onewire sensors.
- [x] peripherals: Drive the Lanbon L8's three relays and three mood LEDs
      over MQTT. See [Relays and LEDs](mqtt.md#relays-and-leds).
- [ ] peripherals: The relays and the LEDs have not been run on hardware. The
      pin table is the openHASP and ESPHome mapping for the L8-HS, not a
      measurement.
- [x] mqtt: Add an MQTT client. See [MQTT](mqtt.md).
- [x] ui, mqtt: Rework the LCARS sounds, add a door chime that only a broker
      can ring, and let MQTT play any sound. See
      [Playing a sound](mqtt.md#playing-a-sound).
- [x] control, ui: Add a thirty-second demonstration tune on a Demo button on
      the Audio settings page. See
      [The demonstration tune](beeper.md#the-demonstration-tune).
- [ ] mqtt: Support TLS. `CONFIG_MQTT_TRANSPORT_SSL` is off and the client
      speaks plain TCP.
- [ ] mqtt: Home Assistant style discovery, so the topics do not have to be
      wired up by hand.
- [x] ble: Scan for BLE beacons and publish them over MQTT. See
      [Bluetooth LE beacons](ble.md).
- [ ] ble: Show the beacons in range on the panel. The table is there;
      nothing draws it yet.
- [ ] ble: The BLE scanner has not been run on hardware either. The NimBLE
      port is checked against the IDF observer examples and the parsers
      against the format specifications, not against a real tag.
- [x] mqttviz: Add the topology view next to the mesh view -- access
      points from the panels' BSSID topics, pinnable positions, beacon
      placement by the distance topic, and an effects layer with an FX
      switch. See [The MQTT visualizer](mqttviz.md) and
      [the note](mqttviz-notes.md).
- [x] mqttviz: Keep whole arrangements as named layouts, restorable in
      one click, with a backup file beside the state that remembers
      what a restore replaced. See
      [Arrangements, named](mqttviz.md#arrangements-named).
- [x] mqttviz: Give the topology view a workspace of four times the
      area with a camera of its own -- wheel zoom, pan, home -- and a
      one-time conversion that keeps every pin where the eye left it.
      See [The workspace and the zoom](mqttviz.md#the-workspace-and-the-zoom).
- [ ] doc: Update the visualizer screenshots. `doc/img` has none of the
      topology view, the physics panel's fourth tab, the FX layer or
      the layouts dialog.
