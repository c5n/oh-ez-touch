# Fonts

The LVGL font sources in `components/lvgl/fonts/` are generated and
committed. A normal build needs no font tooling.

There are three faces, one per theme family:

| Family | Face | Theme | Licence |
| --- | --- | --- | --- |
| `ui` | Barlow Condensed | Material, Classic | OFL 1.1 |
| `hud` | Rajdhani | Reticle | OFL 1.1 |
| `lcars` | LCARS GTJ3 | LCARS | Freeware, © [GTJLCARS.de](https://www.gtjlcars.de) |

The `ui` face is condensed because a tile is narrow. With plain Barlow, too
many state lines did not fit and were cut off. Barlow Condensed has the same
letterforms and cap height and sets text about 25 % narrower.

LCARS GTJ3 is by Jens Goettling, [www.gtjlcars.de](https://www.gtjlcars.de).
It is freeware: it may be used anywhere, but the font files must not be
renamed, changed or sold. For this reason the TTF is not in this repository.
Only the generated glyph subsets are. LCARS GTJ3 has capitals only, and it has
one weight, so all three LCARS sizes use it. At the same pixel size its
capitals are lower than Antonio's were. For this reason the LCARS sizes are
19, 26 and 43 px instead of 16, 22 and 36 px. They give the cap heights that
Antonio had (14, 19 and 31 px), so the text looks as large as before. It is
about 12 % narrower.

Each `ui` and `hud` size is generated from a **different static weight** of
its face: Regular at 16, Medium at 22, SemiBold at 36. A caption, a label and
a reading differ in weight as well as in size. That is where the type
hierarchy comes from.

This is also why Barlow and Rajdhani. Inter, Manrope, Figtree, Outfit, Public
Sans, Work Sans, DM Sans, IBM Plex Sans, Archivo, Saira and Space Grotesk are
all variable-only in `google/fonts`. `lv_font_conv` renders a variable font's
default instance only.

The nine faces cost about 200 KB of flash. That is the largest single item in
the firmware after LVGL itself.

> **NOTE:** The original UI was set in Roboto (Apache-2.0). Roboto is no
> longer in the firmware. The Classic theme uses Barlow Condensed, as
> Material does. The screensaver clock keeps plain Barlow SemiBold.

## Regenerating

Regenerate the fonts after changing a face, a size or a glyph range:

```bash
LCARS_FONT=~/Downloads/LCARSGTJ3.zip tools/build_fonts.sh
```

The script needs `lv_font_conv` (an npm tool) and network access to fetch the
faces from Google Fonts. The `lcars` family also needs `LCARS_FONT`: the path
to `LCARSGTJ3.ttf` or to the `LCARSGTJ3.zip` from
[www.gtjlcars.de](https://www.gtjlcars.de/LCARSindex/LCARSFONTS.htm). To
regenerate one family only, name it: `tools/build_fonts.sh ui`.
