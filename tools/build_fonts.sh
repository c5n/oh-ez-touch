#!/bin/bash
#
# Regenerate the LVGL font sources in components/lvgl/fonts/.
#
# The fonts are committed, so this only has to run when a face, a size or a
# glyph range changes. Each generated .c also records its own command line in
# its header comment; this script is the same information in runnable form.
#
# Three families, one per theme:
#   ui      -- Material's and Classic's face, Barlow Condensed (SIL OFL 1.1).
#              The condensed cut of Barlow, a humanist signage face with a
#              tall x-height and open apertures. Condensed because a tile is
#              98 px wide and the state line has to fit in it: plain Barlow
#              truncated too many states.
#   hud     -- Reticle's face, Rajdhani (SIL OFL 1.1). Flat-sided, low
#              contrast, clipped joins, and narrow near-tabular digits: at
#              36 px SemiBold five of them fit a 98 px tile where Barlow fits
#              four, which is the practical reason an instrument panel wants it.
#   lcars   -- the LCARS theme's face, LCARS GTJ3 by Jens Goettling
#              (https://www.gtjlcars.de). Freeware: free to use anywhere, but
#              the font files must not be renamed, changed or sold. So the
#              TTF is not in this repository and is not downloaded here; only
#              the generated glyph subsets are committed. Pass the font as
#              LCARS_FONT, either LCARSGTJ3.ttf or the LCARSGTJ3.zip it ships
#              in.
#
# Each size is generated from a *different static weight* of its face, so the
# caption, the label and the reading differ in weight as well as in size. That
# is the whole hierarchy and it costs nothing -- three files either way. It is
# also why Barlow and Rajdhani rather than the obvious modern choices: Inter,
# Manrope, Figtree, Outfit, Public Sans, Work Sans, DM Sans, IBM Plex Sans,
# Archivo, Saira and Space Grotesk are all variable-only in google/fonts, and
# lv_font_conv renders a variable font's default instance -- Regular -- with no
# way to ask for another short of instancing it with fonttools first.
#
# LCARS GTJ3 has one weight, so all three LCARS sizes use it.
#
# The LV_SYMBOL_* glyphs are not in either face and are merged in from LVGL's
# own FontAwesome subset, so a theme switch cannot lose an icon.
#
# lv_font_conv is an npm tool. It is invoked through `pnpm dlx` so that nothing
# has to be installed into the tree; set LV_FONT_CONV to override, e.g. if you
# have it installed globally:
#   LV_FONT_CONV="lv_font_conv" tools/build_fonts.sh
#
# Usage: [LCARS_FONT=path/to/LCARSGTJ3.zip] tools/build_fonts.sh [ui|hud|lcars|all]
#        (default: all; LCARS_FONT is needed for lcars and all)

set -euo pipefail

cd "$(dirname "$0")/.."

OUT="components/lvgl/fonts"
FA="components/lvgl/lvgl/scripts/generators/built_in_font/FontAwesome5-Solid+Brands+Regular.woff"
GF="https://raw.githubusercontent.com/google/fonts/main/ofl"
LCARS_FONT="${LCARS_FONT:-}"

# Static instances, one per size role.
BARLOW_URL_SMALL="$GF/barlowcondensed/BarlowCondensed-Regular.ttf"
BARLOW_URL_NORMAL="$GF/barlowcondensed/BarlowCondensed-Medium.ttf"
BARLOW_URL_LARGE="$GF/barlowcondensed/BarlowCondensed-SemiBold.ttf"
# The screensaver clock keeps plain Barlow: it has the whole panel width.
BARLOW_URL_CLOCK="$GF/barlow/Barlow-SemiBold.ttf"
RAJDHANI_URL_SMALL="$GF/rajdhani/Rajdhani-Regular.ttf"
RAJDHANI_URL_NORMAL="$GF/rajdhani/Rajdhani-Medium.ttf"
RAJDHANI_URL_LARGE="$GF/rajdhani/Rajdhani-SemiBold.ttf"

WORK="${WORK:-$(mktemp -d -t ohez-fonts-XXXXXX)}"

# fetch <url> <name> -- into $WORK, once.
fetch()
{
    local url="$1" name="$2"

    # Progress to stderr: stdout is the path, and the caller uses it in a
    # command substitution.
    if [ ! -s "$WORK/$name" ]; then
        echo "Fetching $name ..." >&2
        curl -sSLf -o "$WORK/$name" "$url"
    fi

    echo "$WORK/$name"
}

: "${LV_FONT_CONV:=pnpm dlx lv_font_conv@1.5.3}"

# The LV_SYMBOL_* codepoints the 16 px and 22 px fonts carry. The 36 px font
# used to get only the two watermark symbols, on the grounds that it was only
# ever used for a numeric reading -- which stopped being true when the item
# screens gave whole controls a single glyph as their content. FA_LARGE now
# also carries the ten a control can be: transport, plus, minus, up, down and
# the back chevron. Without them those pads render a placeholder box.
FA_FULL="61441,61448,61451,61452,61453,61457,61459,61461,61465,61468,61473,61478,61479,61480,61502,61512,61515,61516,61517,61521,61522,61523,61524,61543,61544,61550,61552,61553,61556,61559,61560,61561,61563,61587,61589,61636,61637,61639,61671,61674,61683,61724,61732,61787,61931,62016,62017,62018,62019,62020,62087,62099,62189,62212,62810,63426,63650"
#         eye   nline plus  minus prev  play  pause stop  next  left  up    down
FA_LARGE="61550,63650,61543,61544,61512,61515,61516,61517,61521,61523,61559,61560"

# The Latin-1 letters beyond 0x7F the 36 px faces carry: the German ones,
# Ä Ö Ü ß ä ö ü. The 16 px and 22 px faces have all of 0xA0-0xFF. The large
# faces show the value of a tile or a clock item, and that value is often a
# String state ("grün"), or "März" in the screensaver's month names
# (ui_clock.cpp). A missing glyph draws as a box. The rest of 0xA0-0xFF is
# left out: compressed, it costs 65-80 bytes per glyph and face, about 25 KB
# for the three faces, and the app partition does not have that to spare.
# These seven cost about 2 KB.
FA_UMLAUT="0xC4,0xD6,0xDC,0xDF,0xE4,0xF6,0xFC"

[ -f "$FA" ] || { echo "FontAwesome subset not found: $FA (run 'git submodule update --init' once)" >&2; exit 1; }

# gen <face-file> <name> <size> <latin-range-args...> -- <fontawesome-list>
gen()
{
    local face="$1" name="$2" size="$3"; shift 3
    local ranges=() fa=""
    while [ "$1" != "--" ]; do ranges+=("$1"); shift; done
    shift; fa="$1"

    # Compression above 20 px: the bigger the glyphs, the better the RLE
    # ratio and the more the panel's flash cares. The 16 px captions stay
    # raw, because they are what every line of every list draws and the
    # decompression is per glyph, per frame. A compressed font needs
    # LV_USE_FONT_COMPRESSED at draw time -- see lv_conf.h.
    local store_flags="--no-compress --no-prefilter"
    if [ "$size" -gt 20 ]; then
        store_flags=""
    fi

    echo "  $name"
    # shellcheck disable=SC2086
    $LV_FONT_CONV --format lvgl --lv-include lvgl.h --bpp 4 \
        $store_flags --force-fast-kern-format \
        --size "$size" \
        --font "$face" "${ranges[@]}" \
        --font "$FA" -r "$fa" \
        --lv-font-name "$name" \
        -o "$OUT/$name.c"
}

FAMILY="${1:-all}"

echo "Generating fonts into $OUT ..."

if [ "$FAMILY" = ui ] || [ "$FAMILY" = all ]; then
    gen "$(fetch "$BARLOW_URL_SMALL"  BarlowCondensed-Regular.ttf)"  custom_font_ui_16 16 -r 0x20-0x7F -r 0xA0-0xFF -- "$FA_FULL"
    gen "$(fetch "$BARLOW_URL_NORMAL" BarlowCondensed-Medium.ttf)"   custom_font_ui_22 22 -r 0x20-0x7F -r 0xA0-0xFF -- "$FA_FULL"
    gen "$(fetch "$BARLOW_URL_LARGE"  BarlowCondensed-SemiBold.ttf)" custom_font_ui_36 36 -r 0x20-0x7F -r 0xB0 -r "$FA_UMLAUT" -- "$FA_LARGE"

    # The screensaver's time face: 117 px, well over three times the largest role.
    # Not a theme role -- the screensaver already abandons the theme's colours,
    # and a clock from across the room owes its legibility to one face everyone
    # gets. The range is the whole of what the time line can spell: the digits,
    # the colon, and the dashes of "--:--" before NTP has answered
    # (0x2D-0x3A is '-' through ':', with the punctuation in between). No
    # FontAwesome symbols -- an empty list breaks the converter, so it gets
    # the one code point that renders as nothing. At this size "HH:MM" still
    # leaves a margin at the panel's edges.
    gen "$(fetch "$BARLOW_URL_CLOCK"  Barlow-SemiBold.ttf)" custom_font_clock_117 117 -r 0x2D-0x3A -- "0x20"
fi

if [ "$FAMILY" = hud ] || [ "$FAMILY" = all ]; then
    gen "$(fetch "$RAJDHANI_URL_SMALL"  Rajdhani-Regular.ttf)"  custom_font_hud_16 16 -r 0x20-0x7F -r 0xA0-0xFF -- "$FA_FULL"
    gen "$(fetch "$RAJDHANI_URL_NORMAL" Rajdhani-Medium.ttf)"   custom_font_hud_22 22 -r 0x20-0x7F -r 0xA0-0xFF -- "$FA_FULL"
    gen "$(fetch "$RAJDHANI_URL_LARGE"  Rajdhani-SemiBold.ttf)" custom_font_hud_36 36 -r 0x20-0x7F -r 0xB0 -r "$FA_UMLAUT" -- "$FA_LARGE"
fi

if [ "$FAMILY" = lcars ] || [ "$FAMILY" = all ]; then
    [ -n "$LCARS_FONT" ] && [ -s "$LCARS_FONT" ] || {
        echo "LCARS_FONT must name LCARSGTJ3.ttf or LCARSGTJ3.zip (https://www.gtjlcars.de)" >&2
        exit 1
    }
    LCARS_TTF="$LCARS_FONT"
    case "$LCARS_FONT" in
        *.zip)
            LCARS_TTF="$WORK/LCARSGTJ3.ttf"
            unzip -p "$LCARS_FONT" LCARSGTJ3.ttf > "$LCARS_TTF"
            ;;
    esac

    # 19/26/43 px rather than 16/22/36: LCARS GTJ3's capitals are lower than
    # Antonio's at the same size. These sizes give the cap heights Antonio had
    # at 16/22/36 px (14/19/31 px), so the text looks as large as before and
    # is still about 12 % narrower.
    gen "$LCARS_TTF" custom_font_lcars_19 19 -r 0x20-0x7F -r 0xA0-0xFF -- "$FA_FULL"
    gen "$LCARS_TTF" custom_font_lcars_26 26 -r 0x20-0x7F -r 0xA0-0xFF -- "$FA_FULL"
    gen "$LCARS_TTF" custom_font_lcars_43 43 -r 0x20-0x7F -r 0xB0 -r "$FA_UMLAUT" -- "$FA_LARGE"
fi

echo "Done. Remember to declare new fonts in LV_FONT_CUSTOM_DECLARE (components/lvgl/lv_conf.h)."
