#!/bin/sh
#
# Take every picture in doc/mqttviz.md, from the demo fleet and the demo
# house, in a headless Chromium.
#
#   mqttviz/demo/make-screenshots.sh [chromium]
#
# Needs Python 3, Node 22 or newer, and a Chromium (default: the first of
# chromium, chromium-browser, google-chrome on the PATH). Run from
# anywhere; the pictures land in doc/img/mqttviz_*.png. The demo runs on a
# port and in a data directory of its own, so a visualizer already
# running and its configuration are left alone.

set -eu

HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
IMG="$ROOT/doc/img"
PORT=18091

CHROME=${1:-}
if [ -z "$CHROME" ]; then
    for candidate in chromium chromium-browser google-chrome; do
        if command -v "$candidate" >/dev/null 2>&1; then
            CHROME=$candidate
            break
        fi
    done
fi
[ -n "$CHROME" ] || { echo "no Chromium found; name one" >&2; exit 1; }

WORK=$(mktemp -d)
DEMO_PID=

cleanup()
{
    [ -n "$DEMO_PID" ] && kill "$DEMO_PID" 2>/dev/null || true
    rm -rf "$WORK"
}
trap cleanup EXIT INT TERM

python3 "$HERE/demo_house.py" "$WORK/demo.sh3d"
python3 "$HERE/demo.py" --port "$PORT" --data-dir "$WORK/data" \
    >"$WORK/demo.log" 2>&1 &
DEMO_PID=$!

i=0
until curl -sf "http://localhost:$PORT/api/state" >/dev/null; do
    i=$((i + 1))
    [ "$i" -lt 50 ] || { cat "$WORK/demo.log" >&2; exit 1; }
    sleep 0.2
done
# a few intervals of the fleet, so every box has its values
sleep 5

mkdir -p "$IMG"
echo "pictures into $IMG:"
node "$HERE/screenshots.mjs" "http://localhost:$PORT" "$WORK/demo.sh3d" \
    "$IMG" "$CHROME"
