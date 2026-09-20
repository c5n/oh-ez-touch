#!/usr/bin/env python3
"""Firmware updates for the device manager.

One worker per device, in a thread: check the device answers, stream the
image to /update with a percent to report, wait for the reboot, and then --
this is the part that matters -- read /api/status and insist on the expected
version and target. The upload POST returning 200 proves nothing: the old
firmware answers 200 even when the flash failed. Only the status page
showing what was meant to be flashed counts as a success.

The flow and the verification rule are adapted from tools/batchupdate.py,
which is the proven implementation of both.

Images come from one of two places: the repository's newest release
directory (release/X.Y/oh-ez-touch-X.Y-<target>.bin), falling back to the
build tree (build/<target>/oh-ez-touch.bin) -- or a file the user uploaded
through the manager, which then goes to every selected device regardless of
target.
"""

import http.client
import os
import re
import threading
import time

from probe import fetch_json, is_status, DeviceUnreachable

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# The targets the fleet can be made of, with the name the flashed firmware
# reports on /api/status for each. Same table as tools/batchupdate.py.
TARGETS = {
    "arduitouch": "ArduiTouch",
    "arduitouch28": "ArduiTouch28",
    "lanbon": "Lanbon",
    "cyd": "CYD",
    "arduitouch_jtag": "ArduiTouchJTAG",
}

UPLOAD_TIMEOUT_S = 30
REBOOT_WAIT_FACTOR = 12

# What a device running the minimal recovery firmware reports as its target.
# It is deliberately not in TARGETS: the recovery image is board-independent,
# so there is no per-target image to pick for it -- and no way to learn the
# board from a device running it, which is what an update would need.
MINIMAL_TARGET_NAME = "Minimal"

# The phases a device moves through, for the progress display.
PHASES = ("queued", "checking", "uploading", "waiting for reboot",
          "verifying", "PASS", "FAIL")


def expected_version_default():
    """The version this source tree builds, from main/version.h."""
    major = minor = None

    with open(os.path.join(REPO_ROOT, "main", "version.h")) as handle:
        for line in handle:
            match = re.match(r"#define\s+VERSION_MAJOR\s+(\d+)", line)
            if match:
                major = int(match.group(1))
            match = re.match(r"#define\s+VERSION_MINOR\s+(\d+)", line)
            if match:
                minor = int(match.group(1))

    if major is None or minor is None:
        return None

    return "%d.%02d" % (major, minor)


def latest_release():
    """The newest versioned directory under release/, or None."""
    release_dir = os.path.join(REPO_ROOT, "release")

    if not os.path.isdir(release_dir):
        return None

    versions = [name for name in os.listdir(release_dir)
                if re.match(r"^\d+\.\d+$", name)
                and os.path.isdir(os.path.join(release_dir, name))]

    if not versions:
        return None

    return max(versions, key=lambda name: [int(p) for p in name.split(".")])


def image_for(target):
    """Where the image for a target comes from: the newest release if it has
    one, the build tree otherwise. Returns (path, source) or (None, None)."""
    release = latest_release()

    if release is not None:
        candidate = os.path.join(REPO_ROOT, "release", release,
                                 "oh-ez-touch-%s-%s.bin" % (release, target))
        if os.path.isfile(candidate):
            return candidate, "release %s" % release

    candidate = os.path.join(REPO_ROOT, "build", target, "oh-ez-touch.bin")

    if os.path.isfile(candidate):
        return candidate, "build tree"

    return None, None


def available_images():
    """What the update dialog needs to know per target: the image path and
    where it comes from, or that there is none."""
    return {target: image_for(target) for target in TARGETS}


def _post_image(host, image, progress):
    """POST the image as multipart/form-data, streamed, with progress(
    sent, total) per chunk. Returns (status, body)."""
    boundary = "ohezdevmgr"
    head = ("--%s\r\n"
            'Content-Disposition: form-data; name="update"; filename="%s"\r\n'
            "Content-Type: application/octet-stream\r\n\r\n"
            % (boundary, os.path.basename(image))).encode("ascii")
    tail = ("\r\n--%s--\r\n" % boundary).encode("ascii")
    total = len(head) + os.path.getsize(image) + len(tail)

    connection = http.client.HTTPConnection(host, timeout=UPLOAD_TIMEOUT_S)

    try:
        connection.putrequest("POST", "/update")
        connection.putheader("Content-Type",
                             "multipart/form-data; boundary=%s" % boundary)
        connection.putheader("Content-Length", str(total))
        connection.endheaders()

        sent = 0

        def send(chunk):
            nonlocal sent
            connection.send(chunk)
            sent += len(chunk)
            progress(sent, total)

        send(head)

        with open(image, "rb") as handle:
            while True:
                chunk = handle.read(65536)
                if not chunk:
                    break
                send(chunk)

        send(tail)

        response = connection.getresponse()
        return response.status, response.read()
    finally:
        connection.close()


class DeviceUpdate(threading.Thread):
    """The whole per-device flow, in a thread. Readable state via as_dict();
    the manager polls it for the progress display."""

    def __init__(self, host, image, expect_version=None, expect_target=None,
                 timeout=10, logger=None):
        super().__init__(daemon=True)
        self.host = host
        self.image = image
        self.expect_version = expect_version
        self.expect_target = expect_target
        self.timeout = timeout
        self.logger = logger or (lambda level, source, message: None)
        self.phase = "queued"
        self.percent = 0
        self.detail = ""
        self.lock = threading.Lock()

    def _set(self, phase, percent=None, detail=None):
        with self.lock:
            self.phase = phase
            if percent is not None:
                self.percent = percent
            if detail is not None:
                self.detail = detail

    def as_dict(self):
        with self.lock:
            return {"host": self.host, "phase": self.phase,
                    "percent": self.percent, "detail": self.detail}

    def run(self):
        try:
            self._run()
        except Exception as error:  # a worker must not die silently
            self._set("FAIL", detail="internal error: %s" % error)
            self.logger("error", "update",
                        "%s: internal error: %s" % (self.host, error))

    def _run(self):
        self._set("checking")

        try:
            doc = fetch_json(self.host, "/api/status", self.timeout)
        except DeviceUnreachable as error:
            self._set("FAIL", detail="unreachable: %s" % error)
            return

        if not is_status(doc):
            self._set("FAIL", detail="not an OhEzTouch 0.91+ device")
            return

        self.logger("info", "update",
                    "%s: uploading %s" % (self.host, self.image))

        last_reported = [-1]  # a list: the closure's own cell

        def progress(sent, total):
            percent = sent * 100 // total

            if percent != last_reported[0]:
                last_reported[0] = percent
                self._set("uploading", percent=percent)

        try:
            status, body = _post_image(self.host, self.image, progress)
        except Exception as error:
            self._set("FAIL", detail="upload failed: %s" % error)
            self.logger("error", "update",
                        "%s: upload failed: %s" % (self.host, error))
            return

        if status != 200:
            reason = body.decode("utf-8", "replace").strip()
            self._set("FAIL", detail="device refused the image: %s" % reason)
            self.logger("error", "update",
                        "%s: device refused the image: %s" % (self.host, reason))
            return

        # The device restarts itself after a 200. Give it time, then insist
        # on the status showing what was meant to be flashed.
        self._set("waiting for reboot", percent=100)

        deadline = time.time() + self.timeout * REBOOT_WAIT_FACTOR

        while time.time() < deadline:
            try:
                doc = fetch_json(self.host, "/api/status", self.timeout)
            except DeviceUnreachable:
                time.sleep(1)
                continue

            self._set("verifying")

            version = doc.get("version")
            target = doc.get("target")

            version_ok = (self.expect_version is None
                          or version == self.expect_version)
            target_ok = (self.expect_target is None
                         or (target or "").strip().lower()
                         == self.expect_target.lower())

            if version_ok and target_ok:
                detail = "version %s, target %s" % (version, target)
                self._set("PASS", detail=detail)
                self.logger("info", "update", "%s: PASS -- %s"
                            % (self.host, detail))
            else:
                self._set("FAIL", detail="status shows version %s, target %s"
                          % (version, target))
                self.logger("error", "update",
                            "%s: FAIL -- status shows version %s, target %s"
                            % (self.host, version, target))
            return

        self._set("FAIL", detail="device did not come back within %d s"
                  % (self.timeout * REBOOT_WAIT_FACTOR))
        self.logger("error", "update",
                    "%s: FAIL -- did not come back" % self.host)
