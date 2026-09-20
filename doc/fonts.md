# Fonts

The LVGL font sources in `components/lvgl/fonts/` are generated and
committed. A normal build needs no font tooling.

There are three faces, one per theme family:

| Family | Face | Theme | Licence |
| --- | --- | --- | --- |
| `ui` | Barlow | Material | OFL 1.1 |
| `hud` | Rajdhani | Reticle | OFL 1.1 |
| `lcars` | Antonio | LCARS | OFL 1.1 |

Each of the three sizes is generated from a **different static weight** of
its face: Regular at 16, Medium at 22, SemiBold at 36. A caption, a label and
a reading differ in weight as well as in size. That is where the type
hierarchy comes from.

This is also why Barlow and Rajdhani. Inter, Manrope, Figtree, Outfit, Public
Sans, Work Sans, DM Sans, IBM Plex Sans, Archivo, Saira and Space Grotesk are
all variable-only in `google/fonts`. `lv_font_conv` renders a variable font's
default instance only. Antonio is variable too, and stays that way: LCARS
wants one weight anyway.

The nine faces cost about 210 KB of flash. That is the largest single item in
the firmware after LVGL itself.

> **NOTE:** The original UI was set in Roboto (Apache-2.0). Roboto is no
> longer in the firmware. The Classic theme uses Barlow, as Material does.

## Regenerating

Regenerate the fonts after changing a face, a size or a glyph range:

```bash
tools/build_fonts.sh
```

The script needs `lv_font_conv` (an npm tool) and network access to fetch the
faces from Google Fonts.
