#!/usr/bin/env python3
"""The visualizer with a fleet of its own: no broker, no panels, just
four made-up panels on two access points and two beacons walking
between them, fed into the registry the way the broker would.

    python3 mqttviz/demo/demo.py [--port 8089] [--data-dir DIR]

Everything else is the real mqttviz -- the same page, the same API, the
same state file, in a data directory of its own (by default a fresh
temporary one), so the live configuration is never touched. The house
for the space view is imported like any other:

    python3 mqttviz/demo/demo_house.py demo.sh3d
    curl --data-binary @demo.sh3d -H 'Content-Type: application/octet-stream' \\
         http://localhost:8089/api/house/import
"""

import argparse
import math
import os
import sys
import tempfile
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))

import mqttviz  # noqa: E402

AP_HALL = "a0b1c2d3e4f5"
AP_OFFICE = "a0b1c2d3e4f6"

# hostname -> access point, signal, and what the panel has to show
PANELS = {
    "panel-living": (AP_HALL, -52, {"sensor/temperature": "21.4",
                                     "sensor/humidity": "44"}),
    "panel-kitchen": (AP_HALL, -61, {"relay/1": "ON", "relay/2": "OFF"}),
    "panel-office": (AP_OFFICE, -48, {"led/red": "0", "led/green": "80",
                                      "led/blue": "20"}),
    "panel-bedroom": (AP_OFFICE, -67, {"sensor/temperature": "19.8"}),
}

# address -> name, and the distances (m) each panel hears it at, as a
# base and how far it wanders from there
BEACONS = {
    "c0ffee000001": ("keys", {"panel-living": (2.0, 1.2),
                              "panel-kitchen": (4.5, 1.5),
                              "panel-office": (6.0, 1.5),
                              "panel-bedroom": (4.0, 1.0)}),
    "c0ffee000002": ("dog", {"panel-living": (5.0, 1.5),
                             "panel-kitchen": (3.0, 1.2),
                             "panel-office": (2.5, 1.0),
                             "panel-bedroom": (5.5, 1.0)}),
}


def publish(host, suffix, value, retain=False):
    mqttviz.note_message(host, suffix, str(value), retain)


def seed():
    """What a panel publishes on connect, once."""
    for host, (bssid, rssi, extra) in PANELS.items():
        publish(host, "status", "online", True)
        publish(host, "system/name", host, True)
        publish(host, "system/target", "ArduiTouch", True)
        publish(host, "system/version", "0.20", True)
        publish(host, "system/ssid", "HomeNet", True)
        publish(host, "system/bssid", bssid, True)
        publish(host, "system/ip", "192.168.1.%d"
                % (20 + list(PANELS).index(host)), True)
        for suffix, value in extra.items():
            publish(host, suffix, value, True)


def tick(start):
    """What the panels publish every interval, and what they hear."""
    now = time.time()
    for index, (host, (bssid, rssi, _)) in enumerate(PANELS.items()):
        wobble = math.sin(now / 7 + index)
        publish(host, "system/uptime", int(now - start) + 3600 * (index + 1))
        publish(host, "system/heap", 142000 + int(4000 * wobble))
        publish(host, "system/fps", "%.1f" % (28 + 2 * wobble))
        publish(host, "system/rssi", rssi + int(3 * wobble))
        publish(host, "system/quality", max(0, min(100, 2 * (rssi + 100))))
        publish(host, "ui/night", "OFF")
        publish(host, "ui/backlight", "ON")
        publish(host, "ui/activity", "ON" if index == 1 else "OFF")
        publish(host, "ui/brightness", 80 if index != 3 else 35)

    for index, (addr, (name, heard)) in enumerate(BEACONS.items()):
        for host, (base, wander) in heard.items():
            distance = base + wander * math.sin(now / 9 + index * 2
                                                + len(host))
            publish(host, "ble/%s/name" % addr, name, True)
            publish(host, "ble/%s/type" % addr, "ibeacon", True)
            publish(host, "ble/%s/rssi" % addr,
                    int(-55 - 8 * max(0.5, distance)))
            publish(host, "ble/%s/distance" % addr,
                    "%.2f" % max(0.5, distance))


def feed():
    time.sleep(0.5)                   # the server's state exists by now
    start = time.time()
    seed()
    while True:
        tick(start)
        time.sleep(2)


def main():
    parser = argparse.ArgumentParser(description="mqttviz with a demo fleet")
    parser.add_argument("--port", type=int, default=mqttviz.DEFAULT_PORT)
    parser.add_argument("--data-dir", metavar="DIR",
                        help="where the demo keeps its state (default: a"
                             " fresh temporary directory)")
    args = parser.parse_args()

    data_dir = args.data_dir or tempfile.mkdtemp(prefix="mqttviz-demo-")
    print("mqttviz demo: http://localhost:%d  (state in %s)"
          % (args.port, data_dir), flush=True)

    threading.Thread(target=feed, daemon=True).start()
    sys.argv = ["mqttviz.py", "--no-mqtt", "--port", str(args.port),
                "--data-dir", data_dir]
    mqttviz.main()


if __name__ == "__main__":
    main()
