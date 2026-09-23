#!/bin/bash
#
# Regenerate the LVGL font sources in components/lvgl/fonts/.
#
# The fonts are committed, so this only has to run when a face, a size or a
# glyph range changes. Each generated .c also records its own command line in
# its header comment; this script is the same information in runnable form.
#
# Three families, one per theme:
#   ui      -- Material's face, Barlow (SIL OFL 1.1). A slightly condensed
#              humanist signage face with a tall x-height and open apertures,
#              which is what "legible from across the room" wants.
#   hud     -- Reticle's face, Rajdhani (SIL OFL 1.1). Flat-sided, low
#              contrast, clipped joins, and narrow near-tabular digits: at
#              36 px SemiBold five of them fit a 98 px tile where Barlow fits
#              four, which is the practical reason an instrument panel wants it.
#   lcars   -- the tall condensed face the LCARS theme uses, Antonio
#              (SIL OFL 1.1), unchanged.
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
# Antonio is variable and stays that way: LCARS wants one weight anyway.
#
# The LV_SYMBOL_* glyphs are not in either face and are merged in from LVGL's
# own FontAwesome subset, so a theme switch cannot lose an icon.
#
# lv_font_conv is an npm tool. It is invoked through `pnpm dlx` so that nothing
# has to be installed into the tree; set LV_FONT_CONV to override, e.g. if you
# have it installed globally:
#   LV_FONT_CONV="lv_font_conv" tools/build_fonts.sh
#
# Usage: tools/build_fonts.sh [ui|hud|lcars|all]   (default: all)

set -euo pipefail

cd "$(dirname "$0")/.."

OUT="components/lvgl/fonts"
FA="components/lvgl/lvgl/scripts/built_in_font/FontAwesome5-Solid+Brands+Regular.woff"
GF="https://raw.githubusercontent.com/google/fonts/main/ofl"
ANTONIO_URL="$GF/antonio/Antonio%5Bwght%5D.ttf"
ANTONIO="${ANTONIO:-$(mktemp -t Antonio-XXXXXX.ttf)}"

# Static instances, one per size role.
BARLOW_URL_SMALL="$GF/barlow/Barlow-Regular.ttf"
BARLOW_URL_NORMAL="$GF/barlow/Barlow-Medium.ttf"
BARLOW_URL_LARGE="$GF/barlow/Barlow-SemiBold.ttf"
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

# The one Latin-1 letter beyond 0x7F the 36 px faces carry: 0xE4, lowercase a
# with diaeresis, which "März" in the screensaver's German month names needs
# (ui_clock.cpp). The 16 px and 22 px faces already have all of 0xA0-0xFF;
# the large ones never did, because a numeric reading cannot contain an
# umlaut -- and the app partition is tight enough that the four glyphs of a
# full äöüß still cost more than the one that is actually spelled.
FA_UMLAUT="0xE4"

[ -f "$FA" ] || { echo "FontAwesome subset not found: $FA (run 'git submodule update --init' once)" >&2; exit 1; }
if [ ! -s "$ANTONIO" ]; then
    echo "Fetching Antonio from Google Fonts..."
    curl -sSLf -o "$ANTONIO" "$ANTONIO_URL"
fi

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
    gen "$(fetch "$BARLOW_URL_SMALL"  Barlow-Regular.ttf)"  custom_font_ui_16 16 -r 0x20-0x7F -r 0xA0-0xFF -- "$FA_FULL"
    gen "$(fetch "$BARLOW_URL_NORMAL" Barlow-Medium.ttf)"   custom_font_ui_22 22 -r 0x20-0x7F -r 0xA0-0xFF -- "$FA_FULL"
    gen "$(fetch "$BARLOW_URL_LARGE"  Barlow-SemiBold.ttf)" custom_font_ui_36 36 -r 0x20-0x7F -r 0xB0 -r "$FA_UMLAUT" -- "$FA_LARGE"

    # The screensaver's time face: 130 px, well over three times the largest role.
    # Not a theme role -- the screensaver already abandons the theme's colours,
    # and a clock from across the room owes its legibility to one face everyone
    # gets. The range is the whole of what the time line can spell: the digits,
    # the colon, and the dashes of "--:--" before NTP has answered
    # (0x2D-0x3A is '-' through ':', with the punctuation in between). No
    # FontAwesome symbols -- an empty list breaks the converter, so it gets
    # the one code point that renders as nothing. At this size "HH:MM" still
    # leaves a margin at the panel's edges.
    gen "$(fetch "$BARLOW_URL_LARGE"  Barlow-SemiBold.ttf)" custom_font_clock_130 130 -r 0x2D-0x3A -- "0x20"
fi

if [ "$FAMILY" = hud ] || [ "$FAMILY" = all ]; then
    gen "$(fetch "$RAJDHANI_URL_SMALL"  Rajdhani-Regular.ttf)"  custom_font_hud_16 16 -r 0x20-0x7F -r 0xA0-0xFF -- "$FA_FULL"
    gen "$(fetch "$RAJDHANI_URL_NORMAL" Rajdhani-Medium.ttf)"   custom_font_hud_22 22 -r 0x20-0x7F -r 0xA0-0xFF -- "$FA_FULL"
    gen "$(fetch "$RAJDHANI_URL_LARGE"  Rajdhani-SemiBold.ttf)" custom_font_hud_36 36 -r 0x20-0x7F -r 0xB0 -r "$FA_UMLAUT" -- "$FA_LARGE"
fi

if [ "$FAMILY" = lcars ] || [ "$FAMILY" = all ]; then
    gen "$ANTONIO" custom_font_lcars_16 16 -r 0x20-0x7F -r 0xA0-0xFF -- "$FA_FULL"
    gen "$ANTONIO" custom_font_lcars_22 22 -r 0x20-0x7F -r 0xA0-0xFF -- "$FA_FULL"
    gen "$ANTONIO" custom_font_lcars_36 36 -r 0x20-0x7F -r 0xB0 -r "$FA_UMLAUT" -- "$FA_LARGE"
fi

echo "Done. Remember to declare new fonts in LV_FONT_CUSTOM_DECLARE (components/lvgl/lv_conf.h)."
