#!/usr/bin/env python3
"""The OhEzTouch device manager: a web UI for the fleet.

Serves a single-page interface on localhost and talks to the devices through
the firmware's REST API (0.91 and later). Everything the manager knows --
the subnet to scan, the refresh interval, and every device it has ever
seen -- lives in data/devmgr.json, so a restart loses nothing, and a device
that stops answering is shown as offline rather than forgotten.

    python3 devmgr/devmgr.py [--port 8088]

Needs nothing but Python 3. The background work -- the periodic refresh,
scans, firmware updates -- is logged to a ring buffer that the page's debug
console polls, so what the manager is doing is always one click away.
"""

import argparse
import ipaddress
import json
import os
import re
import threading
import time
import urllib.error
import urllib.parse
import urllib.request
from collections import deque
from concurrent.futures import ThreadPoolExecutor
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

import probe
import updater

HERE = os.path.dirname(os.path.abspath(__file__))
DATA_DIR = os.path.join(HERE, "data")
STATE_FILE = os.path.join(DATA_DIR, "devmgr.json")
UPLOAD_DIR = os.path.join(DATA_DIR, "uploads")
WEB_DIR = os.path.join(HERE, "web")

DEFAULT_PORT = 8088
LOG_CAPACITY = 500

# The port devices serve their web interface on. 80 in the field; the Linux
# simulator moves to 8780, which is why this is an option at all.
DEVICE_PORT = 80

LEVELS = ("debug", "info", "warn", "error")


def hostport(host):
    return host if DEVICE_PORT == 80 else "%s:%d" % (host, DEVICE_PORT)


# --------------------------------------------------------------------- state

class State:
    """Everything the manager persists, plus the log. One lock covers both:
     the file and the buffer are written from the scheduler, the scan, the
     update workers and the HTTP handlers alike."""

    DEFAULT_SETTINGS = {
        "subnet": "192.168.1.0/24",
        "interval_s": 10,
        # Off until asked for: a manager left open on a desktop should not
        # poll a wall of panels forever without someone having chosen that.
        "refresh_enabled": False,
        "log_level": "info",
    }

    def __init__(self):
        self.lock = threading.RLock()
        self.settings = dict(self.DEFAULT_SETTINGS)
        self.devices = {}          # mac -> device dict
        self.log_entries = deque(maxlen=LOG_CAPACITY)
        self.log_seq = 0
        self.scan = {"running": False, "done": 0, "total": 0}
        self.updates = {}          # mac -> DeviceUpdate
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

        # Nothing is online at startup: the first refresh decides.
        for device in self.devices.values():
            device["online"] = False

    def save(self):
        """Write the file, debounced: callers mark dirty, and the actual
        write happens at most once every couple of seconds from whoever
        notices first."""
        with self.lock:
            self._dirty = True

    def save_now_if_dirty(self):
        with self.lock:
            if not self._dirty:
                return
            self._dirty = False

            doc = {"settings": self.settings, "devices": self.devices}

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


# ------------------------------------------------------------ device records

def upsert_device(doc, address):
    """One answered /api/status into the store, keyed by its MAC. Returns
    (mac, is_new)."""
    with STATE.lock:
        mac = doc["mac"]
        device = STATE.devices.get(mac, {})
        is_new = not device

        now = time.strftime("%Y-%m-%d %H:%M:%S")

        device.update({
            "mac": mac,
            "ip": address,
            "hostname": doc.get("hostname") or "",
            "target_name": doc.get("target") or "",
            "version": doc.get("version") or "",
            "ssid": doc.get("ssid") or "",
            "wired": bool(doc.get("wired")),
            "rssi": doc.get("rssi"),
            "uptime_s": doc.get("uptime_s") or 0,
            "free_heap": doc.get("free_heap") or 0,
            "online": True,
            "last_seen": now,
        })

        device.setdefault("first_seen", now)
        device.setdefault("comment", "")

        STATE.devices[mac] = device
        STATE.save()

        return mac, is_new


def mark_offline(mac):
    with STATE.lock:
        device = STATE.devices.get(mac)
        if device is not None and device.get("online"):
            device["online"] = False
            STATE.save()
            return device
    return None


def device_host(device):
    """Where to talk to a device: its last seen address and port."""
    return hostport(device["ip"])


# ------------------------------------------------------------------ scanning

def run_scan(subnet):
    STATE.log("info", "scan", "scanning %s ..." % subnet)

    try:
        def on_found(address, doc):
            name = doc.get("hostname") or address
            STATE.log("info", "scan", "found %s at %s (%s, v%s)"
                      % (name, address, doc.get("target"), doc.get("version")))

        def on_progress(done, total):
            with STATE.lock:
                STATE.scan.update({"done": done, "total": total})

        found = probe.scan_subnet(subnet, port=DEVICE_PORT, on_found=on_found,
                                  on_progress=on_progress)
    except ValueError as error:
        STATE.log("error", "scan", "bad subnet %r: %s" % (subnet, error))
        with STATE.lock:
            STATE.scan["running"] = False
        return
    except Exception as error:
        STATE.log("error", "scan", "scan failed: %s" % error)
        with STATE.lock:
            STATE.scan["running"] = False
        return

    for address, doc in found:
        upsert_device(doc, address)

    STATE.log("info", "scan", "scan finished: %d device(s) found" % len(found))

    with STATE.lock:
        STATE.scan["running"] = False


def start_scan(subnet):
    with STATE.lock:
        if STATE.scan["running"]:
            return False
        STATE.scan = {"running": True, "done": 0, "total": 0}

    thread = threading.Thread(target=run_scan, args=(subnet,), daemon=True)
    thread.start()
    return True


# ------------------------------------------------------------------ refresh

def refresh_once():
    """One pass over every known device. Transitions are what gets logged --
     a steady 'still there' every ten seconds is noise, not signal."""
    with STATE.lock:
        targets = {d["ip"]: mac for mac, d in STATE.devices.items()}

    if not targets:
        return

    results = probe.refresh_devices(list(targets), port=DEVICE_PORT)

    for host, doc in results.items():
        mac = targets[host]

        if doc is None:
            device = mark_offline(mac)
            if device is not None:
                STATE.log("warn", "refresh", "%s went offline"
                          % (device.get("hostname") or host))
            continue

        with STATE.lock:
            was_offline = not STATE.devices[mac].get("online", False)

        upsert_device(doc, host)

        if was_offline:
            STATE.log("info", "refresh", "%s is back"
                      % (doc.get("hostname") or host))

    STATE.save_now_if_dirty()


def scheduler(stop_event):
    """The refresh timer. One tick a second; the interval setting says how
    many ticks make a pass. Refreshing can be switched off entirely -- the
    scan and the manual refresh still work then."""
    last = 0.0

    while not stop_event.is_set():
        with STATE.lock:
            enabled = STATE.settings.get("refresh_enabled", True)
            interval = STATE.settings.get("interval_s", 10)

        if enabled and time.time() - last >= interval:
            last = time.time()
            try:
                refresh_once()
            except Exception as error:
                STATE.log("error", "refresh", "refresh failed: %s" % error)

        STATE.save_now_if_dirty()

        stop_event.wait(1.0)


# ------------------------------------------------------------------ updates

def start_updates(macs, image_kind, uploaded_path=None):
    """One DeviceUpdate per selected device. image_kind "auto" picks the
    image per target from the release directory or the build tree; anything
    else is the uploaded file, which goes to every device as-is."""
    started = []
    skipped = []

    with STATE.lock:
        devices = {mac: dict(STATE.devices[mac]) for mac in macs
                   if mac in STATE.devices}

    for mac, device in devices.items():
        name = device.get("hostname") or device["ip"]

        if image_kind == "auto":
            target_name = device.get("target_name") or ""
            target = None

            for build, display in updater.TARGETS.items():
                if display.lower() == target_name.strip().lower():
                    target = build
                    break

            if target is None:
                skipped.append({"mac": mac, "reason":
                                "target %r is not a known build" % target_name})
                continue

            image, source = updater.image_for(target)

            if image is None:
                skipped.append({"mac": mac, "reason":
                                "no image for target %s" % target})
                continue

            expect_version = updater.expected_version_default() \
                if source == "build tree" else (source or "").split()[-1]
            expect_target = updater.TARGETS[target]
        else:
            image = uploaded_path
            expect_version = None
            expect_target = None

        STATE.log("info", "update", "%s: starting update with %s"
                  % (name, image))

        worker = updater.DeviceUpdate(
            device_host(device), image,
            expect_version=expect_version, expect_target=expect_target,
            logger=STATE.log)

        with STATE.lock:
            STATE.updates[mac] = worker

        worker.start()
        started.append(mac)

    return started, skipped


def updates_status():
    with STATE.lock:
        return {mac: worker.as_dict() for mac, worker in STATE.updates.items()}


# --------------------------------------------------------------- HTTP layer

def json_bytes(doc):
    return json.dumps(doc).encode("utf-8")


class Handler(BaseHTTPRequestHandler):
    """The API and the static page. Quiet by default: request lines are
    logged to the debug console at debug level, not to stderr."""

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

    def device_or_404(self, mac):
        with STATE.lock:
            device = STATE.devices.get(mac)
            return dict(device) if device is not None else None

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
        if path == "/api/update/images":
            return self.handle_update_images()

        match = re.match(r"^/api/device/([^/]+)/config$", path)
        if match:
            return self.handle_device_config_get(match.group(1))

        self.send_error_json("not found", 404)

    def do_POST(self):
        path = self.path.split("?", 1)[0]

        if path == "/api/settings":
            return self.handle_settings()
        if path == "/api/scan":
            return self.handle_scan()
        if path == "/api/refresh":
            return self.handle_refresh()
        if path == "/api/log/clear":
            return self.handle_log_clear()
        if path == "/api/devices/config":
            return self.handle_bulk_config()
        if path == "/api/update":
            return self.handle_update()
        if path == "/api/update/upload":
            return self.handle_update_upload()

        match = re.match(r"^/api/device/([^/]+)/config$", path)
        if match:
            return self.handle_device_config_post(match.group(1))

        match = re.match(r"^/api/device/([^/]+)/(restart|chime|comment)$", path)
        if match:
            return self.handle_device_action(match.group(1), match.group(2))

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

    # -- API: state, settings, log ---------------------------------------

    def handle_state(self):
        with STATE.lock:
            self.send_json({
                "settings": STATE.settings,
                "devices": sorted(STATE.devices.values(),
                                  key=lambda d: d.get("hostname") or d["ip"]),
                "scan": STATE.scan,
                "updates": updates_status(),
            })

    def handle_settings(self):
        body = self.read_json_body()

        if not isinstance(body, dict):
            return self.send_error_json("expected a JSON object")

        with STATE.lock:
            if "subnet" in body:
                try:
                    subnet = str(ipaddress.ip_network(str(body["subnet"]),
                                                      strict=False))
                except ValueError as error:
                    return self.send_error_json("bad subnet: %s" % error)
                STATE.settings["subnet"] = subnet

            if "interval_s" in body:
                try:
                    interval = int(body["interval_s"])
                except (TypeError, ValueError):
                    return self.send_error_json("interval must be a number")
                if interval < 2 or interval > 3600:
                    return self.send_error_json("interval out of range (2..3600)")
                STATE.settings["interval_s"] = interval

            if "refresh_enabled" in body:
                STATE.settings["refresh_enabled"] = bool(body["refresh_enabled"])

            if "log_level" in body:
                if body["log_level"] not in LEVELS:
                    return self.send_error_json("unknown log level")
                STATE.settings["log_level"] = body["log_level"]

            STATE.save()
            STATE.save_now_if_dirty()

            self.send_json({"settings": STATE.settings})

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

    # -- API: scan and refresh --------------------------------------------

    def handle_scan(self):
        body = self.read_json_body() or {}

        subnet = str(body.get("subnet") or STATE.settings["subnet"])

        try:
            subnet = str(ipaddress.ip_network(subnet, strict=False))
        except ValueError as error:
            return self.send_error_json("bad subnet: %s" % error)

        with STATE.lock:
            STATE.settings["subnet"] = subnet
            STATE.save()

        if not start_scan(subnet):
            return self.send_error_json("a scan is already running", 409)

        self.send_json({"ok": True})

    def handle_refresh(self):
        threading.Thread(target=refresh_once, daemon=True).start()
        self.send_json({"ok": True})

    # -- API: single device ------------------------------------------------

    def proxy_get(self, device, path, timeout=5):
        return probe.fetch_json(device_host(device), path, timeout)

    def handle_device_config_get(self, mac):
        device = self.device_or_404(mac)

        if device is None:
            return self.send_error_json("unknown device", 404)

        try:
            doc = self.proxy_get(device, "/api/config")
        except probe.DeviceUnreachable as error:
            STATE.log("warn", "config", "%s: config read failed: %s"
                      % (device.get("hostname") or device["ip"], error))
            return self.send_error_json("device unreachable: %s" % error, 502)

        self.send_json(doc)

    def handle_device_config_post(self, mac):
        device = self.device_or_404(mac)

        if device is None:
            return self.send_error_json("unknown device", 404)

        fields = self.read_json_body()

        if not isinstance(fields, dict) or not fields:
            return self.send_error_json("expected a JSON object of fields")

        name = device.get("hostname") or device["ip"]

        try:
            status, doc = probe.post_json(device_host(device), "/api/config",
                                          fields, 5)
        except probe.DeviceUnreachable as error:
            STATE.log("warn", "config", "%s: save failed: %s" % (name, error))
            return self.send_error_json("device unreachable: %s" % error, 502)

        if status == 200:
            STATE.log("info", "config", "%s: applied %s"
                      % (name, ", ".join(sorted(fields))))
        else:
            STATE.log("warn", "config", "%s: rejected %s" % (name, doc))

        self.send_json(doc, status)

    def handle_device_action(self, mac, action):
        device = self.device_or_404(mac)

        if device is None:
            return self.send_error_json("unknown device", 404)

        name = device.get("hostname") or device["ip"]

        if action == "comment":
            body = self.read_json_body()
            with STATE.lock:
                STATE.devices[mac]["comment"] = str(
                    (body or {}).get("comment", ""))[:200]
                STATE.save()
            return self.send_json({"ok": True})

        if action == "restart":
            try:
                request = urllib.request.Request(
                    "http://%s/restart" % device_host(device), data=b"",
                    method="POST")
                urllib.request.urlopen(request, timeout=5).read()
            except (urllib.error.URLError, OSError) as error:
                STATE.log("warn", "api", "%s: restart failed: %s" % (name, error))
                return self.send_error_json("restart failed: %s" % error, 502)

            STATE.log("info", "api", "%s: restart requested" % name)
            return self.send_json({"ok": True})

        if action == "chime":
            try:
                status, doc = probe.post_json(
                    device_host(device), "/api/sound",
                    {"name": "door_chime", "force": True}, 5)
            except probe.DeviceUnreachable as error:
                STATE.log("warn", "api", "%s: chime failed: %s" % (name, error))
                return self.send_error_json("chime failed: %s" % error, 502)

            if status != 200:
                STATE.log("warn", "api", "%s: chime refused: %s" % (name, doc))
                return self.send_json(doc, status)

            STATE.log("info", "api", "%s: door chime queued" % name)
            return self.send_json({"ok": True, "queued": "door_chime"})

        self.send_error_json("unknown action", 404)

    def handle_device_delete(self, mac):
        with STATE.lock:
            if mac not in STATE.devices:
                return self.send_error_json("unknown device", 404)
            name = STATE.devices[mac].get("hostname") or mac
            del STATE.devices[mac]
            STATE.save()
            STATE.save_now_if_dirty()

        STATE.log("info", "api", "%s removed from the list" % name)
        self.send_json({"ok": True})

    # -- API: bulk config ---------------------------------------------------

    def handle_bulk_config(self):
        body = self.read_json_body()

        if not isinstance(body, dict) \
                or not isinstance(body.get("macs"), list) \
                or not isinstance(body.get("fields"), dict) \
                or not body["fields"]:
            return self.send_error_json(
                "expected {\"macs\": [...], \"fields\": {...}}")

        with STATE.lock:
            devices = {mac: dict(STATE.devices[mac]) for mac in body["macs"]
                       if mac in STATE.devices}

        if not devices:
            return self.send_error_json("no known devices selected")

        fields = body["fields"]

        def apply_one(item):
            mac, device = item
            name = device.get("hostname") or device["ip"]

            try:
                status, doc = probe.post_json(device_host(device),
                                              "/api/config", fields, 8)
            except probe.DeviceUnreachable as error:
                STATE.log("warn", "config", "%s: save failed: %s" % (name, error))
                return mac, {"ok": False, "error": str(error)}

            if status == 200:
                STATE.log("info", "config", "%s: applied %s"
                          % (name, ", ".join(sorted(fields))))
                return mac, {"ok": True}

            STATE.log("warn", "config", "%s: rejected %s" % (name, doc))
            return mac, {"ok": False, "error": doc}

        results = {}

        with ThreadPoolExecutor(max_workers=8) as pool:
            for mac, result in pool.map(apply_one, devices.items()):
                results[mac] = result

        self.send_json({"results": results})

    # -- API: firmware update -----------------------------------------------

    def handle_update_images(self):
        images = {}

        for target, (path, source) in updater.available_images().items():
            images[target] = {"path": path, "source": source}

        self.send_json({"targets": updater.TARGETS, "images": images,
                        "tree_version": updater.expected_version_default()})

    def handle_update_upload(self):
        """Store an uploaded .bin for the next update run. The file lands in
        data/uploads/ and is referred to by name from then on."""
        body = self.read_body()

        if not body:
            return self.send_error_json("empty upload")

        os.makedirs(UPLOAD_DIR, exist_ok=True)

        name = os.path.basename(
            self.headers.get("X-Filename") or "uploaded.bin")
        name = re.sub(r"[^A-Za-z0-9._-]", "_", name)

        path = os.path.join(UPLOAD_DIR, name)

        with open(path, "wb") as handle:
            handle.write(body)

        STATE.log("info", "update", "uploaded image %s (%d bytes)"
                  % (name, len(body)))

        self.send_json({"ok": True, "name": name, "size": len(body)})

    def handle_update(self):
        body = self.read_json_body()

        if not isinstance(body, dict) or not isinstance(body.get("macs"), list) \
                or not body["macs"]:
            return self.send_error_json("expected {\"macs\": [...]}")

        uploaded_path = None
        image_kind = body.get("image", "auto")

        if image_kind == "upload":
            name = re.sub(r"[^A-Za-z0-9._-]", "_",
                          str(body.get("name") or ""))
            uploaded_path = os.path.join(UPLOAD_DIR, name)

            if not name or not os.path.isfile(uploaded_path):
                return self.send_error_json("no uploaded image by that name")

        started, skipped = start_updates(body["macs"], image_kind,
                                         uploaded_path)

        self.send_json({"started": started, "skipped": skipped})


# ---------------------------------------------------------------------- main

def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--port", type=int, default=DEFAULT_PORT,
                        help="port to serve on (default %d)" % DEFAULT_PORT)
    parser.add_argument("--device-port", type=int, default=80,
                        help="port devices serve their web interface on "
                             "(default 80; the simulator uses 8780)")
    args = parser.parse_args()

    global DEVICE_PORT
    DEVICE_PORT = args.device_port

    stop_event = threading.Event()

    scheduler_thread = threading.Thread(target=scheduler, args=(stop_event,),
                                        daemon=True)
    scheduler_thread.start()

    server = ThreadingHTTPServer(("127.0.0.1", args.port), Handler)

    STATE.log("info", "system", "devmgr listening on http://localhost:%d"
              % args.port)

    print("devmgr: http://localhost:%d" % args.port)

    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        stop_event.set()
        STATE.save_now_if_dirty()
        server.server_close()


if __name__ == "__main__":
    main()
