#!/usr/bin/env python3
"""Drive and inspect the OhEzTouch simulator.

The simulator listens for commands on a UDP socket on the loopback interface
(main/testif/), which is how a script sends a touch, asks what is on screen,
reads the telemetry, or pulls the framebuffer. This is the front end for it:
it handles the request/reply correlation, the timeouts and the retry once, and
it turns the raw framebuffer into a PNG on this machine -- the panel never
encodes one, because the hardware has no RAM to spare for it.

    tools/ohez_ctl.py ping
    tools/ohez_ctl.py wait-page
    tools/ohez_ctl.py tap-label "Living Room"
    tools/ohez_ctl.py swipe right
    tools/ohez_ctl.py shot /tmp/panel.png

Exits non-zero when the simulator answers `err` or does not answer at all, so
it reads in a shell script the way the host test binary does.

See doc/test-interface.md for the protocol and the JSON the dumps produce.
"""

import argparse
import itertools
import json
import socket
import struct
import sys
import time
import urllib.request
import zlib

DEFAULT_HOST = "127.0.0.1"
DEFAULT_PORT = 8781
DEFAULT_WEB_PORT = 8780

# Long enough for a dump the simulator assembles inside one turn of its loop,
# short enough that a dead simulator is noticed rather than waited on.
TIMEOUT_S = 2.0


class ControlError(Exception):
    """The simulator refused a command, or never answered one."""


class Panel:
    """One simulator, addressed over UDP."""

    def __init__(self, host, port, web_port):
        self.address = (host, port)
        self.web_port = web_port
        self.host = host
        self.ids = itertools.count(1)
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.settimeout(TIMEOUT_S)

    def send(self, command, *args):
        """One request, one reply, as a payload string (possibly empty)."""
        # The id is what makes a retry safe: a reply to the request that timed
        # out is recognised and dropped rather than read as the answer to this
        # one.
        tag = next(self.ids)
        request = " ".join(["@%d" % tag, command] + [_quote(str(a)) for a in args])

        for attempt in (1, 2):
            self.sock.sendto(request.encode(), self.address)

            deadline = time.monotonic() + TIMEOUT_S

            while time.monotonic() < deadline:
                try:
                    reply = self.sock.recv(65535).decode(errors="replace").strip()
                except socket.timeout:
                    break

                if not reply.startswith("@%d " % tag):
                    continue  # a late answer to something else

                body = reply.split(" ", 1)[1]

                if body.startswith("err "):
                    raise ControlError(body[4:])
                if body == "ok":
                    return ""
                if body.startswith("ok "):
                    return body[3:]

                raise ControlError("unparseable reply: %r" % reply)

            if attempt == 2:
                raise ControlError(
                    "no answer from %s:%d -- is the simulator running?" % self.address
                )

        raise AssertionError("unreachable")

    def json(self, command, *args):
        return json.loads(self.send(command, *args))

    def screenshot_url(self):
        """Ask the panel where its framebuffer is, so the web port need not be
        guessed -- OHEZ_WEBUI_PORT is free to move it."""
        try:
            return self.send("shot")
        except ControlError:
            return "http://%s:%d/screenshot.raw" % (self.host, self.web_port)


def _quote(text):
    """Wrap a value containing spaces, which the tokeniser understands."""
    return '"%s"' % text if (" " in text or "\t" in text) else text


# --------------------------------------------------------------- framebuffer

# 5- and 6-bit channels widened to 8, by replicating the high bits into the low
# ones -- so 31 becomes 255 rather than 248 and white stays white.
_R5 = [(v << 3) | (v >> 2) for v in range(32)]
_G6 = [(v << 2) | (v >> 4) for v in range(64)]


def decode_framebuffer(blob):
    """Unpack the OHFB body into (width, height, list of RGB888 rows)."""
    if len(blob) < 16 or blob[:4] != b"OHFB":
        raise ControlError("not a framebuffer dump")

    version, fmt, width, height, stride = struct.unpack("<HHHHI", blob[4:16])

    if version != 1:
        raise ControlError("framebuffer version %d is not understood" % version)
    if fmt != 1:
        raise ControlError("framebuffer format %d is not RGB565" % fmt)

    pixels = blob[16:]

    if len(pixels) < stride * height:
        raise ControlError("framebuffer is short: %d of %d bytes"
                           % (len(pixels), stride * height))

    rows = []

    for y in range(height):
        line = pixels[y * stride:y * stride + width * 2]
        out = bytearray(width * 3)

        for x, value in enumerate(struct.unpack("<%dH" % width, line)):
            out[x * 3] = _R5[(value >> 11) & 0x1F]
            out[x * 3 + 1] = _G6[(value >> 5) & 0x3F]
            out[x * 3 + 2] = _R5[value & 0x1F]

        rows.append(bytes(out))

    return width, height, rows


def write_png(path, width, height, rows, scale=1):
    """A PNG, with nothing but zlib and struct.

    No Pillow: this script is run to check on a simulator, and a dependency
    would be one more thing to install before that can happen.
    """
    if scale > 1:
        rows = [_stretch(row, scale) for row in rows for _ in range(scale)]
        width *= scale
        height *= scale

    # Filter type 0 (none) on every scanline. The image is small and the point
    # is to look at it, not to store it well.
    raw = b"".join(b"\x00" + row for row in rows)

    def chunk(kind, data):
        body = kind + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body))

    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(raw, 6))
    png += chunk(b"IEND", b"")

    with open(path, "wb") as handle:
        handle.write(png)


def _stretch(row, scale):
    out = bytearray()

    for i in range(0, len(row), 3):
        out += row[i:i + 3] * scale

    return bytes(out)


# ------------------------------------------------------------------ commands

def find_tile(panel, label):
    """The tile whose label matches, case-insensitively."""
    tiles = panel.json("screen")["page"]["tiles"]

    for tile in tiles:
        if tile["label"].lower() == label.lower():
            return tile

    names = ", ".join(repr(t["label"]) for t in tiles) or "none"
    raise ControlError("no tile labelled %r -- on screen: %s" % (label, names))


def tap_tile(panel, tile):
    panel.send("tap", tile["x"] + tile["w"] // 2, tile["y"] + tile["h"] // 2)


def cmd_wait_page(panel, args):
    """Poll until the openHAB page has finished loading.

    The one call that keeps a script from being racy: tapping a tile before the
    page is ready reaches whatever was there before, or nothing at all.
    """
    deadline = time.monotonic() + args.timeout

    while time.monotonic() < deadline:
        state = panel.json("screen")["page"]["state"]

        if state == "ready":
            return

        time.sleep(0.1)

    raise ControlError("the page was still %r after %gs" % (state, args.timeout))


def cmd_calibrate_run(panel, args):
    """Walk the touchscreen calibration from start to result.

    The targets come from the firmware rather than from this script, so a
    layout change that moved them cannot leave a test tapping empty screen and
    passing for the wrong reason -- the same reason tap-tile reads the screen
    dump instead of naming pixels.

    Each cross is tapped until the firmware says it counted it, rather than
    tapped once and slept over. A press can be lost -- the overlay is built and
    the pointer read on the same task, and a tap that arrives in the wrong half
    of that has nothing to land on. Repeating is safe because the firmware drops
    a press that reads where the last accepted one read, so the repeat is either
    the press that went missing or a duplicate that costs nothing.

    Prints the ten numbers `calibrate result` reports: the four constants the
    panel was using, the four it would use, the worst error the old ones had at
    the four measured points, and the worst the new ones still have.
    """
    panel.send("settings", "touch")
    panel.send("calibrate")

    numbers = [int(n) for n in panel.send("calibrate", "targets").split()]
    targets = list(zip(numbers[0::2], numbers[1::2]))

    for index, (x, y) in enumerate(targets):
        deadline = time.monotonic() + args.timeout

        while True:
            panel.send("tap", x, y)

            # Poll for a while before pressing again, rather than pressing on
            # every tick: the fourth cross is replaced by the result screen in
            # the same frame it is counted, and a press that arrives after that
            # lands on a button. The firmware ignores those for a moment
            # (RESULT_GRACE_MS) so this cannot store a calibration nobody
            # looked at -- but there is no reason to send them either.
            settled = time.monotonic() + args.poll

            while time.monotonic() < settled:
                if int(panel.send("calibrate", "step").split()[0]) > index:
                    break

                time.sleep(0.02)

            if int(panel.send("calibrate", "step").split()[0]) > index:
                break

            if time.monotonic() > deadline:
                raise ControlError("target %d of %d never registered"
                                   % (index + 1, len(targets)))

    return panel.send("calibrate", "result")


def cmd_shot(panel, args):
    url = panel.screenshot_url()

    with urllib.request.urlopen(url, timeout=TIMEOUT_S) as response:
        blob = response.read()

    if args.raw:
        with open(args.path, "wb") as handle:
            handle.write(blob)
        print("%s: %d bytes, as fetched" % (args.path, len(blob)))
        return

    width, height, rows = decode_framebuffer(blob)

    write_png(args.path, width, height, rows, args.scale)

    print("%s: %dx%d" % (args.path, width * args.scale, height * args.scale))


def main():
    parser = argparse.ArgumentParser(
        description=__doc__.splitlines()[0],
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument("--host", default=DEFAULT_HOST)
    parser.add_argument("--port", type=int, default=DEFAULT_PORT,
                        help="the control socket (default %d)" % DEFAULT_PORT)
    parser.add_argument("--web-port", type=int, default=DEFAULT_WEB_PORT,
                        help="fallback for the framebuffer, if the panel will "
                             "not say (default %d)" % DEFAULT_WEB_PORT)

    sub = parser.add_subparsers(dest="command", required=True)

    shot = sub.add_parser("shot", help="save the panel's screen as a PNG")
    shot.add_argument("path")
    shot.add_argument("--scale", type=int, default=1,
                      help="repeat each pixel N times, as the SDL window does")
    shot.add_argument("--raw", action="store_true",
                      help="save the framebuffer dump unconverted")

    wait = sub.add_parser("wait-page", help="block until the page has loaded")
    wait.add_argument("--timeout", type=float, default=5.0)

    label = sub.add_parser("tap-label", help="tap the tile with this label")
    label.add_argument("label")

    index = sub.add_parser("tap-tile", help="tap the tile with this index")
    index.add_argument("index", type=int)

    calrun = sub.add_parser("calibrate-run",
                            help="tap all four calibration targets and print the result")
    calrun.add_argument("--poll", type=float, default=0.5,
                        help="seconds to wait for a tap to count before "
                             "pressing again (default 0.5)")
    calrun.add_argument("--timeout", type=float, default=5.0,
                        help="how long to wait for one target (default 5)")

    # Everything else goes through untouched, so a command added to the
    # firmware is usable here without editing this file.
    passthrough = sub.add_parser("send", help="send a raw command line")
    passthrough.add_argument("words", nargs=argparse.REMAINDER)

    for name in ("ping", "screen", "status", "config", "set", "tap", "longpress",
                 "swipe", "press", "move", "release", "nav", "settings", "calibrate",
                 "quit"):
        direct = sub.add_parser(name)
        direct.add_argument("words", nargs=argparse.REMAINDER)

    args = parser.parse_args()
    panel = Panel(args.host, args.port, args.web_port)

    try:
        if args.command == "shot":
            cmd_shot(panel, args)
        elif args.command == "wait-page":
            cmd_wait_page(panel, args)
        elif args.command == "tap-label":
            tap_tile(panel, find_tile(panel, args.label))
        elif args.command == "calibrate-run":
            print(cmd_calibrate_run(panel, args))
        elif args.command == "tap-tile":
            tiles = panel.json("screen")["page"]["tiles"]

            if args.index >= len(tiles):
                raise ControlError("there are %d tiles" % len(tiles))

            tap_tile(panel, tiles[args.index])
        else:
            words = args.words
            command = args.words if args.command == "send" else [args.command] + words

            payload = panel.send(command[0], *command[1:])

            if payload.startswith("{"):
                print(json.dumps(json.loads(payload), indent=2))
            elif payload:
                print(payload)
    except ControlError as error:
        sys.exit("ohez_ctl: %s" % error)
    except OSError as error:
        sys.exit("ohez_ctl: %s" % error)


if __name__ == "__main__":
    main()
