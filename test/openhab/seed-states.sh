#!/bin/sh
#
# Give every fixture item a state worth looking at.
#
# A file-defined item openHAB has never been told about reads "NULL", and a
# panel full of NULL tells you nothing about how the panel draws. This puts a
# plausible value on each one, so that a fresh server plus the .items and
# .sitemap files beside this script reproduce the screens the tests and
# doc/openhab-fixtures.md describe.
#
#   ./seed-states.sh [base-url]        default http://localhost:8080
#
# Safe to run repeatedly, and safe to run against a server that only has some
# of the three fixtures installed: an item that does not exist answers 404 and
# is reported, not fatal.

set -u

BASE="${1:-http://localhost:8080}"
fail=0

# A command, which is what a binding or a UI would send. openHAB applies the
# item's own unit and formatting to it.
cmd()
{
    code=$(curl -s -m 5 -o /dev/null -w '%{http_code}' \
                -X POST -H 'Content-Type: text/plain' \
                --data-binary "$2" "$BASE/rest/items/$1")

    [ "$code" = "200" ] || { echo "  ! $1 <- '$2' : HTTP $code"; fail=$((fail + 1)); }
}

# A state update. Contact is read-only to commands, and DateTime and Location
# have no command form worth using, so those three are set directly.
upd()
{
    code=$(curl -s -m 5 -o /dev/null -w '%{http_code}' \
                -X PUT -H 'Content-Type: text/plain' \
                --data-binary "$2" "$BASE/rest/items/$1/state")

    [ "$code" = "202" ] || { echo "  ! $1 <- '$2' : HTTP $code"; fail=$((fail + 1)); }
}

echo "seeding $BASE"

# ---------------------------------------------------------------- demo.items
echo "  demo"
cmd OHEZTOUCH_Switch        ON
cmd OHEZTOUCH_Select        SEL1
cmd OHEZTOUCH_Player        PAUSE
cmd OHEZTOUCH_Rollershutter 30
cmd OHEZTOUCH_Number        10
cmd OHEZTOUCH_Color         "50,77,70"
cmd OHEZTOUCH_String        "plain text"

# ------------------------------------------------------ oheztouch_gaps.items
echo "  gaps"
cmd OHEZTOUCH_Temp     "21.5 °C"
cmd OHEZTOUCH_Humidity "48 %"      # stored as the ratio 0.48 -- see the doc
cmd OHEZTOUCH_Percent  48
cmd OHEZTOUCH_Dimmer   75
cmd OHEZTOUCH_Light1   ON
cmd OHEZTOUCH_Light2   OFF
cmd OHEZTOUCH_Text     "Hello World"

# ------------------------------------------------------------- oheznav.items
echo "  nav"
cmd NAV_Light_Ceiling ON
cmd NAV_Light_Desk    OFF
cmd NAV_Light_Kitchen ON
cmd NAV_Dimmer_Floor  60
cmd NAV_Color_Strip   "270,65,80"
cmd NAV_Temp_Living   "21.4 °C"
cmd NAV_Temp_Bed      "18.9 °C"
cmd NAV_Setpoint      "22.0 °C"
cmd NAV_Humidity      47
cmd NAV_Power_Fridge  "85 W"
cmd NAV_Power_Oven    "1250 W"
cmd NAV_Blind_Living  30
cmd NAV_Blind_Bed     100
cmd NAV_Player        PAUSE
cmd NAV_Channel       C03
cmd NAV_Volume        35
cmd NAV_Mode          READ
cmd NAV_Umlaut        "Süd"
cmd NAV_LongLabel     "value"
cmd NAV_Spaced        "Hello World"
upd NAV_Door_Front    CLOSED
upd NAV_Last_Motion   "2026-09-15T13:45:00.000+0200"
upd NAV_Car           "52.5200,13.4050"

# NAV_Unset is deliberately never set. It is the fixture's uninitialised item,
# and the tile that shows what a panel does with openHAB's "NULL".

if [ "$fail" -eq 0 ]; then
    echo "all items seeded"
else
    echo "$fail item(s) could not be set -- are all three fixtures installed?"
fi

exit "$fail"
