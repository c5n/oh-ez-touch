#!/bin/sh
#
# Take every picture in README.md, from the simulator, against a real openHAB
# with the demo installed.
#
#   ./make-screenshots.sh [base-url] [simulator]
#
#   base-url   the openHAB server, default http://localhost:8080
#   simulator  the simulator binary, default build/linux/oh-ez-touch.elf
#
# Run from anywhere; the pictures land in img/ beside this script. The
# simulator runs headless with a configuration of its own, so a simulator
# already running elsewhere and its settings are left alone. The demo items are
# seeded first and seeded again at the end, so the server is left the way
# seed-states.sh leaves it.

set -eu

HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
BASE="${1:-http://localhost:8080}"
SIM="${2:-$ROOT/build/linux/oh-ez-touch.elf}"
IMG="$HERE/img"
PORT=18791
WEBPORT=18790

HOSTPORT=${BASE#*://}
OH_HOST=${HOSTPORT%%:*}
OH_PORT=${HOSTPORT##*:}
[ "$OH_PORT" = "$HOSTPORT" ] && OH_PORT=8080

WORK=$(mktemp -d)
SIM_PID=

cleanup()
{
    [ -n "$SIM_PID" ] && kill "$SIM_PID" 2>/dev/null || true
    rm -rf "$WORK"
}
trap cleanup EXIT INT TERM

ctl() { python3 "$ROOT/tools/ohez_ctl.py" --port "$PORT" --web-port "$WEBPORT" "$@"; }

# The field of the screen dump named by a Python expression on `d`.
screen() { ctl screen | python3 -c "import json,sys; d=json.load(sys.stdin); print($1)"; }

upd()
{
    curl -s -m 5 -o /dev/null -X PUT -H 'Content-Type: text/plain' \
         --data-binary "$2" "$BASE/rest/items/OHEZ_DEMO_$1/state"
}

shot()
{
    # Long enough for icons to arrive and a pushed change to be drawn.
    sleep "${2:-2}"
    ctl shot "$IMG/$1.png" --scale 2 >/dev/null
    echo "  $1.png"
}

# Until the screen dump says `screen` is $1, for at most $2 seconds.
wait_screen()
{
    i=0
    while [ "$(screen "d['screen']")" != "$1" ]; do
        i=$((i + 1))
        [ "$i" -gt $(( ${2:-10} * 4 )) ] && { echo "no $1 screen" >&2; exit 1; }
        sleep 0.25
    done
}

home()
{
    while [ "$(screen "any(t['type'] == 'parent_link' for t in d['page']['tiles'])")" = "True" ]; do
        ctl tap-tile 0 >/dev/null
        ctl wait-page >/dev/null
    done
}

open_page() { home; ctl tap-label "$1" >/dev/null; ctl wait-page >/dev/null; }

# Open a tile's control screen, photograph it, and leave by the back bar.
item_shot()
{
    ctl tap-label "$1" >/dev/null
    wait_screen item
    shot "$2"
    ctl tap 20 20 >/dev/null
    wait_screen page
}

mkdir -p "$IMG"
"$HERE/seed-states.sh" "$BASE" >/dev/null

OHEZ_CONFIG_DIR="$WORK/cfg" OHEZ_STATE_DIR="$WORK/state" \
OHEZ_TESTIF_PORT=$PORT OHEZ_WEBUI_PORT=$WEBPORT \
SDL_VIDEODRIVER=dummy SDL_RENDER_DRIVER=software \
    "$SIM" >"$WORK/sim.log" 2>&1 &
SIM_PID=$!

i=0
until ctl ping >/dev/null 2>&1; do
    i=$((i + 1))
    [ "$i" -gt 40 ] && { echo "the simulator did not come up; see $WORK/sim.log" >&2; exit 1; }
    sleep 0.25
done

ctl set oh_host "$OH_HOST" >/dev/null
ctl set oh_port "$OH_PORT" >/dev/null
ctl set oh_sitemap ohezdemo >/dev/null
ctl wait-page >/dev/null

echo "home and clock"
shot home 4

ctl set bl_clock 1 >/dev/null
ctl set bl_timeout 3 >/dev/null
# The timeout is counted from the last touch: one in the gap between two tiles.
ctl tap 108 135 >/dev/null
wait_screen clock 20
shot clock 4
ctl set bl_timeout 0 >/dev/null
ctl tap 160 120 >/dev/null
wait_screen page 20

echo "basics"
open_page Basics
shot basics
item_shot Dimmer      slider
item_shot Heating     setpoint
item_shot Scene       selection
item_shot "Ambient Colour" colorpicker

echo "media"
open_page Media
shot media
item_shot "Living Blinds" rollershutter
item_shot Music           player

echo "groups"
open_page Basics
ctl tap-label Lights >/dev/null
ctl wait-page >/dev/null
shot lights

echo "door"
open_page Door
shot door
item_shot Camera doorbell
# The ring, from another page: the panel follows the ring item wherever it is.
ctl set bell_ring OHEZ_DEMO_Doorbell_Ring >/dev/null
ctl set bell_image OHEZ_DEMO_Doorbell_Snapshot >/dev/null
home
sleep 2
upd Doorbell_Ring ON
wait_screen item 10
shot doorbell-ring
ctl tap 20 20 >/dev/null
wait_screen page
upd Doorbell_Ring OFF
# Quoted for the panel's tokeniser: `set` wants a value, and "" is an empty one.
ctl send 'set bell_ring ""' >/dev/null
ctl send 'set bell_image ""' >/dev/null

echo "values"
open_page Values
shot values-cold
upd Temp_Outside "28.5 °C"
upd Washer DONE
shot values-warm 3
item_shot Ventilation command-options
upd Temp_Outside "3.5 °C"
upd Washer WASH

echo "rules"
open_page Rules
shot rules-hidden
upd ShowDetails ON
upd Heating_Set "24 °C"
shot rules-shown 4
upd ShowDetails OFF
upd Heating_Set "21.5 °C"
sleep 3

echo "secure"
ctl send pin item set 1234 >/dev/null
open_page Secure
shot secure
ctl tap-label "Door Opener" >/dev/null
sleep 1
shot pin-pad
# Cancel the pad by its own bar, then forget the PIN again.
ctl tap $(screen "' '.join(str(v) for v in (d['pin']['keys']['x'][0] + d['pin']['keys']['x'][2] // 2, d['pin']['keys']['x'][1] + d['pin']['keys']['x'][3] // 2))") >/dev/null
ctl send pin item clear >/dev/null

echo "themes"
home
for theme in Material Classic LCARS JARVIS; do
    ctl set theme "$theme" >/dev/null
    ctl wait-page >/dev/null
    shot "theme-$(echo "$theme" | tr 'A-Z' 'a-z')" 3
done
ctl set theme Material >/dev/null

"$HERE/seed-states.sh" "$BASE" >/dev/null
echo "done: $IMG"
