#!/bin/sh
#
# Give every OHEZ_DEMO_ item a state that shows what its tile can do.
#
# An item openHAB has never been told about reads NULL, and a demo full of NULL
# demonstrates nothing. Safe to run again at any time; it puts the demo back
# into the state the pictures in README.md were taken in.
#
#   ./seed-states.sh [base-url]        default http://localhost:8080

set -u

BASE="${1:-http://localhost:8080}"
fail=0

# A state update, which is what a binding reporting a value does. Used for
# every item, read-only ones included, so the demo needs no rules.
upd()
{
    code=$(curl -s -m 5 -o /dev/null -w '%{http_code}' \
                -X PUT -H 'Content-Type: text/plain' \
                --data-binary "$2" "$BASE/rest/items/OHEZ_DEMO_$1/state")

    [ "$code" = "202" ] || [ "$code" = "200" ] || { echo "  ! OHEZ_DEMO_$1 <- '$2' : HTTP $code"; fail=$((fail + 1)); }
}

upd Light_Living    ON
upd Light_Kitchen   OFF
upd Light_Garden    OFF
upd Dimmer_Living   60
upd Color_Living    "30,80,90"
upd Scene           MOVIE

upd Heating_Set     "21.5 °C"
upd Temp_Outside    "3.5 °C"
upd Humidity        48
upd Mode            2
upd Fan             1

upd Shutter_Living  40
upd Shutter_Kitchen 0
upd Player          PAUSE
upd Volume          35
upd Away            OFF

upd LastMotion      "$(date +%Y-%m-%dT%H:%M:%S)"
upd Washer          WASH
upd FrontDoor       CLOSED
upd Motion          ON
upd Power           1240
upd ShowDetails     OFF

upd DoorOpener      OFF
upd Alarm           HOME

[ "$fail" -eq 0 ] && echo "seeded" || echo "$fail item(s) failed"
exit "$fail"
