#!/usr/bin/env python3
"""A small two-storey house as SweetHome3D would save it, for the demo
and the screenshots: 10 x 8 m, four rooms a floor, a round bay on the
east side, a door and three windows.

    python3 mqttviz/demo/demo_house.py demo.sh3d

Only Home.xml is written -- all the space view reads. SweetHome3D itself
would add the serialized Home entry and the models beside it.
"""

import sys
import zipfile


def walls_of_box(level, x0, y0, x1, y1, height):
    corners = [(x0, y0), (x1, y0), (x1, y1), (x0, y1)]
    return [wall(level, corners[i], corners[(i + 1) % 4], height)
            for i in range(4)]


def wall(level, a, b, height, thickness=12, arc=None):
    xml = ("<wall level='%s' xStart='%g' yStart='%g' xEnd='%g' yEnd='%g'"
           " height='%g' thickness='%g'" % (level, a[0], a[1], b[0], b[1],
                                            height, thickness))
    if arc is not None:
        xml += " arcExtent='%g'" % arc
    return xml + "/>"


def room(level, name, points):
    return ("<room level='%s' name='%s'>" % (level, name)
            + "".join("<point x='%g' y='%g'/>" % p for p in points)
            + "</room>")


def opening(level, x, y, elevation, width, height, angle):
    return ("<doorOrWindow level='%s' x='%g' y='%g' elevation='%g'"
            " width='%g' depth='12' height='%g' angle='%g'/>"
            % (level, x, y, elevation, width, height, angle))


def home_xml():
    parts = []
    # the ground floor: living and hall to the west, kitchen and office
    # to the east, the office opening into a round bay
    parts += walls_of_box("g", 0, 0, 1000, 800, 250)
    parts += [wall("g", (450, 0), (450, 800), 250, 10),
              wall("g", (0, 450), (450, 450), 250, 10),
              wall("g", (450, 350), (1000, 350), 250, 10),
              wall("g", (1000, 200), (1000, 600), 250, arc=-2.0)]
    parts += [room("g", "Living", [(0, 0), (450, 0), (450, 450), (0, 450)]),
              room("g", "Hall", [(0, 450), (450, 450), (450, 800),
                                 (0, 800)]),
              room("g", "Kitchen", [(450, 0), (1000, 0), (1000, 350),
                                    (450, 350)]),
              room("g", "Office", [(450, 350), (1000, 350), (1000, 800),
                                   (450, 800)])]
    parts += [opening("g", 200, 800, 0, 100, 210, 0),
              opening("g", 0, 200, 90, 120, 120, 1.5708),
              opening("g", 700, 0, 90, 160, 130, 0)]

    # the upper floor: four rooms around a cross of walls
    parts += walls_of_box("u", 0, 0, 1000, 800, 240)
    parts += [wall("u", (500, 0), (500, 800), 240, 10),
              wall("u", (0, 400), (1000, 400), 240, 10)]
    parts += [room("u", "Bedroom", [(0, 0), (500, 0), (500, 400), (0, 400)]),
              room("u", "Bath", [(500, 0), (1000, 0), (1000, 400),
                                 (500, 400)]),
              room("u", "Kids", [(0, 400), (500, 400), (500, 800),
                                 (0, 800)]),
              room("u", "Study", [(500, 400), (1000, 400), (1000, 800),
                                  (500, 800)])]
    parts += [opening("u", 250, 0, 90, 120, 120, 0)]

    return ("<?xml version='1.0'?>\n"
            "<home name='demo' wallHeight='250'>"
            "<level id='g' name='Ground floor' elevation='0' height='250'"
            " floorThickness='12' elevationIndex='0'/>"
            "<level id='u' name='Upper floor' elevation='262' height='240'"
            " floorThickness='12' elevationIndex='1'/>"
            + "".join(parts) + "</home>\n")


def main():
    if len(sys.argv) != 2:
        sys.exit("usage: demo_house.py OUT.sh3d")
    with zipfile.ZipFile(sys.argv[1], "w", zipfile.ZIP_DEFLATED) as archive:
        archive.writestr("Home.xml", home_xml())


if __name__ == "__main__":
    main()
