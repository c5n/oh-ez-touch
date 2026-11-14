"""The fleet log: what every panel reports, recorded for later analysis.

Two recorders feed one store:

* MQTT. Every panel publishes its system values and a last-will status
  under <prefix>/<hostname>/ (see doc/mqtt.md). A minimal MQTT 3.1.1
  subscriber, QoS 0 only, takes <prefix>/+/system/# and <prefix>/+/status.
  The panels push, so recording costs them nothing.
* HTTP and ping. Every probe interval, each panel known from MQTT gets one
  GET /api/status and one ICMP echo. That is the view from outside: a
  panel that still publishes but no longer answers, which is what a starved
  heap looks like, shows up only here.

Every record is one JSON line in data/history/YYYY-MM-DD.jsonl, local date,
appended and never rewritten:

    {"t": 1794628832.8, "src": "mqtt", "dev": "panel-01", "topic": "system/rssi", "v": "-57"}
    {"t": 1794628890.1, "src": "http", "dev": "panel-01", "ok": true, "ms": 41,
     "ping": true, "status": {...the device's /api/status...}}

The files are the record; nothing deletes them. The store keeps the last
RETENTION_S of them in memory for the page, loaded from the files at start
and extended live. data/history/devices.json lists every panel ever seen,
with its last address, board and firmware.

Needs nothing but Python 3.
"""

import glob
import json
import os
import socket
import struct
import subprocess
import threading
import time
import urllib.request
from collections import defaultdict, deque
from concurrent.futures import ThreadPoolExecutor

RETENTION_S = 3 * 24 * 3600

# Topics kept as a series of numbers. Everything else under system/ is a
# string, and only its changes are kept (the BSSID, the firmware).
NUMERIC = {
    "system/uptime", "system/heap", "system/heapblock", "system/rssi",
    "system/quality", "system/fps", "system/render_us", "system/wait_us",
    "system/wlan_channel", "system/wlan_online", "system/wlan_disconnects",
    "system/wlan_reason", "system/wlan_roams",
}

# Series whose dips matter more than their mean: downsampled by minimum.
MIN_BUCKET = {"system/heap", "system/heapblock", "system/rssi"}

# The 802.11 reason codes as ESP-IDF names them (wifi_err_reason_t).
WIFI_REASONS = {
    1: "UNSPECIFIED", 2: "AUTH_EXPIRE", 3: "AUTH_LEAVE",
    4: "DISASSOC_DUE_TO_INACTIVITY", 5: "ASSOC_TOOMANY",
    6: "CLASS2_FRAME_FROM_NONAUTH_STA", 7: "CLASS3_FRAME_FROM_NONASSOC_STA",
    8: "ASSOC_LEAVE", 9: "ASSOC_NOT_AUTHED", 12: "BSS_TRANSITION_DISASSOC",
    14: "MIC_FAILURE", 15: "4WAY_HANDSHAKE_TIMEOUT",
    16: "GROUP_KEY_UPDATE_TIMEOUT", 23: "802_1X_AUTH_FAILED",
    34: "MISSING_ACKS", 36: "STA_LEAVING", 39: "TIMEOUT",
    46: "PEER_INITIATED", 47: "AP_INITIATED",
    200: "BEACON_TIMEOUT", 201: "NO_AP_FOUND", 202: "AUTH_FAIL",
    203: "ASSOC_FAIL", 204: "HANDSHAKE_TIMEOUT", 205: "CONNECTION_FAIL",
    206: "AP_TSF_RESET", 207: "ROAMING", 208: "ASSOC_COMEBACK_TIME_TOO_LONG",
    209: "SA_QUERY_TIMEOUT", 210: "NO_AP_FOUND_W_COMPATIBLE_SECURITY",
    211: "NO_AP_FOUND_IN_AUTHMODE_THRESHOLD",
    212: "NO_AP_FOUND_IN_RSSI_THRESHOLD",
}


class Store:
    """The in-memory view of the last RETENTION_S, per device."""

    def __init__(self):
        self.lock = threading.Lock()
        self.series = defaultdict(lambda: defaultdict(deque))   # dev -> topic -> (t, num)
        self.changes = defaultdict(lambda: defaultdict(deque))  # dev -> topic -> (t, str)
        self.last = defaultdict(dict)                           # dev -> topic -> (t, value)
        self.http = defaultdict(deque)                          # dev -> (t, ok, ms, ping)
        self.http_status = {}                                   # dev -> last /api/status
        self.reboots = defaultdict(deque)                       # dev -> t
        self.first_seen = {}
        self.last_seen = {}

    def add(self, rec):
        t = rec.get("t")
        dev = rec.get("dev")
        if not isinstance(t, (int, float)) or not dev:
            return

        with self.lock:
            self.first_seen.setdefault(dev, t)
            self.last_seen[dev] = max(t, self.last_seen.get(dev, 0))

            if rec.get("src") == "http":
                self.http[dev].append((t, bool(rec.get("ok")), rec.get("ms"),
                                       rec.get("ping")))
                if rec.get("ok") and isinstance(rec.get("status"), dict):
                    self.http_status[dev] = (t, rec["status"])
                return

            topic = rec.get("topic", "")
            value = rec.get("v", "")

            if topic in NUMERIC:
                try:
                    num = float(value)
                except (TypeError, ValueError):
                    return

                # An uptime that went down is a restart, dated by the new
                # uptime rather than by when the message arrived.
                if topic == "system/uptime":
                    previous = self.last[dev].get(topic)
                    if previous is not None and num < float(previous[1]):
                        self.reboots[dev].append(t - num)

                self.series[dev][topic].append((t, num))
            else:
                previous = self.last[dev].get(topic)
                if previous is None or previous[1] != value:
                    self.changes[dev][topic].append((t, value))

            self.last[dev][topic] = (t, value)

    def prune(self, now):
        limit = now - RETENTION_S
        with self.lock:
            for per_dev in (self.series, self.changes):
                for topics in per_dev.values():
                    for q in topics.values():
                        # Keep one change older than the window: it is the
                        # value in effect at the window's start.
                        while len(q) > 1 and q[1][0] < limit:
                            q.popleft()
            for q in list(self.http.values()) + list(self.reboots.values()):
                while q and (q[0][0] if isinstance(q[0], tuple) else q[0]) < limit:
                    q.popleft()


def downsample(points, t0, t1, buckets, by_min):
    """At most `buckets` points between t0 and t1: the minimum or the mean
    per bucket. Points are (t, value), sorted."""
    inside = [p for p in points if t0 <= p[0] <= t1]
    if len(inside) <= buckets:
        return [[round(t, 1), v] for t, v in inside]

    width = (t1 - t0) / buckets
    out = []
    bucket = None
    acc = []

    def flush():
        if not acc:
            return
        values = [v for _, v in acc]
        value = min(values) if by_min else sum(values) / len(values)
        out.append([round(acc[len(acc) // 2][0], 1), round(value, 2)])

    for t, v in inside:
        index = int((t - t0) / width)
        if index != bucket:
            flush()
            acc = []
            bucket = index
        acc.append((t, v))
    flush()

    return out


class MqttSubscriber:
    """MQTT 3.1.1, QoS 0, subscribe only: CONNECT, SUBSCRIBE, PUBLISH in,
    PINGREQ. Calls on_message(topic, payload, retained) from its thread and
    reconnects by itself."""

    KEEPALIVE_S = 60

    def __init__(self, host, port, topics, on_message, on_state):
        self.host = host
        self.port = port
        self.topics = topics
        self.on_message = on_message
        self.on_state = on_state
        self.stop_event = threading.Event()
        self.sock = None

    def stop(self):
        self.stop_event.set()
        sock = self.sock
        if sock is not None:
            try:
                sock.close()
            except OSError:
                pass

    @staticmethod
    def _packet(header, body):
        length = len(body)
        encoded = bytearray()
        while True:
            byte = length % 128
            length //= 128
            if length:
                byte |= 0x80
            encoded.append(byte)
            if not length:
                break
        return bytes([header]) + bytes(encoded) + body

    def _recv_exact(self, n):
        data = b""
        while len(data) < n:
            chunk = self.sock.recv(n - len(data))
            if not chunk:
                raise ConnectionError("broker closed the connection")
            data += chunk
        return data

    def _read_packet(self):
        header = self._recv_exact(1)[0]
        multiplier, length = 1, 0
        while True:
            byte = self._recv_exact(1)[0]
            length += (byte & 0x7F) * multiplier
            if not byte & 0x80:
                break
            multiplier *= 128
        return header, self._recv_exact(length) if length else b""

    def _session(self):
        self.sock = socket.create_connection((self.host, self.port), timeout=10)
        client_id = ("devmgr-%d-%d" % (os.getpid(), int(time.time()))).encode()
        variable = b"\x00\x04MQTT\x04\x02" + struct.pack(">H", self.KEEPALIVE_S)
        self.sock.sendall(self._packet(0x10, variable + struct.pack(">H", len(client_id)) + client_id))

        header, body = self._read_packet()
        if header >> 4 != 2 or len(body) < 2 or body[1] != 0:
            raise ConnectionError("broker refused the connection")

        payload = struct.pack(">H", 1)
        for topic in self.topics:
            encoded = topic.encode()
            payload += struct.pack(">H", len(encoded)) + encoded + b"\x00"
        self.sock.sendall(self._packet(0x82, payload))

        self.on_state(True)
        self.sock.settimeout(self.KEEPALIVE_S / 2)

        while not self.stop_event.is_set():
            try:
                header, body = self._read_packet()
            except socket.timeout:
                self.sock.sendall(b"\xc0\x00")
                continue

            if header >> 4 != 3:
                continue

            qos = (header >> 1) & 3
            topic_len = struct.unpack(">H", body[:2])[0]
            topic = body[2:2 + topic_len].decode("utf-8", "replace")
            start = 2 + topic_len + (2 if qos else 0)
            payload = body[start:].decode("utf-8", "replace")
            self.on_message(topic, payload, bool(header & 1))

    def run(self):
        backoff = 2
        while not self.stop_event.is_set():
            try:
                self._session()
                backoff = 2
            except Exception:                               # noqa: BLE001 - any failure reconnects
                pass
            finally:
                self.on_state(False)
                if self.sock is not None:
                    try:
                        self.sock.close()
                    except OSError:
                        pass
                    self.sock = None
            self.stop_event.wait(backoff)
            backoff = min(backoff * 2, 60)


class Recorder:
    """Owns the store, the files and both recorders."""

    def __init__(self, data_dir, log, device_port=80):
        self.dir = os.path.join(data_dir, "history")
        self.log = log
        self.device_port = device_port
        self.store = Store()
        self.file_lock = threading.Lock()
        self.mqtt = None
        self.mqtt_thread = None
        self.mqtt_connected = False
        self.config = {}
        self.stop_event = threading.Event()
        self.lines_written = 0
        os.makedirs(self.dir, exist_ok=True)
        self._load_files()

    # -- files -----------------------------------------------------------

    def _load_files(self):
        limit = time.time() - RETENTION_S
        loaded = 0
        for path in sorted(glob.glob(os.path.join(self.dir, "*.jsonl"))):
            day_end = os.path.getmtime(path)
            if day_end < limit:
                continue
            with open(path, encoding="utf-8", errors="replace") as handle:
                for line in handle:
                    try:
                        rec = json.loads(line)
                    except ValueError:
                        continue
                    if rec.get("t", 0) >= limit:
                        self.store.add(rec)
                        loaded += 1
        self.log("info", "history", "loaded %d records of the last %d h"
                 % (loaded, RETENTION_S // 3600))

    def write(self, rec):
        self.store.add(rec)
        path = os.path.join(self.dir, time.strftime("%Y-%m-%d", time.localtime(rec["t"])) + ".jsonl")
        line = json.dumps(rec, separators=(", ", ": ")) + "\n"
        with self.file_lock:
            with open(path, "a", encoding="utf-8") as handle:
                handle.write(line)
            self.lines_written += 1

    def save_device_list(self):
        devices = {}
        with self.store.lock:
            for dev in self.store.last_seen:
                last = self.store.last.get(dev, {})
                devices[dev] = {
                    "first_seen": round(self.store.first_seen.get(dev, 0)),
                    "last_seen": round(self.store.last_seen.get(dev, 0)),
                    "ip": last.get("system/ip", (0, ""))[1],
                    "target": last.get("system/target", (0, ""))[1],
                    "version": last.get("system/version", (0, ""))[1],
                    "build": last.get("system/build", (0, ""))[1],
                    "git": last.get("system/git", (0, ""))[1],
                    "bssid": last.get("system/bssid", (0, ""))[1],
                }
        tmp = os.path.join(self.dir, "devices.json.tmp")
        with open(tmp, "w") as handle:
            json.dump(devices, handle, indent=2, sort_keys=True)
            handle.write("\n")
        os.replace(tmp, os.path.join(self.dir, "devices.json"))

    # -- recorders -------------------------------------------------------

    def configure(self, settings):
        """Start, restart or stop the recorders to match the settings."""
        wanted = {
            "host": (settings.get("mqtt_host") or "").strip(),
            "port": int(settings.get("mqtt_port") or 1883),
            "prefix": (settings.get("mqtt_prefix") or "oheztouch").strip().strip("/"),
            "probe_s": int(settings.get("history_probe_s") or 60),
        }
        if wanted == self.config:
            return
        restart_mqtt = {k: wanted[k] for k in ("host", "port", "prefix")} != \
                       {k: self.config.get(k) for k in ("host", "port", "prefix")}
        self.config = wanted

        if restart_mqtt:
            if self.mqtt is not None:
                self.mqtt.stop()
                self.mqtt = None
            if wanted["host"]:
                prefix = wanted["prefix"]
                self.mqtt = MqttSubscriber(
                    wanted["host"], wanted["port"],
                    ["%s/+/system/#" % prefix, "%s/+/status" % prefix],
                    self._on_message, self._on_mqtt_state)
                self.mqtt_thread = threading.Thread(target=self.mqtt.run, daemon=True)
                self.mqtt_thread.start()
                self.log("info", "history", "recording %s/+ from %s:%d"
                         % (prefix, wanted["host"], wanted["port"]))

    def _on_mqtt_state(self, connected):
        if connected != self.mqtt_connected:
            self.mqtt_connected = connected
            self.log("info" if connected else "warn", "history",
                     "MQTT %s" % ("connected" if connected else "disconnected"))

    def _on_message(self, topic, payload, retained):
        # A retained message is the broker's memory, not something that
        # happened now: it would date an old "offline" to this second.
        if retained:
            return
        parts = topic.split("/", 2)
        if len(parts) < 3:
            return
        self.write({"t": round(time.time(), 3), "src": "mqtt", "dev": parts[1],
                    "topic": parts[2], "v": payload})

    def _probe_one(self, dev, ip):
        ping_ok = subprocess.call(["ping", "-c", "1", "-W", "2", ip],
                                  stdout=subprocess.DEVNULL,
                                  stderr=subprocess.DEVNULL) == 0
        start = time.time()
        rec = {"t": round(start, 3), "src": "http", "dev": dev, "ok": False,
               "ms": None, "ping": ping_ok}
        host = ip if self.device_port == 80 else "%s:%d" % (ip, self.device_port)
        try:
            with urllib.request.urlopen("http://%s/api/status" % host, timeout=4) as resp:
                status = json.loads(resp.read().decode("utf-8", "replace"))
            rec["ok"] = True
            rec["ms"] = int((time.time() - start) * 1000)
            rec["status"] = status
        except Exception:                                   # noqa: BLE001 - recorded as a failure
            pass
        self.write(rec)

    def run(self):
        """The probe and housekeeping loop, on its own thread."""
        last_probe = 0
        last_list = 0
        while not self.stop_event.is_set():
            now = time.time()
            probe_s = self.config.get("probe_s", 60)
            if self.config.get("host") and now - last_probe >= probe_s:
                last_probe = now
                with self.store.lock:
                    targets = {dev: last["system/ip"][1]
                               for dev, last in self.store.last.items()
                               if last.get("system/ip", (0, ""))[1]
                               and now - self.store.last_seen.get(dev, 0) < 24 * 3600}
                if targets:
                    with ThreadPoolExecutor(max_workers=16) as pool:
                        for dev, ip in targets.items():
                            pool.submit(self._probe_one, dev, ip)
            if now - last_list >= 60:
                last_list = now
                self.store.prune(now)
                try:
                    self.save_device_list()
                except OSError as error:
                    self.log("error", "history", "device list: %s" % error)
            self.stop_event.wait(1.0)

    # -- the page's API --------------------------------------------------

    def ap_label_map(self):
        """Every BSSID seen, numbered in the order of the sorted addresses so
        the numbering stays put when a panel moves."""
        with self.store.lock:
            seen = set()
            for topics in self.store.changes.values():
                for t, v in topics.get("system/bssid", ()):
                    if v:
                        seen.add(v)
        return {bssid: index for index, bssid in enumerate(sorted(seen))}

    def summary(self, hours):
        now = time.time()
        t0 = now - hours * 3600
        aps = self.ap_label_map()
        out = []
        with self.store.lock:
            for dev in sorted(self.store.last_seen):
                last = self.store.last.get(dev, {})

                def num(topic):
                    value = last.get(topic)
                    try:
                        return float(value[1]) if value else None
                    except ValueError:
                        return None

                http = [h for h in self.store.http.get(dev, ()) if h[0] >= t0]
                http_last = self.store.http[dev][-1] if self.store.http.get(dev) else None
                status = last.get("status")
                block_series = [v for t, v in self.store.series[dev].get("system/heapblock", ()) if t >= t0]
                offline_events = sum(1 for t, v in self.store.changes[dev].get("status", ())
                                     if t >= t0 and v == "offline")
                bssid = last.get("system/bssid", (0, ""))[1]
                out.append({
                    "dev": dev,
                    "status": status[1] if status else None,
                    "status_t": status[0] if status else None,
                    "last_seen": self.store.last_seen.get(dev),
                    "target": last.get("system/target", (0, ""))[1],
                    "version": last.get("system/version", (0, ""))[1],
                    "build": last.get("system/build", (0, ""))[1],
                    "git": last.get("system/git", (0, ""))[1][:7],
                    "ip": last.get("system/ip", (0, ""))[1],
                    "uptime": num("system/uptime"),
                    "uptime_t": last.get("system/uptime", (None,))[0],
                    "heap": num("system/heap"),
                    "heapblock": num("system/heapblock"),
                    "heapblock_min": min(block_series) if block_series else None,
                    "rssi": num("system/rssi"),
                    "bssid": bssid,
                    "ap": aps.get(bssid),
                    "channel": num("system/wlan_channel"),
                    "disconnects": num("system/wlan_disconnects"),
                    "reason": num("system/wlan_reason"),
                    "reason_name": WIFI_REASONS.get(int(num("system/wlan_reason") or 0), ""),
                    "roams": num("system/wlan_roams"),
                    "wlan_online": num("system/wlan_online"),
                    "fps": num("system/fps"),
                    "reboots": sum(1 for t in self.store.reboots.get(dev, ()) if t >= t0),
                    "offline_events": offline_events,
                    "http_ok": http_last[1] if http_last else None,
                    "http_ms": http_last[2] if http_last else None,
                    "ping_ok": http_last[3] if http_last else None,
                    "http_rate": (sum(1 for h in http if h[1]) / len(http)) if http else None,
                    "spark": downsample(list(self.store.series[dev].get("system/heapblock", ())),
                                        t0, now, 60, True),
                })
        return {
            "now": now,
            "devices": out,
            "aps": {bssid: {"index": i} for bssid, i in aps.items()},
            "recording": {
                "mqtt_host": self.config.get("host", ""),
                "mqtt_connected": self.mqtt_connected,
                "probe_s": self.config.get("probe_s", 60),
                "lines_written": self.lines_written,
                "dir": self.dir,
            },
        }

    def detail(self, dev, hours):
        now = time.time()
        t0 = now - hours * 3600
        with self.store.lock:
            series = {topic: downsample(list(points), t0, now, 500, topic in MIN_BUCKET)
                      for topic, points in self.store.series.get(dev, {}).items()}
            changes = {}
            for topic, points in self.store.changes.get(dev, {}).items():
                points = list(points)
                # The value in effect at t0, then every change inside.
                before = [p for p in points if p[0] < t0][-1:]
                changes[topic] = [[round(t, 1), v] for t, v in before + [p for p in points if p[0] >= t0]]
            http = [[round(t, 1), ok, ms, ping] for t, ok, ms, ping in self.store.http.get(dev, ()) if t >= t0]
            reboots = [round(t, 1) for t in self.store.reboots.get(dev, ()) if t >= t0]
            status = self.store.http_status.get(dev)
        return {
            "dev": dev, "t0": t0, "t1": now,
            "series": series, "changes": changes, "http": http,
            "reboots": reboots, "aps": self.ap_label_map(),
            "reasons": {str(k): v for k, v in WIFI_REASONS.items()},
            "http_status": status[1] if status else None,
        }
