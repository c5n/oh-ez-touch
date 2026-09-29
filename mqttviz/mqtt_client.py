#!/usr/bin/env python3
"""A minimal MQTT 3.1.1 client, and nothing else.

The firmware talks QoS 0 over plain TCP, so that is what this speaks:
connect, subscribe, publish, keepalive, reconnect with backoff. No TLS,
no sessions, no pipelined QoS 1 publishing -- but a QoS 1 PUBLISH that a
broker insists on sending is acknowledged, because ignoring it would
make one resend forever.

    client = MQTTClient("localhost", 1883, "mqttviz-me",
                        on_message=lambda topic, payload, retain: ...)
    client.subscribe("oheztouch/#")
    client.start()

Needs nothing but Python 3, like the rest of the tooling.
"""

import socket
import struct
import threading
import time

DEFAULT_KEEPALIVE_S = 30
FIRST_BACKOFF_S = 1.0
MAX_BACKOFF_S = 30.0


# ------------------------------------------------------------- wire encoding

def encode_remaining_length(length):
    """The variable-length prefix every packet after the header byte uses:
    seven bits per byte, most significant bit says 'more follows'."""
    out = bytearray()
    while True:
        byte = length % 0x80
        length //= 0x80
        if length:
            byte |= 0x80
        out.append(byte)
        if not length:
            return bytes(out)


def encode_string(text):
    data = text.encode("utf-8")
    return struct.pack(">H", len(data)) + data


def encode_packet(first_byte, body):
    return bytes([first_byte]) + encode_remaining_length(len(body)) + body


# -------------------------------------------------------------------- client

class MQTTClient:
    """A background thread that keeps one broker connection alive and
    hands every incoming PUBLISH to a callback. Broken connections are
    re-established with a growing backoff; subscribe()ed filters are
    replayed on every reconnect, because a clean session forgot them."""

    def __init__(self, host, port, client_id, username="", password="",
                 keepalive=DEFAULT_KEEPALIVE_S,
                 on_connect=None, on_disconnect=None, on_message=None,
                 on_error=None):
        self.host = host
        self.port = port
        self.client_id = client_id
        self.username = username or ""
        self.password = password or ""
        self.keepalive = keepalive

        self.on_connect = on_connect
        self.on_disconnect = on_disconnect
        self.on_message = on_message
        self.on_error = on_error

        self.connected = False

        self._filters = []
        self._lock = threading.Lock()
        self._sock = None
        self._stopping = False
        self._thread = None

    # -- public ----------------------------------------------------------

    def start(self):
        self._thread = threading.Thread(target=self._run, daemon=True)
        self._thread.start()

    def stop(self):
        self._stopping = True
        self._close_socket()

        if self._thread is not None:
            self._thread.join(timeout=3.0)
            self._thread = None

    def subscribe(self, topic_filter):
        with self._lock:
            if topic_filter not in self._filters:
                self._filters.append(topic_filter)

        if self.connected:
            self._send_subscriptions([topic_filter])

    def publish(self, topic, payload):
        """QoS 0, not retained: the panel's own way of publishing. Returns
        False when there is no connection to publish through."""
        if isinstance(payload, str):
            payload = payload.encode("utf-8")

        body = encode_string(topic) + payload
        return self._send(encode_packet(0x30, body))

    # -- connection handling ----------------------------------------------

    def _run(self):
        backoff = FIRST_BACKOFF_S

        while not self._stopping:
            if self._connect():
                backoff = FIRST_BACKOFF_S
                if self.on_connect:
                    self.on_connect()

                self._read_loop()

                self.connected = False
                if self.on_disconnect:
                    self.on_disconnect()

            if self._stopping:
                return

            # Not a busy error loop: a broker that is simply down is
            # retried politely, a rate that grows the longer it stays down.
            if self.on_error:
                self.on_error("reconnecting in %.0f s" % backoff)
            self._sleep(backoff)
            backoff = min(backoff * 2, MAX_BACKOFF_S)

    def _connect(self):
        try:
            sock = socket.create_connection((self.host, self.port),
                                            timeout=10.0)
        except OSError as error:
            if self.on_error:
                self.on_error("cannot reach %s:%d: %s"
                              % (self.host, self.port, error))
            return False

        flags = 0x02                                   # clean session
        payload = encode_string(self.client_id)
        if self.username:
            flags |= 0x80
            payload += encode_string(self.username)
            if self.password:
                flags |= 0x40
                payload += encode_string(self.password)

        header = encode_string("MQTT") + bytes([4, flags]) \
            + struct.pack(">H", self.keepalive)

        try:
            sock.settimeout(10.0)
            sock.sendall(encode_packet(0x10, header + payload))
            packet = self._recv_exact(sock, 4)
        except OSError as error:
            if self.on_error:
                self.on_error("connect failed: %s" % error)
            sock.close()
            return False

        if not packet or packet[0] >> 4 != 2 or packet[3] != 0:
            sock.close()
            if self.on_error:
                self.on_error("broker refused the connection")
            return False

        sock.settimeout(1.0)

        with self._lock:
            self._sock = sock

        self.connected = True
        self._send_subscriptions(self._filters)
        return True

    def _read_loop(self):
        """Blocking reads with a 1 s timeout, so the keepalive ping can
        be sent on time and a vanished broker is noticed within a
        keepalive period rather than forever."""
        buffer = bytearray()
        sock = self._sock
        last_rx = time.monotonic()
        last_ping = last_rx

        while not self._stopping:
            try:
                chunk = sock.recv(4096)
            except socket.timeout:
                chunk = b""
            except OSError:
                return

            if chunk:
                last_rx = time.monotonic()
                buffer += chunk
                buffer = self._parse(buffer)

            now = time.monotonic()
            if now - last_ping >= self.keepalive / 2:
                if not self._send(b"\xc0\x00"):
                    return
                last_ping = now
            elif now - last_rx > self.keepalive + 10:
                # The broker has been silent through a whole keepalive
                # worth of pings: it is gone, or the path to it is.
                if self.on_error:
                    self.on_error("broker went silent")
                return

    def _parse(self, buffer):
        """Every complete packet out of the buffer; partial tails stay in.
        Returns what is left over."""
        while buffer:
            length, used = self._decode_remaining_length(buffer)
            if length is None or len(buffer) < used + length:
                return buffer

            body = buffer[used:used + length]
            first = buffer[0]
            buffer = buffer[used + length:]

            kind = first >> 4

            if kind == 3:                             # PUBLISH
                qos = (first >> 1) & 0x03
                retain = bool(first & 0x01)

                topic_len = struct.unpack(">H", body[:2])[0]
                topic = body[2:2 + topic_len].decode("utf-8", "replace")
                at = 2 + topic_len

                if qos == 1:
                    packet_id = struct.unpack(">H", body[at:at + 2])[0]
                    at += 2
                    # Acknowledge, or a QoS 1 broker will resend forever.
                    self._send(encode_packet(0x40,
                                             struct.pack(">H", packet_id)))
                elif qos == 2:
                    continue                           # not spoken here

                if self.on_message:
                    self.on_message(topic, bytes(body[at:]), retain)

            # SUBACK, UNSUBACK, PINGRESP and the rest are handled by the
            # silence around them: nothing to carry forward.

        return buffer

    @staticmethod
    def _decode_remaining_length(buffer):
        length = 0
        multiplier = 1

        for i in range(1, min(len(buffer), 5)):
            byte = buffer[i]
            length += (byte & 0x7f) * multiplier
            if not byte & 0x80:
                return length, i + 1
            multiplier *= 128

        return None, 0                                 # incomplete varint

    @staticmethod
    def _recv_exact(sock, count):
        data = bytearray()
        while len(data) < count:
            chunk = sock.recv(count - len(data))
            if not chunk:
                return None
            data += chunk
        return bytes(data)

    def _send_subscriptions(self, filters):
        if not filters:
            return

        body = struct.pack(">H", 1)
        for topic_filter in filters:
            body += encode_string(topic_filter) + b"\x00"   # QoS 0

        self._send(encode_packet(0x82, body))

    def _send(self, data):
        with self._lock:
            sock = self._sock
            if sock is None:
                return False
            try:
                sock.sendall(data)
                return True
            except OSError:
                return False

    def _close_socket(self):
        with self._lock:
            if self._sock is not None:
                try:
                    self._sock.close()
                except OSError:
                    pass
                self._sock = None

    def _sleep(self, seconds):
        deadline = time.monotonic() + seconds
        while not self._stopping and time.monotonic() < deadline:
            time.sleep(0.2)
