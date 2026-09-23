# Testing

## Host unit tests

The unit tests are an ESP-IDF project of their own, on the same `linux`
target as the simulator:

```bash
cd test/host
idf.py --preview set-target linux
idf.py build && ./build/oh-ez-touch-host-test.elf
```

The binary exits with the number of failures. It can be used in a script.

Every file linked out of `main/` into the tests touches neither LVGL nor the
network. This is deliberate. The beacon parsers get raw advertisement bytes
from the port layer (`main/port/port_ble.h`), and the multipart scanner is a
sink, so both can be tested on the host.

### Test suites

- **test_item_setters**: the string setters of `Item`. They copy labels,
  states, patterns and mappings of unknown length from the sitemap JSON. The
  copies must truncate cleanly. A canary after the object catches one that
  does not.
- **test_ui_theme**: the theme name lookups in `main/ui/ui_theme.hpp`. The
  fallback must hold for a typo, an empty string and NULL. It also holds for
  `Default`, the old name of the Material theme. A `config.json` written by
  an older firmware keeps its look through this fallback.
- **test_config_fields**: the settings table in `main/config/config_fields.cpp`.
  No two rows share a name. A name is usable as a POST argument and as an
  MQTT topic segment. Every tab has rows. The restart flags are correct. No
  two rows claim the same storage place. Every default is inside its own
  range.
- **test_config_file**: `config.json` itself. The shipped defaults, a value
  per section surviving save and load, a corrupt file, a file with missing
  sections, and the validation. The suite makes its own directory under
  `$TMPDIR`. It cannot touch the config of a running simulator.
- **test_multipart**: the firmware upload's body scanner in `main/web/multipart.c`.
  This is the one path that can leave a panel unbootable. It parses bytes
  somebody else chose, with no authentication in front of it. The suite found
  a buffer overrun on the first run: nothing checked the length of the
  multipart boundary.
- **test_ble_beacon**: the advertisement parsers in `main/ble/ble_beacon.cpp`.
  The iBeacon and Eddystone layouts come from devices nobody here wrote.
  Malformed input is the normal case. The suite walks a valid advertisement
  truncated at every possible length. It found two bugs in its own fixtures
  and one in the URL character ranges on the first run.
- **test_testif_parse**: the request tokenizer in `main/testif/testif_parse.c`.
  It reads datagrams composed outside the process. The cases that matter are
  the ones a hand-typed `nc -u` line produces: a trailing newline, a doubled
  space, an unclosed quote, a lone `@`, and the two token limits.
- **test_touch_cal**: the touchscreen calibration in `main/port/touch_cal.c`.
  The map is swept over the whole 12-bit input space against a second copy of
  the expression `port_indev.c` carried before it was extracted, in both
  orientations with the flip both ways: the two have to agree everywhere, which
  is what makes the extraction provably a move. The solve is checked by round
  trip -- a panel is invented, its map is run backwards to produce the readings
  four corner presses would give, and what comes out has to put every pixel
  back within one of itself. Then every refusal: four identical readings, a
  mirrored panel, two presses at one end that disagree, a short set, and the
  widest term the arithmetic can produce. This is the one file here whose bugs
  cannot be found by running the thing, because a calibration wrong enough to
  notice has already taken away the pointer needed to correct it.
- **test_beeper_mixer**: the arithmetic in `main/control/beeper_mixer.c`.
  Envelope curves and edges, sweeps in both directions, and how a frame
  divides itself between voices. Two assertions are load-bearing: a frame is
  a pure function of its arguments, and the shipped defaults come out at 63
  counts of duty (the loudness every beep of this firmware has ever had).
- **test_beeper_seq**: the default engine's arithmetic. The same sweeps, a
  glide that must stay monotone, an LFO that must not overflow, a tremolo
  that must not pass the envelope's peak, and a zero-length note that must be
  stepped over.
- **test_ui_beep_chimes** and **test_ui_beep_tunes**: the sound tables, one
  suite per engine. Three families times eighteen sounds. The suites check
  that no sound is missing, that frequencies stay in the band a small piezo
  is loud in, and that no sound outstays the gesture it answers. Both suites
  run even though a panel ships one engine: the unselected engine is the one
  nobody would notice going stale.

## Testing against a real openHAB

The simulator's compiled-in fixtures draw a screen without a server. They
cannot say what openHAB really sends. `test/openhab/` holds sitemaps and
items for a real server:

- One page with six widget types.
- One page with the shapes the first page has no example of.
- A five-level navigation tree that reaches every fixed limit of the panel.

`test/openhab/seed-states.sh` gives them all a value worth looking at.

See [doc/openhab-fixtures.md](openhab-fixtures.md) for installation, the wire
format notes, and the places where the panel and the server still disagree.

## The control interface

The simulator listens on 127.0.0.1:8781 for commands, and a bench device
offers the same channel on the network when it is built with
`CONFIG_OHEZ_TESTIF` (the `arduitouch_jtag` defaults do; every other target
leaves it off). `tools/ohez_ctl.py` is the client for both. It can tap tiles
by label, read the screen as JSON, ask for the heap's shape and pull
screenshots -- the last of those on the simulator only. See
[Simulator](simulator.md#driving-it-from-a-script) and the full reference in
[doc/test-interface.md](test-interface.md).
