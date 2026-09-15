# The built-in icon set

The panel used to get every widget icon from openHAB over HTTP: one GET per
tile on every page load, and another whenever an item's state changed the icon
it resolves to. A six-tile page therefore cost seven requests before it was
drawn, six of them for artwork that is identical on every openHAB installation
in the world and changes about once a release.

Those icons are in the firmware now. `icon_set_get()` answers first, and only a
miss becomes a request.

## Generating it

The artwork is **not in this repository**. The openHAB classic icon set is
copyright the openHAB project and licensed under the EPL-2.0, which this
project's GPL-3.0 cannot take in, so the generated file is gitignored and every
working copy makes its own:

```sh
tools/build_icon_set.py            # the whole set (~400 icons), 32x32
idf.py reconfigure                 # usually unnecessary; see below
```

That writes `main/icons/icon_set_data.h`. Without it nothing breaks: every
lookup finds nothing and every icon comes from openHAB, which is what the
firmware did before this existed. `idf.py` prints which of the two it is at
configure time:

```
-- oh-ez-touch: built-in icon set present (icon_set_data.h, 651552 bytes of source)
-- oh-ez-touch: no built-in icon set -- icons come from openHAB over HTTP; ...
```

and the panel says the same thing on the console at boot:

```
I (1234) openhab_client: built-in icons: 396, 119769 bytes
```

`main/CMakeLists.txt` watches `main/icons/` with `CMAKE_CONFIGURE_DEPENDS`, so
creating the header is normally enough to get it compiled in on the next build.
The explicit `idf.py reconfigure` is there for the case where that does not
fire — the failure mode is silent (a build that still fetches over HTTP), so it
is worth checking the configure line rather than assuming.

Useful arguments:

| | |
|---|---|
| `tools/build_icon_set.py light heating` | just these icons, state variants included |
| `--size 24` | a different edge length (default 32, what the tiles draw at) |
| `--bit-depth auto` | 1 or 2 bits per pixel where the palette allows it |
| `--colors 8` | a smaller palette |
| `--verify-all` | cross-check every icon against ImageMagick, not just the first |

Trimming the set is the lever that matters for flash, and on this firmware it
matters a lot: the whole set is 396 icons and 117 kB of blob, which costs
**126 kB of application image** and takes the free space in a 4 MB board's app
partition from 14% down to 7%. A panel only ever shows the icons its own
sitemaps name, and naming them explicitly turns that into a few kB.

## Why 16-colour PNG

The classic icons are line art: a few flat colours and an antialiased edge.
Sixteen colours holds that comfortably — what the palette mostly spends itself
on is alpha steps, not hues, which is why the quantizer works in RGBA rather
than RGB.

It stays a **PNG** rather than becoming a bitmap because the firmware links
lodepng either way, for the icons that still come from the server, and because
LVGL wants ARGB8888 only at draw time: 32x32 of that is 4096 bytes, against the
~300 a compressed indexed PNG takes. Storing decoded pixels would cost more
flash than the entire set does.

Fully transparent pixels get a palette entry of their own and are never mixed
into a box during quantization. Without that, transparent black averages into
the edge and every icon acquires a faint halo.

## What it resolves, and what it does not

`icon_set_get()` applies openHAB's own three rules, in order:

1. the state-specific icon — `light-on` for `light` in state `ON`;
2. for a numeric state, the nearest variant at or below it — `light` at 44,
   whose steps are `light-0` through `light-100` in tens, gets `light-40`;
3. the plain icon.

Rule 2 is load-bearing rather than a nicety. Once the built-in set answers
first, *its* rules are the ones the panel sees, and a lookup that knew only
rules 1 and 3 would draw every dimmer at full brightness — something the server
had been getting right.

It also has a trap in it, which `test/host/main/test_icon_set.cpp` exists to
keep shut. A step has to be *digits*: `light` ships `light-off` and `light-on`
alongside its numeric ladder, and `strtoul("off")` is 0. On `light` that bug
hides, because 0 loses to `light-20` and the caller never sees it. On the
twenty families that have named variants and no numeric ones — `contact`,
`door`, `lock`, `presence` — there is nothing for it to lose to, and every
numeric state resolves to `contact-ajar`.

A miss falls through to HTTP, which is what keeps custom icons working:
anything dropped into `$OPENHAB_CONF/icons/classic/` exists on that one server
and no firmware can have been built with it.

## Where the precedence lives

In `perform()` in `main/openhab/openhab_client.cpp`, and deliberately not in
the UI. A built-in icon and a fetched one then differ in nothing the caller can
see — same submit, same generation, same queue, the same PNG in the same result
— so the tile just gets its answer on the next turn of the loop instead of
after a round trip. The UI has no idea which happened.

The set also takes priority over offline mode's fixture, whose sixteen icons
(`tools/fetch_sim_icons.py`, `main/sim/icon_fixture.cpp`) are a subset of the
same artwork. Generating the built-in set therefore gives the simulator its
icons too, online or offline; the fixture remains only for a working copy that
has generated one and not the other.

## Tests

`test/host/main/test_icon_set.cpp`, in the host suite — `cd test/host && idf.py
build && ./build/oh-ez-touch-host-test.elf`.

The rules are tested against `test/host/main/icon_set_fixture.h`, fourteen
entries shaped like the generated table, and not against the real set. They
have to be: the artwork is not in the repository, so on a fresh clone or a CI
runner there is no generated table to test, and a suite that quietly skipped
itself there would be testing the one configuration nobody ships.
`icon_set.cpp` takes `ICON_SET_DATA_HEADER` to point at the fixture; the
firmware never defines it.

What the generated table *is* checked for is its own correctness, by the
generator: `--verify-all` decodes every icon back with ImageMagick rather than
with the encoder's own decoder, so a shared bug at both ends cannot agree with
itself.

## Turning it off

`CONFIG_OHEZ_ICONS_BUILTIN` (OhEzTouch → *Answer widget icons from the set
built into the firmware*, default y). Off makes the firmware fetch everything
again without deleting the generated header, which is how to measure what the
set costs in flash against what it saves in requests.

## The other two icon tools

| tool | output | for |
|---|---|---|
| `tools/build_icon_set.py` | `main/icons/icon_set_data.h` | the firmware's built-in set |
| `tools/fetch_openhab_icons.py` | `openhab-icons/*.png` | looking at what a name resolves to |
| `tools/fetch_sim_icons.py` | `main/sim/icon_fixture_data.h` | offline mode's sixteen fixtures |

All three fetch from the same place and share the listing, download and
rasterizing code; the difference is only what they write. See also
`doc/openhab-fixtures.md`, which lists where this panel and a real openHAB
disagree — the icons being PNG rather than SVG is one of the entries there.
