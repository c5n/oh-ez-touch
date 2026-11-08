# ToDo list

- [x] openhab_ui: Fix icon loading. The classic icons are built in, and a
      server icon may fill the receive buffer (12 KB) instead of 5000 bytes.
      See [Icon sizes](sitemap.md#icon-sizes).
- [x] openhab_ui: Auto close item control screen after timeout. On the
      activity timeout, on the home page as well. See
      [LCD Backlight Dimming](configuration.md#lcd-backlight-dimming).
- [x] openhab_ui: Auto back to homescreen after timeout. On the activity
      timeout, when the display dims.
- [ ] openhab_ui: Prefer widget label text instead of item label text.
- [ ] openhab_ui: Add secured sections with PIN protection.
- [x] openhab_ui: Improve selection, setpoint and slider elements.
- [x] openhab: Take item states pushed over `/rest/events` instead of polling
      every tile every five seconds. See
      [The event stream](architecture.md#the-event-stream).
- [x] openhab_ui: Show an Image item's picture, a doorbell's or a camera's,
      and open it from any page when the bell rings. See
      [Image](sitemap.md#image-a-doorbell-or-a-camera) and
      [Doorbell](configuration.md#doorbell).
- [x] main, openhab, build: Give the heap back on an ESP32 with every feature
      on. Measured task stacks and WiFi buffers, no buffers for features that
      are off, a refused sitemap stream asked again. See
      [The heap budget](architecture.md#the-heap-budget).
- [x] openhab_ui: Decode a server icon without a block of its own size. It
      is inflated into 2 KB segments, the first ones in the receive buffer,
      and downscaled from them. See
      [What a page load allocates](architecture.md#what-a-page-load-allocates).
- [x] openhab: Keep a ring the heap could not parse. The doorbell reads such
      an event once more as text, without the heap.
- [x] openhab: Build the event parsers' filters under a lock. The event task
      and the doorbell task both parse, and either can be first.
- [ ] openhab: The picture has not been fetched on hardware. Measure the
      client task's stack (4 KB, with TJpgDec on it) and the heap during a
      ring, and whether an Image item's page loads on a fragmented heap.
- [ ] openhab_ui: Show a progressive JPEG or a PNG. TJpgDec reads baseline
      JPEG only, and a PNG would have to be decoded whole.
- [ ] openhab: Send an API token. The panel sends none, so `/rest/events`,
      `/rest/sitemaps/events`, the sitemap and the item states have to be open
      to an anonymous client.
- [x] openhab: Follow sitemap changes as well. `/rest/sitemaps/events` pushes
      formatted labels, colours and visibility, which `/rest/events` does not.
      See [The event stream](architecture.md#the-event-stream).
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
- [ ] main: The firmware runs on an ArduiTouch with WiFi, openHAB, MQTT, the
      BLE scan and the BME280 on, and a 10-minute soak passed (2026-11-08).
      Still not measured: the touch accuracy, the backlight levels and the
      BME280 values against a reference. The other boards have not run it.
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
- [ ] ble: The BLE scan runs on an ArduiTouch (active scan, every 30 s), but
      no tag was in range: `ble/count` stayed 0. The parsers are checked
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
- [x] mqttviz: A 3D space view: a SweetHome3D house imported as a
      laser-projection hologram, broker, access points and panels
      placed on its walls and floors, beacons multilaterated in metres.
      See [The space view](mqttviz.md#the-space-view).
- [ ] mqttviz: Try the space view against real SweetHome3D homes --
      it was checked against generated `Home.xml` files only. Round
      walls' bending direction and levels with `elevationIndex` ties
      are the likely surprises.
- [x] doc: The visualizer's pictures -- mesh, topology, detail,
      physics, layouts and the space view -- taken from a demo fleet by
      `mqttviz/demo/make-screenshots.sh`. See
      [Trying it without panels](mqttviz.md#trying-it-without-panels).
- [x] mqttviz: The space view's lines carry their lengths in metres,
      lying along them. See
      [The lines tell how long they are](mqttviz.md#the-lines-tell-how-long-they-are).
- [x] mqttviz: A view cube in the space view -- faces and bevels as
      ways to look, a drag as the orbit's hand, zoom and framing
      buttons under it. See
      [The view cube](mqttviz.md#the-view-cube).
- [x] mqttviz: A GUI size setting, one scale for the whole chrome
      (70 to 160 per cent), kept with the settings. See
      [Config](mqttviz.md#config).
- [x] mqttviz: Place beacons by weighted hearings -- 1/d², near
      readings outvoting far guesses through floors -- with a sweep
      over the house's plan first, so the near readings' crossing wins
      over its mirror. See
      [Beacons find their own places](mqttviz.md#beacons-find-their-own-places).
