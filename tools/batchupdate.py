#!/usr/bin/env python3
"""OTA batch updater for OhEzTouch devices.

Reads a JSON device list and flashes every device in it via the firmware
update web interface. The list says per device which hardware target it is,
because devices below 0.90 cannot report that themselves; discover.py
produces such a list from a network scan and leaves the targets to fill in.

    batchupdate.py myOhEzTouchDevices.json
    batchupdate.py -t lanbon oheztouch-01 oheztouch-02 10.0.0.42
    batchupdate.py devices.json retry.json     # several lists at once

Per device the script checks the firmware image exists, checks the device
answers HTTP, POSTs the image to /update, and then -- this is the part that
matters -- waits for the device to come back and reads the status page: the
old firmware answers HTTP 200 even when the flash failed, so the POST
succeeding proves nothing. Only a status page showing the expected version
and target counts as a success.

With --interactive the devices are walked one by one in list order and each
one asks before anything is sent: yes updates it, skip leaves it for the
retry file, abort stops the run. Devices the list marks as current are never
asked about.

While it runs, the terminal shows one live line per device in flight --
checking, uploading with percent, waiting for the reboot, verifying -- above
a progress bar. Piped into a file, every state change is a plain line
instead, because escape sequences in a log are not a nicer layout.

Failed devices are written to a retry file that is a valid input list again,
so a re-run is: batchupdate.py retry.json
"""

import argparse
import http.client
import json
import os
import re
import sys
import threading
import time
from concurrent.futures import ThreadPoolExecutor, as_completed
from urllib.error import URLError
from urllib.request import urlopen

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# The targets the fleet can be made of, with the name the flashed firmware
# prints on its status page for each. Old PlatformIO names map onto the first
# two. discover.py imports TARGETS and VERSION_RE from here.
TARGETS = {
    "arduitouch": "ArduiTouch",
    "arduitouch28": "ArduiTouch28",
    "lanbon": "Lanbon",
    "arduitouch_jtag": "ArduiTouchJTAG",
}
TARGET_ALIASES = {
    "arduitouch28_4mb": "arduitouch28",
    "lanbon_l8": "lanbon",
}

VERSION_RE = re.compile(r"<td>Version</td><td>(\d+\.\d+)</td>")
TARGET_NAME_RE = re.compile(r"<td>Target</td><td>([^<]*)</td>")


def http_get(host, port, path, timeout):
    """One GET, with redirects (AutoConnect's / loves 302) followed."""
    return urlopen("http://%s:%s%s" % (host, port, path),
                   timeout=timeout).read().decode("utf-8", "replace")


def post_multipart(host, port, path, filename, timeout, progress=None):
    """POST a file as multipart/form-data. Returns (status, body).

    Sent streaming, in chunks, with the file read from disk as it goes: the
    image is 1.7 MB and the upload is the one phase worth watching, so
    progress(sent, total) is called per chunk.
    """
    boundary = "ohezbatchupdate"
    head = ("--%s\r\n"
            'Content-Disposition: form-data; name="update"; filename="%s"\r\n'
            "Content-Type: application/octet-stream\r\n\r\n"
            % (boundary, os.path.basename(filename))).encode("ascii")
    tail = ("\r\n--%s--\r\n" % boundary).encode("ascii")
    total = len(head) + os.path.getsize(filename) + len(tail)

    connection = http.client.HTTPConnection(host, port, timeout=timeout)
    try:
        connection.putrequest("POST", path)
        connection.putheader("Content-Type",
                             "multipart/form-data; boundary=%s" % boundary)
        connection.putheader("Content-Length", str(total))
        connection.endheaders()

        sent = 0

        def send(chunk):
            nonlocal sent
            connection.send(chunk)
            sent += len(chunk)
            if progress is not None:
                progress(sent, total)

        send(head)
        with open(filename, "rb") as handle:
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


def check_target(target, page_target):
    """The status page's target names differ from the build directory names."""
    if page_target is None:
        return False
    return page_target.strip().lower() == TARGETS[target].lower()


def current_result(device, expect_version):
    """A CURRENT result when the list says the device runs the expected
    version already, else None. Those devices flash nothing -- this is what
    makes re-running a fleet list after a partial rollout cheap."""
    if device.get("version") is not None and device["version"] == expect_version:
        return {"host": device["host"], "target": device["target"],
                "status": "CURRENT", "detail": "already at %s" % expect_version}
    return None


def update_device(device, args, image, report):
    """The whole per-device flow. report(text) is called on every phase
    change; returns a result dict."""
    host = device["host"]
    target = device["target"]
    result = {"host": host, "target": target, "status": "FAIL", "detail": ""}

    already = current_result(device, args.expect_version)
    if already is not None:
        return already

    if image is None:
        result["status"] = "SKIP"
        result["detail"] = "no build image for target %s" % target
        return result

    host_name, port = host.split(":", 1) if ":" in host else (host, "80")

    report("checking")
    try:
        http_get(host_name, port, "/", args.timeout)
    except (OSError, URLError) as error:
        result["detail"] = "unreachable: %s" % error
        return result

    if args.dry_run:
        result["status"] = "REACHABLE"
        return result

    status = None
    for attempt in range(1, args.retries + 2):
        if attempt > 1:
            report("retrying upload (attempt %d/%d)"
                   % (attempt, args.retries + 1))
        try:
            status, body = post_multipart(
                host_name, int(port), "/update", image, args.timeout,
                progress=lambda sent, total:
                    report("uploading %d%%" % (sent * 100 // total)))
        except Exception as error:
            if attempt <= args.retries:
                continue
            result["detail"] = "upload failed: %s" % error
            return result
        else:
            if status == 200:
                break
            if status == 500 and attempt <= args.retries:
                continue
            result["detail"] = "device refused the image: %s" \
                % body.decode("utf-8", "replace").strip()
            return result

    if args.no_verify:
        result["status"] = "UPLOADED"
        result["detail"] = "not verified"
        return result

    # The device restarts itself after a 200. Give it time, then insist on
    # the status page showing what we meant to flash.
    report("waiting for reboot")
    deadline = time.time() + args.timeout * 12
    while time.time() < deadline:
        try:
            page = http_get(host_name, port, "/", args.timeout)
        except (OSError, URLError):
            time.sleep(1)
            continue

        report("verifying")
        version_match = VERSION_RE.search(page)
        target_match = TARGET_NAME_RE.search(page)
        version = version_match.group(1) if version_match else None
        target_name = target_match.group(1) if target_match else None
        if version == args.expect_version and check_target(target, target_name):
            result["status"] = "PASS"
            result["detail"] = "version %s, target %s" % (version, target_name)
        elif version is None or target_name is None:
            result["detail"] = ("status page shows no version/target -- "
                                "still the old firmware?")
        else:
            result["detail"] = "status page shows version %s, target %s" \
                % (version, target_name)
        return result

    result["detail"] = "device did not come back within %d s" % (args.timeout * 12)
    return result


def load_devices(paths):
    """Read and validate the device list(s). Exits the process on any
    problem: better to fix the file than to skip devices silently."""
    devices = []
    for path in paths:
        try:
            with open(path) as handle:
                data = json.load(handle)
        except (OSError, ValueError) as error:
            sys.exit("cannot read %s: %s" % (path, error))
        if not isinstance(data, dict) or not isinstance(data.get("devices"), list):
            sys.exit('%s: expected { "devices": [ ... ] }' % path)
        devices.extend(data["devices"])

    problems = []
    for index, device in enumerate(devices):
        where = "device %d" % (index + 1)
        if not isinstance(device, dict) or not device.get("host"):
            problems.append("%s: missing host" % where)
            continue
        target = device.get("target")
        if target is None:
            problems.append('%s: no target set -- fill in the hardware target '
                            'before updating' % device["host"])
            continue
        lowered = str(target).lower()
        if lowered in TARGET_ALIASES:
            print("note: %s: target '%s' is now called '%s'"
                  % (device["host"], target, TARGET_ALIASES[lowered]))
            device["target"] = TARGET_ALIASES[lowered]
        elif lowered not in TARGETS:
            problems.append("%s: unknown target '%s' (known: %s)"
                            % (device["host"], target, ", ".join(sorted(TARGETS))))
        else:
            device["target"] = lowered
        version = device.get("version")
        if version is not None and not re.match(r"^\d+\.\d+$", str(version)):
            problems.append('%s: version must look like "0.90", got %r'
                            % (device["host"], version))
    if problems:
        for problem in problems:
            print("error: %s" % problem)
        sys.exit(2)
    if not devices:
        sys.exit("no devices given")
    return devices


def expected_version_default():
    """Version this source tree builds, from main/version.h."""
    header = os.path.join(REPO_ROOT, "main", "version.h")
    major = minor = None
    with open(header) as handle:
        for line in handle:
            match = re.match(r"#define\s+VERSION_MAJOR\s+(\d+)", line)
            if match:
                major = int(match.group(1))
            match = re.match(r"#define\s+VERSION_MINOR\s+(\d+)", line)
            if match:
                minor = int(match.group(1))
    if major is None or minor is None:
        sys.exit("cannot read the version from %s" % header)
    return "%d.%02d" % (major, minor)


def latest_release():
    """The newest versioned directory under release/, or None."""
    release_dir = os.path.join(REPO_ROOT, "release")
    if not os.path.isdir(release_dir):
        return None
    versions = []
    for name in os.listdir(release_dir):
        if re.match(r"^\d+\.\d+$", name) \
                and os.path.isdir(os.path.join(release_dir, name)):
            versions.append(name)
    if not versions:
        return None
    # Version comparison, not modification time -- a rebuilt older release
    # must not jump the queue.
    return max(versions, key=lambda name: [int(part) for part in name.split(".")])


def image_for(target, release):
    """Where the image for a target comes from: the chosen release if it has
    one, the build tree otherwise. Returns (path, source) or (None, None)."""
    if release is not None:
        candidate = os.path.join(REPO_ROOT, "release", release,
                                 "oh-ez-touch-%s-%s.bin" % (release, target))
        if os.path.isfile(candidate):
            return candidate, "release %s" % release
    candidate = os.path.join(REPO_ROOT, "build", target, "oh-ez-touch.bin")
    if os.path.isfile(candidate):
        return candidate, "build tree"
    return None, None


def ask_operator():
    """The one decision an interactive run asks per device: update it, leave
    it for later, or stop the run. An answer that never comes -- EOF, Ctrl-C
    -- is an abort, so a run away from its keyboard does not hang."""
    while True:
        try:
            answer = input("    update? [y]es/[s]kip/[a]bort: ").strip().lower()
        except (EOFError, KeyboardInterrupt):
            print()
            return "abort"
        if answer in ("y", "yes"):
            return "yes"
        if answer in ("s", "skip"):
            return "skip"
        if answer in ("a", "abort"):
            return "abort"


def skipped(device, detail):
    """A result for a device the operator decided not to touch. Skipped
    devices go to the retry file like failures: the decision was 'not now',
    and the retry file is where 'not now' is kept for the next run."""
    return {"host": device["host"], "target": device["target"],
            "status": "SKIP", "detail": detail}


class Reporter(object):
    """How the run talks to the operator, base class with the shared state.

    Events are state(host, text) for a phase change -- 'checking',
    'uploading 42%', 'waiting for reboot', 'verifying' -- and finish(result)
    for the outcome. Devices with nothing to report (already current) only
    ever produce a finish.
    """

    BAR_WIDTH = 20

    def __init__(self, devices):
        self.total = len(devices)
        self.host_width = max(len(device["host"]) for device in devices)
        self.states = {}   # host -> phase text, for the devices in flight
        self.order = []    # hosts in first-seen order, for a stable display
        self.done = 0
        self.failed = 0    # FAIL and SKIP: everything the retry file gets
        self.lock = threading.Lock()

    def state(self, host, text):
        raise NotImplementedError

    def finish(self, result):
        raise NotImplementedError

    def suspend(self):
        """Interactive mode: take the dynamic display away so a prompt can
        be printed and answered. The next event brings it back."""
        pass

    def _note(self, host):
        if host not in self.states:
            self.order.append(host)

    def _forget(self, host):
        self.states.pop(host, None)
        if host in self.order:
            self.order.remove(host)

    def _note_done(self, result):
        self.done += 1
        if result["status"] in ("FAIL", "SKIP"):
            self.failed += 1

    def _device_lines(self):
        return ["  %-*s %s" % (self.host_width, host, self.states[host])
                for host in self.order]

    def _result_line(self, result):
        return "[%*d/%d] %-*s %-11s %s" % (len(str(self.total)), self.done,
                                           self.total,
                                           self.host_width, result["host"],
                                           "[%s]" % result["status"],
                                           result["detail"])

    def bar(self):
        filled = self.BAR_WIDTH * self.done // max(1, self.total)
        text = "[%s%s] %d/%d done" % ("#" * filled,
                                      "-" * (self.BAR_WIDTH - filled),
                                      self.done, self.total)
        if self.failed:
            text += ", %d failed" % self.failed
        return text


class PlainReporter(Reporter):
    """One line per event, for pipes and log files. Upload percent is
    collapsed to a single 'uploading' line per attempt -- a log does not
    need thirty lines of percentages."""

    def __init__(self, devices):
        super().__init__(devices)
        self._printed = {}  # host -> last phase keyword printed

    def state(self, host, text):
        with self.lock:
            self._note(host)
            self.states[host] = text
            keyword = text.split(" ")[0]
            if self._printed.get(host) != keyword:
                self._printed[host] = keyword
                if text.endswith("%"):  # collapse the percentages to one line
                    text = text.rsplit(" ", 1)[0]
                print("  %-*s %s" % (self.host_width, host, text))

    def finish(self, result):
        with self.lock:
            self._forget(result["host"])
            self._note_done(result)
            print(self._result_line(result))


class TTYReporter(Reporter):
    """One line per device in flight plus the progress bar, redrawn in
    place at the bottom of the terminal. Finished devices scroll away as
    permanent result lines above it."""

    def __init__(self, devices):
        super().__init__(devices)
        self._height = 0  # dynamic lines (devices + bar) currently on screen

    def _redraw(self, result_line=None):
        out = []
        if self._height:
            out.append("\x1b[%dA" % self._height)  # up to the first dynamic line
        body = []
        if result_line is not None:
            body.append(result_line)
        body.extend(self._device_lines())
        body.append(self.bar())
        for line in body:
            out.append("\x1b[2K%s\n" % line)
        leftover = self._height - len(body)
        if leftover > 0:  # clear what the old block left below, then come back
            out.append("\x1b[2K\n" * leftover)
            out.append("\x1b[%dA" % leftover)
        sys.stdout.write("".join(out))
        sys.stdout.flush()
        self._height = len(body) - (1 if result_line is not None else 0)

    def state(self, host, text):
        with self.lock:
            self._note(host)
            self.states[host] = text
            self._redraw()

    def finish(self, result):
        with self.lock:
            self._forget(result["host"])
            self._note_done(result)
            self._redraw(self._result_line(result))

    def suspend(self):
        with self.lock:
            if self._height:
                sys.stdout.write("\x1b[%dA%s\x1b[%dA"
                                 % (self._height, "\x1b[2K\n" * self._height,
                                    self._height))
                sys.stdout.flush()
                self._height = 0


def main():
    parser = argparse.ArgumentParser(
        description="OTA batch updater for OhEzTouch devices",
        epilog="device list: " + __doc__.strip().splitlines()[0])
    parser.add_argument("inputs", nargs="+",
                        help="device list file(s), or with -t: hostnames/IPs")
    parser.add_argument("-t", "--target", choices=sorted(TARGETS),
                        help="update the hosts given as arguments, all to this "
                             "target (skips image build and list checks)")
    parser.add_argument("-p", "--parallel", type=int, default=1, metavar="N",
                        help="update N devices at once (default 1)")
    parser.add_argument("--release", metavar="X.Y",
                        help="take the images from release/X.Y (default: the "
                             "newest release, falling back to the build tree)")
    parser.add_argument("--expect-version", metavar="X.Y",
                        help="firmware version to expect after the update "
                             "(default: the release's version, or the one "
                             "in main/version.h)")
    parser.add_argument("--timeout", type=int, default=10, metavar="S",
                        help="HTTP timeout per request (default 10 s); the "
                             "reboot wait is 12 times this")
    parser.add_argument("--retries", type=int, default=1, metavar="N",
                        help="retry the upload N times when the device "
                             "reports failure (default 1)")
    parser.add_argument("-i", "--interactive", action="store_true",
                        help="ask before every device: yes, skip or abort "
                             "(implies one device at a time)")
    parser.add_argument("--dry-run", action="store_true",
                        help="validate the list, the images and reachability only")
    parser.add_argument("--no-verify", action="store_true",
                        help="do not wait for the reboot and the status page "
                             "(NOT recommended -- a failed flash looks like "
                             "a success then)")
    parser.add_argument("-o", "--retry-file", metavar="F",
                        help="write the failed devices to F (default: retry.json "
                             "when there were failures)")
    args = parser.parse_args()

    if args.interactive and args.parallel > 1:
        parser.error("--interactive goes through the devices one by one; "
                     "--parallel does not apply")

    if args.target:
        devices = [{"host": host, "target": args.target} for host in args.inputs]
    else:
        devices = load_devices(args.inputs)

    if not args.expect_version:
        args.expect_version = args.release or expected_version_default()

    # Where the images come from: the newest release by default, a chosen
    # one with --release, the build tree as fallback (and for targets a
    # release does not cover).
    if args.release:
        if not os.path.isdir(os.path.join(REPO_ROOT, "release", args.release)):
            sys.exit("release %s not found under release/" % args.release)
        release = args.release
    else:
        release = latest_release()

    needed = sorted({device["target"] for device in devices})
    images = {}
    for target in needed:
        images[target], source = image_for(target, release)
    if release and all(images[target] and "/release/" in images[target]
                       for target in needed):
        print("images: release %s" % release)
    elif release:
        print("images: release %s, partly build tree" % release)
    else:
        print("images: build tree (no release found)")
    for target in needed:
        path = images[target] or "MISSING"
        print("  %-16s %s" % (target, path))

    reporter = TTYReporter(devices) if sys.stdout.isatty() else PlainReporter(devices)
    failures = []

    if args.interactive:
        # One device at a time, in list order, each one only after the
        # operator's yes. Skipped and unasked devices end up in the retry
        # file, which is where "not now" is kept for the next run.
        abort = False
        for position, device in enumerate(devices, start=1):
            if abort:
                result = skipped(device, "run aborted")
            else:
                result = current_result(device, args.expect_version)
                if result is None:
                    reporter.suspend()
                    print()
                    print("device %d/%d: %s (%s)"
                          % (position, len(devices), device["host"],
                             device["target"]))
                    if device.get("comment"):
                        print("    %s" % device["comment"])
                    answer = ask_operator()
                    if answer == "yes":
                        result = update_device(
                            device, args, images[device["target"]],
                            lambda text, h=device["host"]: reporter.state(h, text))
                    elif answer == "skip":
                        result = skipped(device, "skipped by operator")
                    else:
                        abort = True
                        result = skipped(device, "run aborted")
            reporter.finish(result)
            if result["status"] in ("FAIL", "SKIP"):
                failures.append(device)
    else:
        with ThreadPoolExecutor(max_workers=max(1, args.parallel)) as pool:
            futures = {}
            for device in devices:
                future = pool.submit(
                    update_device, device, args, images[device["target"]],
                    lambda text, h=device["host"]: reporter.state(h, text))
                futures[future] = device
            for future in as_completed(futures):
                result = future.result()
                reporter.finish(result)
                if result["status"] in ("FAIL", "SKIP"):
                    failures.append(futures[future])

    if args.dry_run:
        print("\n%d of %d devices reachable" % (len(devices) - len(failures),
                                                len(devices)))
    else:
        print("\n%d of %d devices updated" % (len(devices) - len(failures),
                                              len(devices)))

    if failures:
        retry_path = args.retry_file or "retry.json"
        with open(retry_path, "w") as handle:
            json.dump({"devices": failures}, handle, indent=2)
            handle.write("\n")
        print("failed devices written to %s -- re-run with: "
              "batchupdate.py %s" % (retry_path, retry_path))

    sys.exit(len(failures))


if __name__ == "__main__":
    main()
