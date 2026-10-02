"""A SweetHome3D home, reduced to what the space view draws.

A .sh3d file is a zip. Since SweetHome3D 5.3 it carries a Home.xml entry
beside the serialized Java object; that XML is all this reads -- the
walls, the rooms, the levels and the doors and windows, nothing of the
furniture, the textures or the 3D models. What comes out is a small JSON
document in metres:

    {"version": 1, "name": ..., "levels": [...], "walls": [...],
     "rooms": [...], "openings": [...], "bounds": {...}}

The axes are the ones the page's 3D view uses: SweetHome3D's plan x is
x, its plan y (pointing down the screen) is z, and y is up. Nothing but
the standard library -- the same rules as the rest of the tool.

    python3 mqttviz/sh3d.py home.sh3d > house.json
"""

import io
import json
import math
import sys
import time
import zipfile
import xml.etree.ElementTree as ET

HOME_XML = "Home.xml"

# What a house may weigh. A real home is a few hundred walls; these
# bounds are there so a strange file costs a refusal, not the server.
XML_MAX = 64 * 1024 * 1024
MAX_LEVELS = 50
MAX_WALLS = 5000
MAX_ROOMS = 2000
MAX_ROOM_POINTS = 500
MAX_OPENINGS = 3000

# A round wall becomes straight pieces, one per this many radians of
# its arc -- smooth enough at room scale, never more than the cap.
ARC_STEP = math.radians(10)
ARC_PIECES_MAX = 36

DEFAULT_WALL_HEIGHT = 250.0       # cm, SweetHome3D's own default
DEFAULT_LEVEL = "_"               # a home without levels has this one


class HouseError(ValueError):
    """A file that is not a home this can read: the message says why."""


def _num(element, name, default=None):
    """One numeric attribute, in whatever unit the file uses -- or the
    default when it is missing or not a finite number."""
    raw = element.get(name)
    if raw is None:
        return default
    try:
        value = float(raw)
    except ValueError:
        return default
    return value if math.isfinite(value) else default


def _m(cm):
    """Centimetres, the unit of every SweetHome3D length, to metres --
    rounded to the millimetre, which is more than a wall is true to."""
    return round(cm / 100.0, 3)


def _arc_points(xa, ya, xb, yb, extent):
    """The corners of a round wall from start to end: its arc of
    `extent` radians cut into straight pieces. A positive extent bends
    the wall to the left of its direction in the plan."""
    chord = math.hypot(xb - xa, yb - ya)
    if chord == 0 or abs(extent) < 1e-3:
        return [(xa, ya), (xb, yb)]

    radius = chord / (2 * math.sin(abs(extent) / 2))
    mx, my = (xa + xb) / 2, (ya + yb) / 2
    # from the chord's middle to the circle's centre, perpendicular
    off = math.sqrt(max(0.0, radius * radius - chord * chord / 4))
    nx, ny = -(yb - ya) / chord, (xb - xa) / chord
    side = 1 if extent > 0 else -1
    if abs(extent) > math.pi:
        side = -side
    cx, cy = mx + side * nx * off, my + side * ny * off

    start = math.atan2(ya - cy, xa - cx)
    pieces = max(2, min(ARC_PIECES_MAX, int(abs(extent) / ARC_STEP) + 1))
    points = []
    for i in range(pieces + 1):
        angle = start + extent * i / pieces
        points.append((cx + radius * math.cos(angle),
                       cy + radius * math.sin(angle)))
    points[0], points[-1] = (xa, ya), (xb, yb)
    return points


def _read_home_xml(data):
    try:
        archive = zipfile.ZipFile(io.BytesIO(data))
    except zipfile.BadZipFile:
        raise HouseError("not a SweetHome3D file (no zip archive)")

    with archive:
        names = archive.namelist()
        if HOME_XML not in names:
            if "Home" in names:
                raise HouseError("this file has no Home.xml -- open it in "
                                 "SweetHome3D 5.3 or newer and save it again")
            raise HouseError("not a SweetHome3D file (no Home.xml)")

        info = archive.getinfo(HOME_XML)
        if info.file_size > XML_MAX:
            raise HouseError("Home.xml is too large")
        with archive.open(info) as handle:
            return handle.read(XML_MAX + 1)


def convert(data, name=None):
    """The bytes of a .sh3d file in, the house document out. Raises
    HouseError for anything that is not a home this can read."""
    xml = _read_home_xml(data)
    try:
        root = ET.fromstring(xml)
    except ET.ParseError as error:
        raise HouseError("Home.xml does not parse: %s" % error)
    if root.tag != "home":
        raise HouseError("Home.xml has no <home>")

    wall_height = _num(root, "wallHeight", DEFAULT_WALL_HEIGHT)

    levels = []
    level_height = {}
    for element in root.iter("level"):
        if len(levels) >= MAX_LEVELS:
            break
        ident = element.get("id")
        if not ident:
            continue
        if element.get("viewable") == "false":
            continue
        height = _num(element, "height", wall_height)
        levels.append({
            "id": ident,
            "name": element.get("name") or ident,
            "elevation": _m(_num(element, "elevation", 0.0)),
            "height": _m(height),
            "index": int(_num(element, "elevationIndex", len(levels))),
        })
        level_height[ident] = height

    levels.sort(key=lambda lv: (lv["elevation"], lv["index"]))
    for level in levels:
        del level["index"]
    known = {level["id"] for level in levels}

    if not levels:
        levels = [{"id": DEFAULT_LEVEL, "name": "",
                   "elevation": 0.0, "height": _m(wall_height)}]
        known = {DEFAULT_LEVEL}

    def level_of(element):
        ident = element.get("level") or DEFAULT_LEVEL
        if ident not in known:
            # a level hidden in the file, or a file without levels
            # naming one anyway: only the default may stand in
            return levels[0]["id"] if ident == DEFAULT_LEVEL else None
        return ident

    xs, zs = [], []

    walls = []
    for element in root.iter("wall"):
        level = level_of(element)
        coords = [_num(element, key) for key in
                  ("xStart", "yStart", "xEnd", "yEnd")]
        if level is None or None in coords:
            continue
        thickness = _num(element, "thickness", 10.0)
        height = _num(element, "height",
                      level_height.get(level, wall_height))
        height_end = _num(element, "heightAtEnd", height)
        points = _arc_points(*coords, _num(element, "arcExtent", 0.0))

        for i in range(len(points) - 1):
            if len(walls) >= MAX_WALLS:
                raise HouseError("more than %d walls" % MAX_WALLS)
            (xa, ya), (xb, yb) = points[i], points[i + 1]
            t0, t1 = i / (len(points) - 1), (i + 1) / (len(points) - 1)
            piece = {
                "level": level,
                "a": [_m(xa), _m(ya)],
                "b": [_m(xb), _m(yb)],
                "thickness": max(0.01, _m(thickness)),
                "height": max(0.01, _m(height + (height_end - height) * t0)),
                "height_end": max(0.01,
                                  _m(height + (height_end - height) * t1)),
            }
            # the pieces of a round wall are one wall: closed only at
            # the two ends of the arc, not between each other
            if len(points) > 2:
                piece["caps"] = [i == 0, i == len(points) - 2]
            walls.append(piece)
            xs += [xa, xb]
            zs += [ya, yb]

    rooms = []
    for element in root.iter("room"):
        level = level_of(element)
        if level is None:
            continue
        points = []
        for point in element.iter("point"):
            x, y = _num(point, "x"), _num(point, "y")
            if x is None or y is None:
                continue
            points.append([_m(x), _m(y)])
            xs.append(x)
            zs.append(y)
            if len(points) >= MAX_ROOM_POINTS:
                break
        if len(points) < 3:
            continue
        if len(rooms) >= MAX_ROOMS:
            raise HouseError("more than %d rooms" % MAX_ROOMS)
        rooms.append({"level": level,
                      "name": (element.get("name") or "")[:60],
                      "points": points})

    openings = []
    for element in root.iter("doorOrWindow"):
        level = level_of(element)
        x, y = _num(element, "x"), _num(element, "y")
        if level is None or x is None or y is None:
            continue
        if len(openings) >= MAX_OPENINGS:
            break
        openings.append({
            "level": level,
            "x": _m(x),
            "z": _m(y),
            "elev": _m(_num(element, "elevation", 0.0)),
            "w": _m(_num(element, "width", 80.0)),
            "h": _m(_num(element, "height", 200.0)),
            "angle": round(_num(element, "angle", 0.0), 4),
        })

    if not walls and not rooms:
        raise HouseError("the home has no walls and no rooms")

    return {
        "version": 1,
        "name": (name or root.get("name") or "house")[:80],
        "imported_at": time.strftime("%Y-%m-%d %H:%M:%S"),
        "levels": levels,
        "walls": walls,
        "rooms": rooms,
        "openings": openings,
        "bounds": {"min": [_m(min(xs)), _m(min(zs))],
                   "max": [_m(max(xs)), _m(max(zs))]},
    }


def main():
    if len(sys.argv) != 2:
        sys.exit("usage: sh3d.py HOME.sh3d > house.json")
    with open(sys.argv[1], "rb") as handle:
        data = handle.read()
    try:
        house = convert(data, name=sys.argv[1].rsplit("/", 1)[-1])
    except HouseError as error:
        sys.exit("sh3d: %s" % error)
    json.dump(house, sys.stdout, indent=1)
    sys.stdout.write("\n")


if __name__ == "__main__":
    main()
