#!/usr/bin/env python3
"""The OhEzTouch MQTT visualizer: a canvas view of the panels on the broker.

One Python script, one page on localhost, nothing but Python 3 installed --
the same rules the device manager (devmgr/) plays by. It subscribes to the
broker with a single wildcard and turns every topic under
<base topic>/<hostname>/ into one box on the page: a live picture of what
each panel is publishing, and -- where the panel has relays, LEDs or sounds
-- a remote control for them.

    python3 mqttviz/mqttviz.py            # then open http://localhost:8089

Everything the visualizer persists lives in mqttviz/data/mqttviz.json (kept
out of the repository, because it contains your broker password): the
connection settings, and every device it has ever seen. A panel that goes
dark is greyed out with its last-seen time, not forgotten.

    python3 mqttviz/mqttviz.py --mqtt-host localhost --base oheztouch
"""

import argparse
import json
import os
import re
import sys
import threading
import time
import urllib.parse
from collections import deque
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

import mqtt_client

HERE = os.path.dirname(os.path.abspath(__file__))
DATA_DIR = os.path.join(HERE, "data")
STATE_FILE = os.path.join(DATA_DIR, "mqttviz.json")
LAYOUT_FILE = os.path.join(DATA_DIR, "layouts.json")
WEB_DIR = os.path.join(HERE, "web")

DEFAULT_PORT = 8089

LOG_CAPACITY = 500
HISTORY_CAP = 60
LEVELS = ("debug", "info", "warn", "error")

# What the page is allowed to publish: exactly the topics the firmware
# subscribes to (doc/mqtt.md). Nothing else goes out, whatever the page asks.
PUBLISHABLE = re.compile(r"^(?:relay/[^/]+|led/[^/]+|sound|config/[^/]+)"
                         r"/set$")
PAYLOAD_MAX = 200
SECRET_MASK = "***"

# One topic per BLE advertiser, keyed by its hardware address: lowercase
# hex, no separators (doc/ble.md). Stale advertisers are cleared with an
# empty retained publish -- the absence is information too.
BLE_TOPIC = re.compile(r"^ble/([0-9a-f]{12})/([^/]+)$")

ON_VALUES = ("online", "on", "true", "yes", "1")


# The physics sliders, as the page sends them: whole numbers in their
# ranges, mapped to real constants in the page. These defaults are the
# thick oil the canvas ships with; the values exist server-side only so
# the choice survives a restart, like everything else the page changes.
# Gravity is the one common slider that reaches below zero -- buoyancy is
# a setting too. The beacons keep the sliders with units of their own:
# the line timeout, the one measured in seconds, the minimum signal, the
# one measured in dBm, and the metre scale, the one measured in pixels.
PHYS_DEFAULTS = {
    "drag": 60,
    "tether": 30,
    "length": 50,
    "homing": 35,
    "convection": 50,
    "convection_speed": 50,
    "shove": 16,
    "wall": 30,
    "sag": 50,
    "gravity": 0,
    "signal_pull": 100,
    "line_timeout": 90,
    "min_signal": -100,
    "metre_px": 60,
}

PHYS_RANGES = {
    "gravity": (-100, 100),
    "line_timeout": (30, 300),
    "min_signal": (-100, -30),
    "metre_px": (10, 200),
}

# The kinds of things in the picture, each with its own physics: the
# panels, the WLAN access points of the topology view, the beacons, and
# the broker itself -- the hub never moves, but its gravity is a setting
# like any other object's.
PHYS_KINDS = ("phys_nodes", "phys_aps", "phys_beacons", "phys_broker")

# What a stored position may be keyed by: the broker, an access point by
# its BSSID, or a panel by its hostname. The page owns the keys; this
# end only bounds them.
POSITION_KEY_MAX = 80

VIEW_MODES = ("mesh", "topology")

# One saved layout, whatever the page named it: short enough to be a
# name, never empty, never a filename nobody asked for.
LAYOUT_NAME_MAX = 40

# The topology view's camera: how much of the workspace it shows, and
# how close it may be brought to the facts. The minimum is the whole
# workspace at once.
CAM_ZOOM_MIN = 0.5
CAM_ZOOM_MAX = 2.5


def normalise_position_key(key):
    """One stored-position key, whatever the page said: a short non-empty
    string, or nothing."""
    key = str(key or "").strip()
    if not key or len(key) > POSITION_KEY_MAX or "/" in key:
        return None
    return key


def normalise_position(raw):
    """One stored position, whatever the page said: x and y as fractions
    of the canvas, whole enough to survive the round trip."""
    if not isinstance(raw, dict):
        return None
    try:
        x = max(0.0, min(1.0, float(raw.get("x"))))
        y = max(0.0, min(1.0, float(raw.get("y"))))
    except (TypeError, ValueError):
        return None
    return {"x": round(x, 4), "y": round(y, 4)}


def normalise_positions(raw):
    """The whole stored-position table: every key a valid key, every
    entry a valid position, everything else dropped."""
    positions = {}
    if isinstance(raw, dict):
        for key, value in raw.items():
            key = normalise_position_key(key)
            pos = normalise_position(value)
            if key and pos:
                positions[key] = pos
    return positions


def normalise_layout_name(name):
    """One name for a saved arrangement, whatever the page said: a
    short non-empty string."""
    name = str(name or "").strip()
    if not name or len(name) > LAYOUT_NAME_MAX:
        return None
    return name


def normalise_layout(raw):
    """One saved layout: the places the topology view keeps, the
    picture they belong to, and when they were kept."""
    if not isinstance(raw, dict):
        return None
    view_mode = raw.get("view_mode")
    return {
        "positions": normalise_positions(raw.get("positions")),
        "view_mode": view_mode if view_mode in VIEW_MODES else "topology",
        "saved_at": str(raw.get("saved_at") or ""),
    }


def normalise_layouts(raw):
    """The whole saved-layout table: every name a valid name, every
    entry a valid layout, everything else dropped."""
    layouts = {}
    if isinstance(raw, dict):
        for name, value in raw.items():
            name = normalise_layout_name(name)
            layout = normalise_layout(value)
            if name and layout:
                layouts[name] = layout
    return layouts


def normalise_cam(raw):
    """The topology view's camera, whatever the file or the page said:
    how much of the workspace it shows and which point of the
    workspace it is centred on -- fractions, so the picture survives a
    resize."""
    cam = {"zoom": 1.0, "x": 0.5, "y": 0.5}
    if isinstance(raw, dict):
        try:
            cam["zoom"] = max(CAM_ZOOM_MIN,
                              min(CAM_ZOOM_MAX, float(raw.get("zoom", 1.0))))
        except (TypeError, ValueError):
            pass
        for axis in ("x", "y"):
            try:
                cam[axis] = max(0.0, min(1.0, float(raw.get(axis, 0.5))))
            except (TypeError, ValueError):
                pass
    cam["zoom"] = round(cam["zoom"], 3)
    cam["x"] = round(cam["x"], 4)
    cam["y"] = round(cam["y"], 4)
    return cam


def positions_into_workspace(positions):
    """Positions were fractions of the canvas once; the topology view
    lays out in a workspace of four times that area now, with the
    canvas's picture in its middle. A position from before the
    workspace keeps its place on the eye by moving into that middle --
    and every pin lands exactly where it was left."""
    return {key: {"x": round(0.25 + 0.5 * pos["x"], 4),
                  "y": round(0.25 + 0.5 * pos["y"], 4)}
            for key, pos in positions.items()}


def normalise_phys(raw):
    """One set of physics sliders, whatever the file or the page said:
    whole numbers, each within its range, missing ones at their
    defaults."""
    phys = dict(PHYS_DEFAULTS)
    if isinstance(raw, dict):
        for key, default in PHYS_DEFAULTS.items():
            low, high = PHYS_RANGES.get(key, (0, 100))
            try:
                value = int(raw.get(key, default))
            except (TypeError, ValueError):
                value = default
            phys[key] = max(low, min(high, value))
    return phys


def topic_sanitise(base):
    """The same rules the firmware applies to its own base topic: no
    wildcards, no empty levels, and never the empty string."""
    parts = [part for part in str(base).split("/") if part]
    cleaned = [part for part in parts if part not in ("+", "#")]
    return "/".join(cleaned) or "oheztouch"


# --------------------------------------------------------------------- state

# One broker profile. The passwords live in the settings file, which git
# keeps out; the state API masks them the way devmgr masks its secrets.
DEFAULT_BROKER = {
    "name": "default",
    "mqtt_host": "localhost",
    "mqtt_port": 1883,
    "mqtt_user": "",
    "mqtt_pass": "",
    "base_topic": "oheztouch",
}

BROKER_KEYS = ("mqtt_host", "mqtt_port", "mqtt_user", "mqtt_pass",
               "base_topic")


def normalise_broker(profile):
    """One profile entry, whatever the file or the page said, into the
    shape the rest of the tool reads."""
    out = dict(DEFAULT_BROKER)

    # A stable id is what follows a profile through renames and reorders:
    # the page sends masks for unchanged passwords, and the mask is
    # "leave the one with this id alone" -- by name would fail the moment
    # the name is the very thing being edited.
    out["id"] = str(profile.get("id") or "").strip()[:32] or os.urandom(4).hex()

    out["name"] = str(profile.get("name") or "").strip()[:40] or "broker"
    host = str(profile.get("mqtt_host") or "").strip()
    out["mqtt_host"] = host if host and "/" not in host else "localhost"

    try:
        port = int(profile.get("mqtt_port") or DEFAULT_BROKER["mqtt_port"])
    except (TypeError, ValueError):
        port = DEFAULT_BROKER["mqtt_port"]
    out["mqtt_port"] = port if 1 <= port <= 65535 \
        else DEFAULT_BROKER["mqtt_port"]

    out["mqtt_user"] = str(profile.get("mqtt_user") or "").strip()
    out["mqtt_pass"] = str(profile.get("mqtt_pass") or "")
    out["base_topic"] = topic_sanitise(profile.get("base_topic")
                                       or DEFAULT_BROKER["base_topic"])
    return out


def sort_brokers(brokers, follow_id=None, follow_name=None):
    """The list is kept alphabetical -- it is a list of places, and
    places are looked up by name. The entry the tool is connected to
    follows its profile wherever the sort puts it: by id, by name, and
    only as a last resort back to the first. Returns (sorted, index)."""
    ordered = sorted(brokers, key=lambda profile: profile["name"].lower())

    index = None
    if follow_id:
        index = next((i for i, profile in enumerate(ordered)
                      if profile.get("id") == follow_id), None)
    if index is None and follow_name:
        index = next((i for i, profile in enumerate(ordered)
                      if profile["name"] == follow_name), None)
    return ordered, 0 if index is None else index


def active_broker():
    """The profile the tool is connected to, or trying to: (index, copy)."""
    with STATE.lock:
        brokers = STATE.settings.get("brokers") or [dict(DEFAULT_BROKER)]
        index = STATE.settings.get("broker_index") or 0
        if not 0 <= index < len(brokers):
            index = 0
        return index, dict(brokers[index])


def connection_snapshot():
    """Everything a broker connection depends on, in one comparable tuple:
    a restart is only worth its reconnect-backoff when this changed. The
    profile's index is not in it -- a save that reorders or renames the
    list while keeping the same connection to the same profile is no
    reason to disconnect."""
    _, broker = active_broker()
    with STATE.lock:
        enabled = bool(STATE.settings.get("mqtt_enabled"))
    return (enabled, broker["mqtt_host"], broker["mqtt_port"],
            broker["mqtt_user"], broker["mqtt_pass"], broker["base_topic"])


class State:
    """The settings, the device registry and the console log, one lock over
    all of it: the broker thread, the saver and the HTTP handlers all write
    here."""

    DEFAULT_SETTINGS = {
        "mqtt_enabled": True,
        "brokers": [dict(DEFAULT_BROKER)],
        "broker_index": 0,
        "log_level": "info",
        "verbose": False,
        "show_broker": True,
        "show_beacons": True,
        "show_fx": True,
        "view_mode": "mesh",
        "lang": "en",
        "positions": {},
        "pos_space": "world",
        "layouts": {},
        "cam": {"zoom": 1.0, "x": 0.5, "y": 0.5},
        "phys_nodes": dict(PHYS_DEFAULTS),
        "phys_aps": dict(PHYS_DEFAULTS),
        "phys_beacons": dict(PHYS_DEFAULTS),
        "phys_broker": dict(PHYS_DEFAULTS),
    }

    def __init__(self):
        self.lock = threading.RLock()
        self.settings = dict(self.DEFAULT_SETTINGS)
        self.settings["brokers"] = [dict(DEFAULT_BROKER)]
        self.settings["phys_nodes"] = dict(PHYS_DEFAULTS)
        self.settings["phys_aps"] = dict(PHYS_DEFAULTS)
        self.settings["phys_beacons"] = dict(PHYS_DEFAULTS)
        self.settings["phys_broker"] = dict(PHYS_DEFAULTS)
        self.devices = {}          # hostname -> device dict
        self.beacons = {}          # address -> beacon dict, memory only
        self.log_entries = deque(maxlen=LOG_CAPACITY)
        self.log_seq = 0
        self._dirty = False
        # What the canvas looked like right before the last layout was
        # restored -- memory only, written to the layout backup file so
        # trying a layout on costs nothing that cannot be taken back.
        self.before_load = None
        self._load()

    def _load(self):
        try:
            with open(STATE_FILE) as handle:
                doc = json.load(handle)
        except (OSError, ValueError):
            return

        if isinstance(doc.get("settings"), dict):
            for key in self.DEFAULT_SETTINGS:
                if key in doc["settings"]:
                    self.settings[key] = doc["settings"][key]

            # A settings file from before the profile list kept the
            # connection flat: host, port and credentials as single
            # settings. They become the one profile of the new shape, so
            # a switch to the new version moves nothing.
            flat = {key: doc["settings"][key] for key in BROKER_KEYS
                    if key in doc["settings"]}

            if flat and not isinstance(doc["settings"].get("brokers"), list):
                self.settings["brokers"] = [normalise_broker(
                    {"name": "default", **flat})]
                self.settings["broker_index"] = 0

        # Whatever the file said a profile list is, it is one now.
        if not isinstance(self.settings.get("brokers"), list):
            self.settings["brokers"] = [dict(DEFAULT_BROKER)]

        old_index = self.settings.get("broker_index") or 0
        profiles = [normalise_broker(profile)
                     for profile in self.settings["brokers"]]

        # A file that arrived with an empty list gets the one true
        # profile; the rest of the tool never has to defend against none.
        if not profiles:
            profiles = [normalise_broker(dict(DEFAULT_BROKER))]

        # The list is kept alphabetical, and the entry the tool was
        # connected to keeps its place in the connection through it.
        old = profiles[old_index] if 0 <= old_index < len(profiles) else None
        self.settings["brokers"], self.settings["broker_index"] = sort_brokers(
            profiles, follow_id=old and old.get("id"),
            follow_name=old and old["name"])

        # Whatever the file said the sliders were, they are sliders now:
        # every one whole, in range, and missing ones at their defaults.
        # A file from before the two halves were set apart kept one set
        # under "phys"; it becomes all of them, so nothing on the canvas
        # moves the day after the upgrade.
        file_settings = doc.get("settings") if isinstance(
            doc.get("settings"), dict) else {}
        legacy = file_settings.get("phys")
        phys_keys = ("phys_nodes", "phys_aps", "phys_beacons", "phys_broker")
        if isinstance(legacy, dict) and not any(
                k in file_settings for k in phys_keys):
            for key in phys_keys:
                self.settings[key] = normalise_phys(legacy)
        else:
            for key in phys_keys:
                self.settings[key] = normalise_phys(self.settings.get(key))

        # Which picture the canvas draws: the classic mesh, or the
        # topology of broker, access points and panels.
        if self.settings.get("view_mode") not in VIEW_MODES:
            self.settings["view_mode"] = "mesh"

        # The pinned positions of the topology view: fractions of the
        # canvas, every entry checked the way it is checked when the
        # page sends one.
        self.settings["positions"] = normalise_positions(
            self.settings.get("positions"))

        # The saved arrangements: named snapshots of the same, checked
        # the same way, so a file that arrives from anywhere becomes a
        # table the rest of the tool can trust.
        self.settings["layouts"] = normalise_layouts(
            self.settings.get("layouts"))

        # Positions were fractions of the canvas once; the topology view
        # lays out in a workspace of four times that area now, with the
        # canvas's picture in its middle. A file from before the
        # workspace keeps its places by moving them into that middle:
        # one conversion, at load time, and every pin -- and every
        # layout's pins -- lands exactly where the eye left it.
        raw_settings = doc.get("settings") if isinstance(
            doc.get("settings"), dict) else {}
        if raw_settings.get("pos_space") != "world":
            self.settings["positions"] = positions_into_workspace(
                self.settings["positions"])
            self.settings["layouts"] = {
                name: {"positions": positions_into_workspace(
                           layout["positions"]),
                       "view_mode": layout["view_mode"],
                       "saved_at": layout["saved_at"]}
                for name, layout in self.settings["layouts"].items()}
            self.settings["pos_space"] = "world"
            self.save()

        # Whatever the file said the camera was, it is fractions in
        # range now -- the workspace is as big as it is, and the camera
        # looks somewhere inside it.
        self.settings["cam"] = normalise_cam(self.settings.get("cam"))

        # The language of the page: English or German, kept with the rest
        # so every browser that opens the tool starts right.
        if self.settings.get("lang") not in ("en", "de"):
            self.settings["lang"] = "en"

        if isinstance(doc.get("devices"), dict):
            self.devices = doc["devices"]

        # Nothing is online at startup: the broker's retained messages
        # replay the moment the connection is up, and they decide.
        for device in self.devices.values():
            device["online"] = False

    def save(self):
        with self.lock:
            self._dirty = True

    def save_now_if_dirty(self):
        with self.lock:
            if not self._dirty:
                return
            self._dirty = False

            # Histories are a rolling picture of the last minutes; they are
            # re-learned from the broker and would only bloat the file.
            doc = {
                "settings": self.settings,
                "devices": {
                    host: {key: value for key, value in device.items()
                           if key != "history"}
                    for host, device in self.devices.items()
                },
            }

        os.makedirs(DATA_DIR, exist_ok=True)

        tmp = STATE_FILE + ".tmp"
        with open(tmp, "w") as handle:
            json.dump(doc, handle, indent=2)
            handle.write("\n")

        # The previous good state is kept one generation back: whatever
        # a corrupted write, a full disk or a mistaken rm does to the
        # live file, the last thing that was true is still beside it.
        if os.path.exists(STATE_FILE):
            os.replace(STATE_FILE, STATE_FILE + ".bak")
        os.replace(tmp, STATE_FILE)

    def log(self, level, source, message):
        if LEVELS.index(level) < LEVELS.index(self.settings.get("log_level",
                                                                "info")):
            return

        with self.lock:
            self.log_seq += 1
            self.log_entries.append({
                "seq": self.log_seq,
                "ts": time.strftime("%H:%M:%S"),
                "level": level,
                "source": source,
                "message": message,
            })

    def log_since(self, since, min_level="debug"):
        with self.lock:
            return [entry for entry in self.log_entries
                    if entry["seq"] > since
                    and LEVELS.index(entry["level"]) >= LEVELS.index(min_level)]


# The state itself is created in main(), once the --data-dir option has
# had its say: everything below refers to STATE as it runs, never as the
# module loads.
STATE = None


# ------------------------------------------------------- layout backup file

def layout_snapshot(positions, view_mode):
    """A copy of one arrangement the way the backup file tells it: no
    reference into the live table, so a later change cannot reach into
    the past."""
    return {
        "view_mode": view_mode if view_mode in VIEW_MODES else "mesh",
        "positions": {key: dict(pos)
                      for key, pos in (positions or {}).items()},
    }


def write_layout_backup():
    """The whole arrangement situation in one readable file beside the
    state: the live places as they stand, every layout the page has
    saved, and -- once a layout has been restored -- what the canvas
    looked like right before that. Whatever a later write corrupts in
    the state file, the places found by hand are still here, human
    readable, to be typed back in or imported whole. Written atomically
    like the state, and never fatal to the request that asked for it:
    a backup that cannot be written is a warning, not a lost
    arrangement."""
    with STATE.lock:
        doc = {
            "written": time.strftime("%Y-%m-%d %H:%M:%S"),
            "current": layout_snapshot(STATE.settings.get("positions"),
                                       STATE.settings.get("view_mode")),
            "layouts": {name: {"positions": dict(layout["positions"]),
                               "view_mode": layout["view_mode"],
                               "saved_at": layout["saved_at"]}
                        for name, layout
                        in (STATE.settings.get("layouts") or {}).items()},
        }
        if STATE.before_load:
            doc["before_load"] = dict(STATE.before_load)

    try:
        os.makedirs(DATA_DIR, exist_ok=True)
        tmp = LAYOUT_FILE + ".tmp"
        with open(tmp, "w") as handle:
            json.dump(doc, handle, indent=2)
            handle.write("\n")
        os.replace(tmp, LAYOUT_FILE)
    except OSError as error:
        STATE.log("warn", "backup", "layout backup not written: %s" % error)


# ----------------------------------------------------------- device registry

def device_value(device, suffix):
    record = device.get("topics", {}).get(suffix)
    return None if record is None else record.get("value")


def note_message(host, suffix, value, retain):
    """One MQTT message into the registry: a new device, a status
    transition, a history point, or just a value that changed."""
    with STATE.lock:
        device = STATE.devices.get(host)

        if device is None:
            device = {
                "host": host,
                "first_seen": time.strftime("%Y-%m-%d %H:%M:%S"),
                "online": False,
                "topics": {},
                "history": {},
                "msgcount": 0,
            }
            STATE.devices[host] = device
            STATE.log("info", "device", "new device: %s" % host)
            STATE.save()

        now = time.time()
        was_online = device.get("online", False)

        device["topics"][suffix] = {"value": value, "ts": now,
                                    "retain": bool(retain)}
        device["msgcount"] = device.get("msgcount", 0) + 1
        device["last_ts"] = now
        device["last_seen"] = time.strftime("%Y-%m-%d %H:%M:%S")

        if suffix == "status":
            device["online"] = value.lower() in ON_VALUES
            if device["online"] and not was_online:
                STATE.log("info", "device", "%s is online" % host)
            elif not device["online"] and was_online:
                STATE.log("warn", "device", "%s went offline" % host)

        # A numeric value is a point on a sparkline; the page keeps the
        # short view and asks for more detail one box at a time.
        try:
            history = device.setdefault("history", {}).setdefault(suffix, [])
            history.append([round(now, 3), float(value)])
            if len(history) > HISTORY_CAP:
                del history[:-HISTORY_CAP]
        except (TypeError, ValueError):
            pass

        if STATE.settings.get("verbose"):
            STATE.log("debug", "mqtt",
                      "%s/%s = %s" % (host, suffix, value[:80]))

        STATE.save()

    # The BLE subtree is double-booked: the raw topics stay with the
    # device (its detail view lists them), and the beacons themselves
    # become mesh objects on the page, keyed by address, one box however
    # many panels see it.
    if value == "" or BLE_TOPIC.match(suffix):
        note_beacon(host, suffix, value)


# ----------------------------------------------------------- beacon registry

def beacon_label(addr, beacon=None):
    if beacon:
        name = beacon["fields"].get("name", {}).get("value")
        if name:
            return "%s (%s)" % (name, addr)
    return addr


def note_beacon(host, suffix, value):
    """One ble/<address>/<field> message into the beacon registry. An
    empty value is the firmware's way of saying the advertiser is gone:
    the field is dropped, and a beacon nobody sees any more is no beacon
    at all. Beacons are not persisted -- they are radio contacts, and
    the broker's retained messages rebuild the picture on every start."""
    match = BLE_TOPIC.match(suffix)
    if not match:
        return                                    # ble/count, ble/dropped

    addr, field = match.groups()
    now = time.time()

    with STATE.lock:
        beacon = STATE.beacons.get(addr)

        if value == "":
            if beacon is None:
                return

            seen = beacon["devices"].get(host)
            if seen is not None:
                seen["fields"].discard(field)
                if not seen["fields"]:
                    del beacon["devices"][host]

            if not beacon["devices"]:
                del STATE.beacons[addr]
                STATE.log("info", "beacon",
                          "beacon %s removed -- cleared" % beacon_label(addr, beacon))
            return

        if beacon is None:
            beacon = {
                "addr": addr,
                "fields": {},
                "devices": {},       # host -> {fields, rssi, count, ts}
                "history": [],       # best rssi, for the box's sparkline
                "first_seen": time.strftime("%Y-%m-%d %H:%M:%S"),
            }
            STATE.beacons[addr] = beacon
            STATE.log("info", "beacon",
                      "new beacon %s, seen by %s" % (addr, host))

        seen = beacon["devices"].setdefault(
            host, {"fields": set(), "rssi": None, "count": 0, "ts": now})
        seen["fields"].add(field)
        seen["count"] += 1
        seen["ts"] = now

        beacon["fields"][field] = {"value": value, "ts": now}

        if field == "rssi":
            try:
                seen["rssi"] = int(float(value))
            except (TypeError, ValueError):
                pass

            # The sparkline follows the strongest hearing of the moment;
            # which panel that is changes as the beacon moves around.
            best = max((d["rssi"] for d in beacon["devices"].values()
                        if d["rssi"] is not None), default=None)
            if best is not None:
                beacon["history"].append([round(now, 3), best])
                if len(beacon["history"]) > HISTORY_CAP:
                    del beacon["history"][:-HISTORY_CAP]

        # The distance estimate is per scanner: each panel hears the
        # beacon from where it stands. The topology view places the
        # beacon by these, as springs of metres scaled to pixels.
        if field == "distance":
            try:
                seen["distance"] = max(0.0, float(value))
            except (TypeError, ValueError):
                pass


def forget_device_everywhere(host):
    """A device removed from the list is removed from its beacons too --
    the panel is gone, the radio contacts it reported are not facts any
    more. A beacon with no panels left seeing it goes with it."""
    with STATE.lock:
        for addr in list(STATE.beacons):
            beacon = STATE.beacons[addr]
            if host in beacon["devices"]:
                del beacon["devices"][host]
            if not beacon["devices"]:
                del STATE.beacons[addr]
                STATE.log("info", "beacon",
                          "beacon %s removed -- nobody sees it"
                          % beacon_label(addr, beacon))


def on_mqtt_message(topic, payload, retain):
    """The whole tree arrives through one wildcard subscription; the levels
    between the base topic and the rest say which panel it came from."""
    value = payload.decode("utf-8", "replace")

    _, broker = active_broker()
    base = broker["base_topic"]

    prefix = base + "/"
    if not topic.startswith(prefix):
        return

    rest = topic[len(prefix):]
    parts = rest.split("/", 1)
    if len(parts) != 2:
        return                                   # the base topic itself

    host, suffix = parts
    if not host or not suffix:
        return

    note_message(host, suffix, value, retain)


# ------------------------------------------------------------------- broker

class Broker:
    """One MQTTClient, restarted whenever the settings demand it, plus the
    counters the page animates with."""

    def __init__(self):
        self.lock = threading.Lock()
        self.client = None
        self.host = ""
        self.port = 0
        self.connected = False
        self.reconnects = 0
        self.messages_in = 0
        self.messages_out = 0

    def stop(self):
        with self.lock:
            client = self.client
            self.client = None

        if client is not None:
            client.stop()
            STATE.log("info", "mqtt", "disconnected on request")

    def restart(self):
        index, broker = active_broker()

        self.stop()

        # The picture so far belongs to the broker we are leaving: what
        # it said stays visible, but nothing of it is known to be true
        # any more -- the new connection's retained messages decide,
        # exactly as they do at startup.
        with STATE.lock:
            for device in STATE.devices.values():
                device["online"] = False
            STATE.beacons = {}
            STATE.save()

        if not connection_snapshot()[0]:
            self.connected = False
            STATE.log("info", "mqtt", "the broker connection is off")
            return

        base = broker["base_topic"]
        self.host = broker["mqtt_host"]
        self.port = broker["mqtt_port"]

        client = mqtt_client.MQTTClient(
            self.host, self.port, "mqttviz-%s" % os.uname().nodename.lower(),
            username=broker["mqtt_user"],
            password=broker["mqtt_pass"],
            on_connect=self._on_connect,
            on_disconnect=self._on_disconnect,
            on_message=self._on_message,
            on_error=self._on_error)

        with self.lock:
            self.client = client

        client.subscribe(base + "/#")
        client.start()

        STATE.log("info", "mqtt", "connecting to %s:%d (%s), watching %s/#"
                  % (self.host, self.port, broker["name"], base))

    def publish(self, host, suffix, payload):
        """The page may only ever speak the topics the firmware subscribes
        to, and only to a device that is in the list."""
        if not PUBLISHABLE.match(suffix):
            return False, "not a topic the panel listens to"

        if len(payload.encode("utf-8")) > PAYLOAD_MAX:
            return False, "payload too long"

        with STATE.lock:
            if host not in STATE.devices:
                return False, "unknown device"

        _, broker = active_broker()
        base = broker["base_topic"]

        with self.lock:
            client = self.client
            connected = self.connected

        if client is None or not connected:
            return False, "not connected to the broker"

        topic = "%s/%s/%s" % (base, host, suffix)
        if not client.publish(topic, payload):
            return False, "publish failed"

        with self.lock:
            self.messages_out += 1

        STATE.log("info", "publish", "%s <- %s" % (topic, payload))
        return True, ""

    def as_dict(self):
        with self.lock:
            return {
                "host": self.host,
                "port": self.port,
                "connected": self.connected,
                "reconnects": self.reconnects,
                "messages_in": self.messages_in,
                "messages_out": self.messages_out,
            }

    def _on_connect(self):
        with self.lock:
            self.reconnects += 1
            self.connected = True
        STATE.log("info", "mqtt", "connected to %s:%d"
                  % (self.host, self.port))

    def _on_disconnect(self):
        with self.lock:
            self.connected = False
        STATE.log("warn", "mqtt", "connection to %s:%d lost"
                  % (self.host, self.port))

    def _on_message(self, topic, payload, retain):
        with self.lock:
            self.messages_in += 1
        on_mqtt_message(topic, payload, retain)

    def _on_error(self, message):
        STATE.log("warn", "mqtt", message)


BROKER = Broker()


# --------------------------------------------------------------- HTTP layer

def json_bytes(doc):
    return json.dumps(doc).encode("utf-8")


def masked_settings():
    """The page sees every setting except the passwords, which are masked
    the way devmgr masks its secrets: a mask sent back means 'unchanged'."""
    with STATE.lock:
        settings = dict(STATE.settings)
        settings["brokers"] = []

        brokers = STATE.settings.get("brokers") or [dict(DEFAULT_BROKER)]
        for profile in brokers:
            masked = dict(profile)
            if masked.get("mqtt_pass"):
                masked["mqtt_pass"] = SECRET_MASK
            settings["brokers"].append(masked)

        index = STATE.settings.get("broker_index") or 0
        settings["broker_index"] = max(0, min(index, len(brokers) - 1))
    return settings


class Handler(BaseHTTPRequestHandler):
    """The API and the static page. Quiet by default: request lines are
    logged to the debug console, not to stderr."""

    protocol_version = "HTTP/1.1"

    def log_message(self, fmt, *args):
        STATE.log("debug", "http", fmt % args)

    # -- helpers ---------------------------------------------------------

    def send_json(self, doc, status=200):
        body = json_bytes(doc)
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def send_error_json(self, message, status=400):
        self.send_json({"error": message}, status)

    def read_body(self):
        length = int(self.headers.get("Content-Length") or 0)
        return self.rfile.read(length) if length > 0 else b""

    def read_json_body(self):
        try:
            return json.loads(self.read_body().decode("utf-8", "replace"))
        except ValueError:
            return None

    # -- routing ---------------------------------------------------------

    def do_GET(self):
        path = self.path.split("?", 1)[0]

        if path == "/":
            return self.serve_static("index.html", "text/html")
        if path in ("/app.js", "/lang.js", "/style.css"):
            mime = "text/javascript" if path.endswith(".js") else "text/css"
            return self.serve_static(path[1:], mime)

        if path == "/api/state":
            return self.handle_state()
        if path == "/api/log":
            return self.handle_log()

        self.send_error_json("not found", 404)

    def do_POST(self):
        path = self.path.split("?", 1)[0]

        if path == "/api/settings":
            return self.handle_settings()
        if path == "/api/position":
            return self.handle_position()
        if path == "/api/position/delete":
            return self.handle_position_delete()
        if path == "/api/layout/save":
            return self.handle_layout_save()
        if path == "/api/layout/load":
            return self.handle_layout_load()
        if path == "/api/layout/delete":
            return self.handle_layout_delete()
        if path == "/api/layout/import":
            return self.handle_layout_import()
        if path == "/api/publish":
            return self.handle_publish()
        if path == "/api/log/clear":
            return self.handle_log_clear()

        match = re.match(r"^/api/device/([^/]+)/delete$", path)
        if match:
            return self.handle_device_delete(match.group(1))

        self.send_error_json("not found", 404)

    # -- static ----------------------------------------------------------

    def serve_static(self, name, mime):
        full = os.path.join(WEB_DIR, name)

        try:
            with open(full, "rb") as handle:
                body = handle.read()
        except OSError:
            return self.send_error_json("not found", 404)

        self.send_response(200)
        self.send_header("Content-Type", mime)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    # -- API -------------------------------------------------------------

    def handle_state(self):
        with STATE.lock:
            devices = []
            for host in sorted(STATE.devices):
                device = STATE.devices[host]

                topics = {}
                for suffix, record in device.get("topics", {}).items():
                    record = dict(record)
                    record["age"] = max(0, round(time.time()
                                                 - record.get("ts", 0)))
                    topics[suffix] = record

                devices.append({
                    "host": device["host"],
                    "online": device.get("online", False),
                    "first_seen": device.get("first_seen"),
                    "last_seen": device.get("last_seen"),
                    "last_age": (max(0, round(time.time()
                                              - device.get("last_ts", 0)))
                                 if device.get("last_ts") else None),
                    "msgcount": device.get("msgcount", 0),
                    "topics": topics,
                    "history": device.get("history", {}),
                })

            now = time.time()
            beacons = []
            for addr in sorted(STATE.beacons):
                beacon = STATE.beacons[addr]
                fields = beacon["fields"]
                heard = []

                best_rssi = None
                latest_ts = 0
                for host, seen in beacon["devices"].items():
                    heard.append({
                        "host": host,
                        "rssi": seen["rssi"],
                        "distance": seen.get("distance"),
                        "age": max(0, round(now - seen["ts"])),
                        "count": seen["count"],
                    })
                    if seen["rssi"] is not None \
                            and (best_rssi is None or seen["rssi"] > best_rssi):
                        best_rssi = seen["rssi"]
                    latest_ts = max(latest_ts, seen["ts"])

                # A beacon the panels still list but have not heard for a
                # while is drawn as asleep, not gone: walking out of range
                # and back is what beacons do.
                heard.sort(key=lambda d: -(d["rssi"] if d["rssi"] is not None
                                            else -999))

                beacons.append({
                    "addr": addr,
                    "name": fields.get("name", {}).get("value", ""),
                    "id": fields.get("id", {}).get("value", ""),
                    "type": fields.get("type", {}).get("value", ""),
                    "fields": {field: {"value": record["value"],
                                       "age": max(0, round(now - record["ts"]))}
                               for field, record in fields.items()},
                    "devices": heard,
                    "rssi": best_rssi,
                    "active": (now - latest_ts) < 90,
                    "age": max(0, round(now - latest_ts)) if latest_ts else None,
                    "history": beacon.get("history", [])[-HISTORY_CAP:],
                    "first_seen": beacon.get("first_seen"),
                })

            self.send_json({
                "settings": masked_settings(),
                "devices": devices,
                "beacons": beacons,
                "broker": BROKER.as_dict(),
            })

    def handle_settings(self):
        body = self.read_json_body()

        if not isinstance(body, dict):
            return self.send_error_json("expected a JSON object")

        with STATE.lock:
            before = connection_snapshot()

            if "mqtt_enabled" in body:
                STATE.settings["mqtt_enabled"] = bool(body["mqtt_enabled"])

            # The profile list arrives whole: the page owns adding,
            # removing and editing entries, this end makes them true.
            # The password mask means "leave it alone", matched by the
            # profile's id, so renaming and reordering are safe. An
            # explicit index in the same body refers to the incoming
            # list's order, before the alphabetical sort.
            if "brokers" in body:
                incoming = body["brokers"]

                if not isinstance(incoming, list) or not incoming:
                    return self.send_error_json(
                        "the broker list cannot be empty")

                names = [str(p.get("name") or "").strip()
                         for p in incoming]
                if any(not name for name in names):
                    return self.send_error_json("every broker needs a name")
                if len(set(names)) != len(names):
                    return self.send_error_json(
                        "the broker names must be distinct")

                existing = STATE.settings.get("brokers") or []
                old_index = STATE.settings.get("broker_index") or 0
                active = existing[old_index] \
                    if 0 <= old_index < len(existing) else None

                profiles = []
                for profile in incoming:
                    merged = dict(profile)
                    if merged.get("mqtt_pass") == SECRET_MASK:
                        # by id, and by position as the fallback for a
                        # list that predates ids
                        old = next((p for p in existing
                                    if p.get("id") == merged.get("id")), None)
                        if old is None:
                            position = incoming.index(profile)
                            if 0 <= position < len(existing):
                                old = existing[position]
                        if old is not None:
                            merged["mqtt_pass"] = old["mqtt_pass"]
                    profiles.append(normalise_broker(merged))

                STATE.settings["brokers"], follow = sort_brokers(
                    profiles, follow_id=active and active.get("id"),
                    follow_name=active and active["name"])
                STATE.settings["broker_index"] = follow

            if "broker_index" in body:
                try:
                    index = int(body["broker_index"])
                except (TypeError, ValueError):
                    return self.send_error_json("broker index must be a number")

                if "brokers" in body:
                    # a switch made in the same breath as the new list
                    # names a position of the incoming list
                    if not 0 <= index < len(incoming):
                        return self.send_error_json("no broker at that index")
                    chosen = incoming[index]
                    STATE.settings["broker_index"] = next(
                        i for i, profile in enumerate(STATE.settings["brokers"])
                        if profile.get("id") == chosen.get("id")
                        or profile["name"] == chosen.get("name"))
                else:
                    brokers = STATE.settings.get("brokers") or []
                    if not 0 <= index < len(brokers):
                        return self.send_error_json("no broker at that index")
                    STATE.settings["broker_index"] = index

            if "log_level" in body:
                if body["log_level"] not in LEVELS:
                    return self.send_error_json("unknown log level")
                STATE.settings["log_level"] = body["log_level"]

            # View switches -- what the page draws, not what it knows.
            # The devices keep tracking either way.
            if "show_broker" in body:
                STATE.settings["show_broker"] = bool(body["show_broker"])

            if "show_beacons" in body:
                STATE.settings["show_beacons"] = bool(body["show_beacons"])

            # The effects layer: the physics is the same either way, the
            # glow is what the switch turns off.
            if "show_fx" in body:
                STATE.settings["show_fx"] = bool(body["show_fx"])

            # Which picture the canvas draws of the same facts.
            if "view_mode" in body:
                STATE.settings["view_mode"] = \
                    body["view_mode"] if body["view_mode"] in VIEW_MODES \
                    else "mesh"

            # The topology view's camera: how much of the workspace the
            # viewport shows, and where it looks. The page owns the
            # motion while it is being moved; this is where the settled
            # picture is kept.
            if "cam" in body:
                STATE.settings["cam"] = normalise_cam(body.get("cam"))

            # The physics sliders: whole numbers in their ranges, whatever
            # the page meant by them, clamped here -- and set apart for
            # the two kinds of things that float, the panels and the
            # beacons. The canvas takes care of what the numbers mean.
            for phys_key in PHYS_KINDS:
                if phys_key not in body:
                    continue

                incoming = body[phys_key]
                if not isinstance(incoming, dict):
                    return self.send_error_json(
                        phys_key + " must be an object")

                merged = dict(STATE.settings.get(phys_key) or {})
                for key, default in PHYS_DEFAULTS.items():
                    if key not in incoming:
                        continue
                    try:
                        value = int(incoming[key])
                    except (TypeError, ValueError):
                        return self.send_error_json(
                            "%s.%s must be a number" % (phys_key, key))
                    low, high = PHYS_RANGES.get(key, (0, 100))
                    merged[key] = max(low, min(high, value))
                STATE.settings[phys_key] = normalise_phys(merged)

            # The language of the page: English or German.
            if "lang" in body:
                if body["lang"] not in ("en", "de"):
                    return self.send_error_json("lang must be 'en' or 'de'")
                STATE.settings["lang"] = body["lang"]

            if "verbose" in body:
                STATE.settings["verbose"] = bool(body["verbose"])

            STATE.save()
            STATE.save_now_if_dirty()
            after = connection_snapshot()

        # The backup file follows the picture switch too -- the
        # arrangement and the picture it belongs to are one fact.
        write_layout_backup()

        # Only a change to the connection is worth a reconnect; log level,
        # verbosity and view switches are read as they are.
        if before != after:
            threading.Thread(target=BROKER.restart, daemon=True).start()

        self.send_json({"settings": masked_settings()})

    def handle_position(self):
        """Pin one object of the topology view to the canvas: the key
        says which object, x and y say where, as fractions of the
        canvas."""
        body = self.read_json_body()

        if not isinstance(body, dict):
            return self.send_error_json("expected a JSON object")

        key = normalise_position_key(body.get("key"))
        pos = normalise_position(body)
        if not key or not pos:
            return self.send_error_json("a position needs a key, x and y")

        with STATE.lock:
            STATE.settings.setdefault("positions", {})[key] = pos
            STATE.settings["positions"] = normalise_positions(
                STATE.settings["positions"])
            STATE.save()
            STATE.save_now_if_dirty()
            positions = dict(STATE.settings["positions"])

        # The backup file follows every place the canvas keeps, the
        # moment it is kept.
        write_layout_backup()

        self.send_json({"ok": True, "positions": positions})

    def handle_position_delete(self):
        """Unpin one object: it floats on its links again."""
        body = self.read_json_body()

        if not isinstance(body, dict):
            return self.send_error_json("expected a JSON object")

        key = normalise_position_key(body.get("key"))
        if not key:
            return self.send_error_json("a position needs a key")

        with STATE.lock:
            STATE.settings.setdefault("positions", {}).pop(key, None)
            STATE.save()
            STATE.save_now_if_dirty()
            positions = dict(STATE.settings["positions"])

        write_layout_backup()

        self.send_json({"ok": True, "positions": positions})

    def handle_layout_save(self):
        """Keep the arrangement as it stands under a name: the pinned
        places and the picture they belong to, to be brought back whole.
        The same name twice is the newer arrangement winning -- a name
        is a slot, not a promise."""
        body = self.read_json_body()

        if not isinstance(body, dict):
            return self.send_error_json("expected a JSON object")

        name = normalise_layout_name(body.get("name"))
        if not name:
            return self.send_error_json("a layout needs a name")

        with STATE.lock:
            layouts = normalise_layouts(STATE.settings.get("layouts"))
            layouts[name] = layout_snapshot(STATE.settings.get("positions"),
                                            STATE.settings.get("view_mode"))
            layouts[name]["saved_at"] = time.strftime("%Y-%m-%d %H:%M:%S")
            STATE.settings["layouts"] = layouts
            STATE.save()
            STATE.save_now_if_dirty()

        write_layout_backup()

        self.send_json({"ok": True, "settings": masked_settings()})

    def handle_layout_load(self):
        """Bring a saved arrangement back whole: its places are the
        canvas's places again and its picture is the picture drawn.
        What stood before is remembered in the backup file -- trying a
        layout on never costs the one that was there."""
        body = self.read_json_body()

        if not isinstance(body, dict):
            return self.send_error_json("expected a JSON object")

        name = normalise_layout_name(body.get("name"))
        if not name:
            return self.send_error_json("a layout needs a name")

        with STATE.lock:
            layouts = normalise_layouts(STATE.settings.get("layouts"))
            layout = layouts.get(name)
            if layout is None:
                return self.send_error_json("no layout by that name")

            STATE.before_load = layout_snapshot(
                STATE.settings.get("positions"),
                STATE.settings.get("view_mode"))
            STATE.before_load["replaced_by"] = name
            STATE.before_load["ts"] = time.strftime("%Y-%m-%d %H:%M:%S")

            STATE.settings["positions"] = normalise_positions(
                layout["positions"])
            STATE.settings["view_mode"] = layout["view_mode"]
            STATE.save()
            STATE.save_now_if_dirty()

        write_layout_backup()

        self.send_json({"ok": True, "settings": masked_settings()})

    def handle_layout_delete(self):
        """Forget one saved arrangement. The file it can be read back
        from is whatever the page downloaded while it existed."""
        body = self.read_json_body()

        if not isinstance(body, dict):
            return self.send_error_json("expected a JSON object")

        name = normalise_layout_name(body.get("name"))
        if not name:
            return self.send_error_json("a layout needs a name")

        with STATE.lock:
            layouts = normalise_layouts(STATE.settings.get("layouts"))
            if name not in layouts:
                return self.send_error_json("no layout by that name")
            del layouts[name]
            STATE.settings["layouts"] = layouts
            STATE.save()
            STATE.save_now_if_dirty()

        write_layout_backup()

        self.send_json({"ok": True, "settings": masked_settings()})

    def handle_layout_import(self):
        """Read layouts back in from a file -- the one the page
        downloads, or the backup file this server keeps beside the
        state. Names the file carries win over the same names here;
        names only here live on: an import adds, it never wipes."""
        body = self.read_json_body()

        if not isinstance(body, dict) \
                or not isinstance(body.get("layouts"), dict):
            return self.send_error_json("expected a layouts object")

        incoming = normalise_layouts(body["layouts"])

        with STATE.lock:
            layouts = normalise_layouts(STATE.settings.get("layouts"))
            layouts.update(incoming)
            STATE.settings["layouts"] = layouts
            STATE.save()
            STATE.save_now_if_dirty()

        write_layout_backup()

        self.send_json({"ok": True, "imported": len(incoming),
                        "settings": masked_settings()})

    def handle_publish(self):
        body = self.read_json_body()

        if not isinstance(body, dict):
            return self.send_error_json("expected a JSON object")

        host = str(body.get("device") or "")
        suffix = str(body.get("suffix") or "")
        payload = "" if body.get("payload") is None else str(body["payload"])

        if not host:
            return self.send_error_json("missing device")

        ok, error = BROKER.publish(host, suffix, payload)
        if not ok:
            return self.send_error_json(error)

        self.send_json({"ok": True})

    def handle_log(self):
        since = 0
        level = "debug"

        query = urllib.parse.parse_qs(urllib.parse.urlsplit(self.path).query)

        if "since" in query:
            try:
                since = int(query["since"][0])
            except ValueError:
                pass
        if "level" in query and query["level"][0] in LEVELS:
            level = query["level"][0]

        self.send_json({"entries": STATE.log_since(since, level)})

    def handle_log_clear(self):
        with STATE.lock:
            STATE.log_entries.clear()
        self.send_json({"ok": True})

    def handle_device_delete(self, host):
        with STATE.lock:
            device = STATE.devices.pop(host, None)
            STATE.save()

        if device is None:
            return self.send_error_json("not found", 404)

        forget_device_everywhere(host)
        STATE.log("info", "device", "%s removed from the list" % host)
        self.send_json({"ok": True})


# ----------------------------------------------------------------- shutdown

def run_server(port):
    server = ThreadingHTTPServer(("0.0.0.0", port), Handler)

    stopping = threading.Event()

    def watch_stdin():
        # The ^D that ends a terminal session should end the tool too,
        # the way it ends the device manager -- but only when there is a
        # terminal: a detached process gets /dev/null and must not read
        # its end-of-file as a request to quit.
        try:
            while input():
                pass
        except EOFError:
            pass
        stopping.set()
        threading.Thread(target=server.shutdown, daemon=True).start()

    if sys.stdin.isatty():
        threading.Thread(target=watch_stdin, daemon=True).start()

    STATE.log("info", "system", "mqttviz listening on http://localhost:%d"
              % port)
    print("mqttviz: http://localhost:%d" % port)

    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        stopping.set()
        server.server_close()

    # One last thing: an orderly save, so a session ended by ^C or ^D
    # does not lean on the debounced saver.
    STATE.save_now_if_dirty()
    BROKER.stop()


def main():
    parser = argparse.ArgumentParser(
        description="The OhEzTouch MQTT visualizer")
    parser.add_argument("--port", type=int, default=DEFAULT_PORT,
                        help="where the page is served (default %d)"
                        % DEFAULT_PORT)
    parser.add_argument("--data-dir", metavar="DIR",
                        help="keep the state somewhere else -- which is how"
                             " a second instance, or a test run, avoids"
                             " touching the live configuration")
    parser.add_argument("--mqtt-host", help="the MQTT broker to watch")
    parser.add_argument("--mqtt-port", type=int, help="the broker's port")
    parser.add_argument("--user", help="the broker username, if any")
    parser.add_argument("--password", help="the broker password, if any")
    parser.add_argument("--base", help="the base topic (default oheztouch)")
    parser.add_argument("--no-mqtt", action="store_true",
                        help="start without connecting to the broker")
    args = parser.parse_args()

    global STATE, DATA_DIR, STATE_FILE, LAYOUT_FILE

    if args.data_dir:
        DATA_DIR = args.data_dir
        STATE_FILE = os.path.join(DATA_DIR, "mqttviz.json")
        LAYOUT_FILE = os.path.join(DATA_DIR, "layouts.json")

    STATE = State()

    # Command line overrides: they are written into the settings -- into
    # the profile the tool is connected to, that is -- so what was typed
    # once is what the next start remembers.
    if args.mqtt_host or args.mqtt_port or args.user is not None \
            or args.password is not None or args.base:
        index, profile = active_broker()
        if args.mqtt_host:
            profile["mqtt_host"] = args.mqtt_host
        if args.mqtt_port:
            profile["mqtt_port"] = args.mqtt_port
        if args.user is not None:
            profile["mqtt_user"] = args.user
        if args.password is not None:
            profile["mqtt_pass"] = args.password
        if args.base:
            profile["base_topic"] = topic_sanitise(args.base)
        STATE.settings["brokers"][index] = normalise_broker(profile)
        STATE.save_now_if_dirty()
    if args.no_mqtt:
        STATE.settings["mqtt_enabled"] = False

    STATE.save_now_if_dirty()

    # The backup file exists from the first moment on: everything about
    # the arrangement, readable, before anything has had the chance to
    # go wrong.
    write_layout_backup()

    if STATE.settings.get("mqtt_enabled"):
        BROKER.restart()

    run_server(args.port)


if __name__ == "__main__":
    main()
