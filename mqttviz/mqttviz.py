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

ON_VALUES = ("online", "on", "true", "yes", "1")


def topic_sanitise(base):
    """The same rules the firmware applies to its own base topic: no
    wildcards, no empty levels, and never the empty string."""
    parts = [part for part in str(base).split("/") if part]
    cleaned = [part for part in parts if part not in ("+", "#")]
    return "/".join(cleaned) or "oheztouch"


# --------------------------------------------------------------------- state

class State:
    """The settings, the device registry and the console log, one lock over
    all of it: the broker thread, the saver and the HTTP handlers all write
    here."""

    DEFAULT_SETTINGS = {
        "mqtt_enabled": True,
        "mqtt_host": "localhost",
        "mqtt_port": 1883,
        "mqtt_user": "",
        "mqtt_pass": "",
        "base_topic": "oheztouch",
        "log_level": "info",
        "verbose": False,
    }

    def __init__(self):
        self.lock = threading.RLock()
        self.settings = dict(self.DEFAULT_SETTINGS)
        self.devices = {}          # hostname -> device dict
        self.log_entries = deque(maxlen=LOG_CAPACITY)
        self.log_seq = 0
        self._dirty = False
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


STATE = State()


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


def on_mqtt_message(topic, payload, retain):
    """The whole tree arrives through one wildcard subscription; the levels
    between the base topic and the rest say which panel it came from."""
    value = payload.decode("utf-8", "replace")

    with STATE.lock:
        base = STATE.settings.get("base_topic", "oheztouch")

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
        with STATE.lock:
            settings = dict(STATE.settings)

        self.stop()

        if not settings.get("mqtt_enabled"):
            self.connected = False
            STATE.log("info", "mqtt", "the broker connection is off")
            return

        base = topic_sanitise(settings.get("base_topic"))
        self.host = settings.get("mqtt_host") or "localhost"
        self.port = int(settings.get("mqtt_port") or 1883)

        client = mqtt_client.MQTTClient(
            self.host, self.port, "mqttviz-%s" % os.uname().nodename.lower(),
            username=settings.get("mqtt_user") or "",
            password=settings.get("mqtt_pass") or "",
            on_connect=self._on_connect,
            on_disconnect=self._on_disconnect,
            on_message=self._on_message,
            on_error=self._on_error)

        with self.lock:
            self.client = client

        client.subscribe(base + "/#")
        client.start()

        STATE.log("info", "mqtt", "connecting to %s:%d, watching %s/#"
                  % (self.host, self.port, base))

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
            base = topic_sanitise(STATE.settings.get("base_topic"))

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
    """The page sees every setting except the password, which is masked the
    way devmgr masks it: a mask sent back means 'unchanged'."""
    settings = dict(STATE.settings)
    if settings.get("mqtt_pass"):
        settings["mqtt_pass"] = SECRET_MASK
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
        if path in ("/app.js", "/style.css"):
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

            self.send_json({
                "settings": masked_settings(),
                "devices": devices,
                "broker": BROKER.as_dict(),
            })

    def handle_settings(self):
        body = self.read_json_body()

        if not isinstance(body, dict):
            return self.send_error_json("expected a JSON object")

        with STATE.lock:
            before = dict(STATE.settings)

            if "mqtt_enabled" in body:
                STATE.settings["mqtt_enabled"] = bool(body["mqtt_enabled"])

            if "mqtt_host" in body:
                host = str(body["mqtt_host"]).strip()
                if not host or "/" in host:
                    return self.send_error_json("bad broker host")
                STATE.settings["mqtt_host"] = host

            if "mqtt_port" in body:
                try:
                    port = int(body["mqtt_port"])
                except (TypeError, ValueError):
                    return self.send_error_json("port must be a number")
                if port < 1 or port > 65535:
                    return self.send_error_json("port out of range (1..65535)")
                STATE.settings["mqtt_port"] = port

            if "mqtt_user" in body:
                STATE.settings["mqtt_user"] = str(body["mqtt_user"]).strip()

            if "mqtt_pass" in body:
                # The mask the state API hands out means "leave it alone".
                if body["mqtt_pass"] != SECRET_MASK:
                    STATE.settings["mqtt_pass"] = str(body["mqtt_pass"])

            if "base_topic" in body:
                STATE.settings["base_topic"] = topic_sanitise(body["base_topic"])

            if "log_level" in body:
                if body["log_level"] not in LEVELS:
                    return self.send_error_json("unknown log level")
                STATE.settings["log_level"] = body["log_level"]

            if "verbose" in body:
                STATE.settings["verbose"] = bool(body["verbose"])

            STATE.save()
            STATE.save_now_if_dirty()
            after = dict(STATE.settings)

        # Only a change to the connection is worth a reconnect; log level
        # and verbosity are read as they are.
        keys = ("mqtt_enabled", "mqtt_host", "mqtt_port", "mqtt_user",
                "mqtt_pass", "base_topic")
        if any(before[key] != after[key] for key in keys):
            threading.Thread(target=BROKER.restart, daemon=True).start()

        self.send_json({"settings": masked_settings()})

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
    parser.add_argument("--mqtt-host", help="the MQTT broker to watch")
    parser.add_argument("--mqtt-port", type=int, help="the broker's port")
    parser.add_argument("--user", help="the broker username, if any")
    parser.add_argument("--password", help="the broker password, if any")
    parser.add_argument("--base", help="the base topic (default oheztouch)")
    parser.add_argument("--no-mqtt", action="store_true",
                        help="start without connecting to the broker")
    args = parser.parse_args()

    # Command line overrides: they are written into the settings, so what
    # was typed once is what the next start remembers.
    if args.mqtt_host:
        STATE.settings["mqtt_host"] = args.mqtt_host
    if args.mqtt_port:
        STATE.settings["mqtt_port"] = args.mqtt_port
    if args.user is not None:
        STATE.settings["mqtt_user"] = args.user
    if args.password is not None:
        STATE.settings["mqtt_pass"] = args.password
    if args.base:
        STATE.settings["base_topic"] = topic_sanitise(args.base)
    if args.no_mqtt:
        STATE.settings["mqtt_enabled"] = False

    STATE.save_now_if_dirty()

    if STATE.settings.get("mqtt_enabled"):
        BROKER.restart()

    run_server(args.port)


if __name__ == "__main__":
    main()
