#!/bin/bash
#
# Regenerate the LVGL font sources in components/lvgl/fonts/.
#
# The fonts are committed, so this only has to run when a face, a size or a
# glyph range changes. Each generated .c also records its own command line in
# its header comment; this script is the same information in runnable form.
#
# Two families:
#   roboto  -- the default UI face, Roboto Regular (Apache-2.0), taken from the
#              distribution's fonts-roboto-unhinted package.
#   lcars   -- the tall condensed face the LCARS theme uses, Antonio
#              (SIL OFL 1.1), fetched from Google Fonts. Antonio ships as a
#              variable font; lv_font_conv renders its default instance, which
#              is Regular.
#
# The LV_SYMBOL_* glyphs are not in either face and are merged in from LVGL's
# own FontAwesome subset, so a theme switch cannot lose an icon.
#
# lv_font_conv is an npm tool. It is invoked through `pnpm dlx` so that nothing
# has to be installed into the tree; set LV_FONT_CONV to override, e.g. if you
# have it installed globally:
#   LV_FONT_CONV="lv_font_conv" tools/build_fonts.sh
#
# Usage: tools/build_fonts.sh [roboto|lcars|all]   (default: all)

set -euo pipefail

cd "$(dirname "$0")/.."

OUT="components/lvgl/fonts"
FA="components/lvgl/lvgl/scripts/built_in_font/FontAwesome5-Solid+Brands+Regular.woff"
ROBOTO="/usr/share/fonts/truetype/roboto/unhinted/RobotoTTF/Roboto-Regular.ttf"
ANTONIO_URL="https://raw.githubusercontent.com/google/fonts/main/ofl/antonio/Antonio%5Bwght%5D.ttf"
ANTONIO="${ANTONIO:-$(mktemp -t Antonio-XXXXXX.ttf)}"

: "${LV_FONT_CONV:=pnpm dlx lv_font_conv@1.5.3}"

# The LV_SYMBOL_* codepoints the 16 px and 22 px fonts carry. The 36 px font is
# only ever used for the big value labels and the two watermark symbols, so it
# gets just those.
FA_FULL="61441,61448,61451,61452,61453,61457,61459,61461,61465,61468,61473,61478,61479,61480,61502,61512,61515,61516,61517,61521,61522,61523,61524,61543,61544,61550,61552,61553,61556,61559,61560,61561,61563,61587,61589,61636,61637,61639,61671,61674,61683,61724,61732,61787,61931,62016,62017,62018,62019,62020,62087,62099,62189,62212,62810,63426,63650"
FA_LARGE="61550,63650"

[ -f "$FA" ] || { echo "FontAwesome subset not found: $FA (run 'git submodule update --init' once)" >&2; exit 1; }
[ -f "$ROBOTO" ] || { echo "Roboto not found: $ROBOTO (Debian/Ubuntu: fonts-roboto-unhinted)" >&2; exit 1; }

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

    echo "  $name"
    # shellcheck disable=SC2086
    $LV_FONT_CONV --format lvgl --lv-include lvgl.h --bpp 4 \
        --no-compress --no-prefilter --force-fast-kern-format \
        --size "$size" \
        --font "$face" "${ranges[@]}" \
        --font "$FA" -r "$fa" \
        --lv-font-name "$name" \
        -o "$OUT/$name.c"
}

FAMILY="${1:-all}"

echo "Generating fonts into $OUT ..."

if [ "$FAMILY" = roboto ] || [ "$FAMILY" = all ]; then
    gen "$ROBOTO" custom_font_roboto_16 16 -r 0x20-0x7F -r 0xA0-0xFF -- "$FA_FULL"
    gen "$ROBOTO" custom_font_roboto_22 22 -r 0x20-0x7F -r 0xA0-0xFF -- "$FA_FULL"
    gen "$ROBOTO" custom_font_roboto_36 36 -r 0x20-0x7F -r 0xB0      -- "$FA_LARGE"
fi

if [ "$FAMILY" = lcars ] || [ "$FAMILY" = all ]; then
    gen "$ANTONIO" custom_font_lcars_16 16 -r 0x20-0x7F -r 0xA0-0xFF -- "$FA_FULL"
    gen "$ANTONIO" custom_font_lcars_22 22 -r 0x20-0x7F -r 0xA0-0xFF -- "$FA_FULL"
    gen "$ANTONIO" custom_font_lcars_36 36 -r 0x20-0x7F -r 0xB0      -- "$FA_LARGE"
fi

echo "Done. Remember to declare new fonts in LV_FONT_CUSTOM_DECLARE (components/lvgl/lv_conf.h)."
