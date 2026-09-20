#!/usr/bin/env python3
"""Find OhEzTouch devices on a subnet and show or record what it finds.

Probes every address of a subnet with an HTTP GET / and prints the result as
a table. With -o the same result is also written as the JSON format
tools/batchupdate.py reads:

    tools/discover.py 192.168.1.0/24
    tools/discover.py                          # the local /24
    tools/discover.py 192.168.1.0/24 -o myOhEzTouchDevices.json

Recognition, per firmware generation:

  * 0.90 and later serve a status page with Version and Target rows, so the
    entry comes out complete -- target included.
  * A device running the minimal recovery firmware serves the same rows but
    reports target "Minimal", and the one thing it cannot know is the board
    it runs on. The entry comes out like a legacy one -- "target": null,
    "version": null (recording the version would make batchupdate.py skip
    the device as current, and it is anything but) -- with a comment saying
    what it is and what to do.
  * The pre-0.90 AutoConnect firmware answers with pages titled "OhEzTouch"
    but tells neither its version nor its board over HTTP, so the entry comes
    out with "target": null -- deliberately: batchupdate.py refuses it until
    a person fills the target in, which is the only safe way to pick the
    image a device gets flashed with. The configured hostname is read from
    its /openhab_settings page into the comment.

Careful by design: one GET per address -- one more on the settings page for
legacy devices -- two seconds of patience per answer, bounded parallelism.
This is not a port scanner, and it does not touch the /update endpoint.

Needs nothing but Python 3. Shares the target table and the status page
parsing with batchupdate.py, so the two cannot drift apart.
"""

import argparse
import ipaddress
import json
import re
import socket
import sys
import urllib.error
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path

# tools/ is sys.path[0] when this runs as a script, so this import is also
# what a run from any other directory resolves.
from batchupdate import TARGETS, VERSION_RE, TARGET_NAME_RE, http_get, \
    MINIMAL_TARGET_NAME

# What a probe gives an address before calling it nothing: two seconds is a
# long time for one GET on a LAN, and short enough that a dark /24 is done in
# under a minute at the default parallelism.
PROBE_TIMEOUT_S = 2.0
PROBE_WORKERS = 32

# The TARGET_NAME a build reports on its status page, back to the build
# directory that produced it.
TARGET_NAME_TO_BUILD = {name: build for build, name in TARGETS.items()}

# The AutoConnect firmware's pages carry this as ACConfig.title. It is the
# whole of what identifies a pre-0.90 device -- no version, no target. The
# one useful thing it does tell lives on its settings page: /openhab_settings
# renders an input named config_general_hostname with the configured hostname
# as value (main branch, src/ac_settings.hpp).
LEGACY_MARKER = "OhEzTouch"
LEGACY_SETTINGS_PAGE = "/openhab_settings"
LEGACY_HOSTNAME_FIELD = "config_general_hostname"


def classify(body):
    """What one answered page says about the device serving it.

    Returns (generation, version, target_name): generation "current" or
    "legacy", the two remaining fields None where the firmware does not tell.
    """
    version = VERSION_RE.search(body)
    target = TARGET_NAME_RE.search(body)

    if version and target:
        return "current", version.group(1), target.group(1).strip()

    if LEGACY_MARKER in body:
        return "legacy", None, None

    return None, None, None


def legacy_hostname(body):
    """The configured hostname from a legacy /openhab_settings page, or None.

    AutoConnect renders one input element per setting; find the one with our
    name and take its value, without trusting the attribute order."""
    for tag in re.findall(r"<input\b[^>]*>", body, flags=re.IGNORECASE):
        if 'name="%s"' % LEGACY_HOSTNAME_FIELD not in tag:
            continue
        value = re.search(r'value="([^"]*)"', tag)
        if value:
            return value.group(1) or None
    return None


def probe(address, port, resolve_names):
    """One address. A result dict for an OhEzTouch device, None otherwise --
    connection failures, non-200 answers and pages that are not ours all look
    alike from here: not something to put in the list."""
    try:
        body = http_get(address, port, "/", PROBE_TIMEOUT_S)
    except (urllib.error.URLError, OSError):
        return None

    generation, version, target_name = classify(body)

    if generation is None:
        return None

    # A legacy device's hostname is worth one more GET: it is the only name
    # the pre-0.90 firmware can report for itself.
    hostname = None
    if generation == "legacy":
        try:
            settings = http_get(address, port, LEGACY_SETTINGS_PAGE,
                                PROBE_TIMEOUT_S)
            hostname = legacy_hostname(settings)
        except (urllib.error.URLError, OSError):
            pass

    name = None
    if resolve_names:
        try:
            name = socket.gethostbyaddr(address)[0]
        except (OSError, socket.herror):
            pass

    return {"ip": address, "name": name, "generation": generation,
            "version": version, "target_name": target_name,
            "hostname": hostname}


def device_entry(found):
    """One scan result as a devices.json entry. The comment keeps the address
    the device was seen at, because the host is its name where DNS knew one --
    and the address is what still finds it when the name stops resolving."""
    host = found["name"] or found["ip"]
    comment = found["ip"]

    if found["generation"] == "legacy":
        comment = "%s; pre-0.90" % comment
        if found["hostname"]:
            comment += ", hostname %r" % found["hostname"]
        comment += ", target unknown -- fill in before updating"
        return {"host": host, "target": None, "version": None,
                "comment": comment}

    # A device running the minimal recovery firmware reports the version it
    # was built from, but it is not running it: the full image is still to
    # come. Recording that version would make batchupdate.py skip the device
    # as already current, so -- exactly like a legacy device -- the entry
    # says nothing and asks for the board instead.
    if found["target_name"] == MINIMAL_TARGET_NAME:
        comment = ("%s; running the minimal recovery firmware %s, board "
                   "target unknown -- fill in, then update to the full image"
                   % (comment, found["version"]))
        return {"host": host, "target": None, "version": None,
                "comment": comment}

    target = TARGET_NAME_TO_BUILD.get(found["target_name"])
    if target is None:
        comment = "%s; target %r is not a build in this tree" % (comment,
                                                                 found["target_name"])

    return {"host": host, "target": target, "version": found["version"],
            "comment": comment}


def local_subnet():
    """The /24 of the primary IPv4 address, when no subnet was given.

    The UDP connect sends nothing -- it is the way to ask the routing table
    which local address faces the network, without rooting through interface
    lists."""
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
            s.connect(("192.0.2.1", 80))  # TEST-NET-1: routed, never sent
            return ipaddress.ip_network(s.getsockname()[0] + "/24", strict=False)
    except OSError:
        return None


def print_table(entries):
    """The scan result as a table: the fleet at a glance."""
    if not entries:
        print("no devices found")
        return

    rows = [(entry["host"], entry["target"] or "-", entry["version"] or "-",
             entry.get("comment") or "-") for entry in entries]
    header = ("HOST", "TARGET", "VERSION", "COMMENT")
    widths = [max(len(line) for line in column)
              for column in zip(header, *rows)]
    fmt = "  ".join("%%-%ds" % width for width in widths)

    print(fmt % header)
    print(fmt % tuple("-" * width for width in widths))
    for row in rows:
        print(fmt % row)


def main():
    global PROBE_TIMEOUT_S

    parser = argparse.ArgumentParser(
        description="Find OhEzTouch devices on a subnet, print them as a "
                    "table and, with -o, write a devices.json for "
                    "tools/batchupdate.py.")
    parser.add_argument("subnet", nargs="?",
                        help="CIDR to scan (default: the local /24)")
    parser.add_argument("-o", "--output", metavar="F",
                        help="also write the result as a devices.json")
    parser.add_argument("--port", type=int, default=80,
                        help="HTTP port to probe (default 80)")
    parser.add_argument("--timeout", type=float, default=PROBE_TIMEOUT_S,
                        help="seconds to wait per address (default %.1f)"
                             % PROBE_TIMEOUT_S)
    parser.add_argument("--workers", type=int, default=PROBE_WORKERS,
                        help="parallel probes (default %d)" % PROBE_WORKERS)
    parser.add_argument("--no-dns", action="store_true",
                        help="do not reverse-resolve addresses; list IPs")
    parser.add_argument("--force", action="store_true",
                        help="with -o: overwrite an existing output file")
    args = parser.parse_args()

    PROBE_TIMEOUT_S = args.timeout

    if args.subnet is not None:
        try:
            subnet = ipaddress.ip_network(args.subnet, strict=False)
        except ValueError as e:
            parser.error(str(e))
    else:
        subnet = local_subnet()
        if subnet is None:
            parser.error("no subnet given and the local one cannot be determined")

    output = Path(args.output) if args.output is not None else None
    if output is not None and output.exists() and not args.force:
        print("error: %s exists -- refusing to overwrite it without --force"
              % output, file=sys.stderr)
        return 2

    print("scanning %s (%d addresses, port %d) ..."
          % (subnet, subnet.num_addresses, args.port))

    found = []

    with ThreadPoolExecutor(max_workers=max(1, args.workers)) as pool:
        futures = {pool.submit(probe, str(address), args.port,
                               not args.no_dns): address
                   for address in subnet.hosts()}
        for future in as_completed(futures):
            result = future.result()
            if result is not None:
                found.append(result)
                print("  %-15s %s" % (result["ip"], result["name"] or "(no name)"))

    # The fleet in address order: the file is read and edited by a person.
    found.sort(key=lambda f: ipaddress.ip_address(f["ip"]))

    doc = {"devices": [device_entry(f) for f in found]}

    print()
    print_table(doc["devices"])

    current = sum(1 for f in found if f["generation"] == "current")
    legacy = len(found) - current
    incomplete = sum(1 for f in found
                     if TARGET_NAME_TO_BUILD.get(f["target_name"]) is None)

    print("\n%d devices found: %d on 0.90 or later, %d on pre-0.90 firmware"
          % (len(found), current, legacy))
    if output is not None:
        output.write_text(json.dumps(doc, indent=2) + "\n")
        print("wrote %s" % output)
    if incomplete:
        print("note: %d %s \"target\": null -- fill in the target before "
              "running batchupdate.py"
              % (incomplete,
                 "entry has" if incomplete == 1 else "entries have"))

    return 0


if __name__ == "__main__":
    sys.exit(main())
