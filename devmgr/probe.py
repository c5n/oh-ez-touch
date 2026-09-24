#!/usr/bin/env python3
"""Probing OhEzTouch devices for the device manager.

Two jobs, both built on the firmware's REST API (0.91 and later):

  * scan_subnet()  -- find devices: one GET /api/status per address of a
    subnet, in parallel, with two seconds of patience per answer. The same
    deliberate care tools/discover.py takes: this is not a port scanner, and
    it never touches /update.
  * refresh_devices() -- re-read the status of every device the manager
    already knows, so the list shows what is true now rather than what was
    true at scan time.

A device that answers is identified by its MAC, which is the one thing about
it that DHCP cannot change; its address is re-learnt from every answer. One
that does not answer is offline, and stays in the list -- the manager's
memory is the point of it.
"""

import ipaddress
import json
import urllib.error
import urllib.request
from concurrent.futures import ThreadPoolExecutor, as_completed

# Two seconds is a long time for one GET on a LAN, and short enough that a
# dark /24 finishes in well under a minute at this parallelism.
PROBE_TIMEOUT_S = 2.0
PROBE_WORKERS = 32

# A refresh waits a little longer than a scan probe: the address is known to
# have held a device, so the odd slow answer is worth waiting for.
REFRESH_TIMEOUT_S = 3.0
REFRESH_WORKERS = 16


class DeviceUnreachable(Exception):
    """No answer, a non-200, or a body that is not our JSON."""


def fetch_json(host, path, timeout):
    """One GET, parsed as JSON. Raises DeviceUnreachable for anything that is
    not a 200 with a JSON body -- from out here, a refused connection, a
    timeout and a foreign device all look alike: not something to list."""
    url = "http://%s%s" % (host, path)

    try:
        with urllib.request.urlopen(url, timeout=timeout) as response:
            return json.loads(response.read().decode("utf-8", "replace"))
    except (urllib.error.URLError, OSError, ValueError) as error:
        raise DeviceUnreachable(str(error))


def post_json(host, path, payload, timeout):
    """One POST of a JSON body, the answer parsed as JSON."""
    url = "http://%s%s" % (host, path)
    data = json.dumps(payload).encode("utf-8")
    request = urllib.request.Request(url, data=data,
                                     headers={"Content-Type": "application/json"},
                                     method="POST")

    try:
        with urllib.request.urlopen(request, timeout=timeout) as response:
            body = response.read().decode("utf-8", "replace")
            return response.status, json.loads(body) if body else {}
    except urllib.error.HTTPError as error:
        body = error.read().decode("utf-8", "replace")
        try:
            return error.code, json.loads(body)
        except ValueError:
            return error.code, {"error": body}
    except (urllib.error.URLError, OSError) as error:
        raise DeviceUnreachable(str(error))


def is_status(doc):
    """Whether a JSON document is a 0.91 status answer. Older firmware and
    foreign devices have no "target" beside the "version"."""
    return isinstance(doc, dict) and "version" in doc and "target" in doc \
        and "mac" in doc


def probe_address(address, port):
    """One address. The status document if an OhEzTouch 0.91+ answers there,
    None otherwise."""
    host = "%s:%d" % (address, port) if port != 80 else address

    try:
        doc = fetch_json(host, "/api/status", PROBE_TIMEOUT_S)
    except DeviceUnreachable:
        return None

    return doc if is_status(doc) else None


def scan_subnet(subnet, port=80, workers=PROBE_WORKERS, on_found=None,
                on_progress=None):
    """Every address of the subnet, in parallel. Returns the status documents
    found, in address order.

    on_found(address, doc) is called as each device answers; on_progress(
    done, total) as each probe finishes, so the caller can say how far along
    the scan is without waiting for it.
    """
    network = ipaddress.ip_network(subnet, strict=False)
    addresses = [str(a) for a in network.hosts()]

    # A /32 (one panel, or the simulator) has no hosts() at all.
    if not addresses:
        addresses = [str(network.network_address)]

    found = []
    done = 0

    with ThreadPoolExecutor(max_workers=max(1, workers)) as pool:
        futures = {pool.submit(probe_address, address, port): address
                   for address in addresses}

        for future in as_completed(futures):
            address = futures[future]
            doc = future.result()

            done += 1

            if doc is not None:
                found.append((address, doc))
                if on_found is not None:
                    on_found(address, doc)

            if on_progress is not None:
                on_progress(done, len(addresses))

    # Address order: the list is read by a person.
    found.sort(key=lambda pair: ipaddress.ip_address(pair[0]))

    return found


def refresh_devices(hosts, port=80, workers=REFRESH_WORKERS,
                    timeout=REFRESH_TIMEOUT_S, on_progress=None):
    """Re-read /api/status from each host. Returns {host: doc or None} --
    None is the offline answer, and the caller decides what it means for the
    device that was there. The timeout is the caller's to choose: it must
    stay below the cadence the refresh runs on, or one dark address could
    outlast the whole interval.

    on_progress(done, total) is called as each answer arrives, so the caller
    can show how far along the pass is.
    """
    results = {}
    done = 0

    def one(host):
        target = "%s:%d" % (host, port) if port != 80 else host
        try:
            doc = fetch_json(target, "/api/status", timeout)
        except DeviceUnreachable:
            return None
        return doc if is_status(doc) else None

    with ThreadPoolExecutor(max_workers=max(1, workers)) as pool:
        futures = {pool.submit(one, host): host for host in hosts}

        for future in as_completed(futures):
            results[futures[future]] = future.result()
            done += 1
            if on_progress is not None:
                on_progress(done, len(futures))

    return results
