"""The SweetHome3D converter against homes built here, in memory.

    python3 mqttviz/test_sh3d.py
"""

import io
import math
import unittest
import zipfile

import sh3d


def make_sh3d(xml, extra=None):
    """A .sh3d as SweetHome3D writes it: a zip, with Home.xml in it."""
    buffer = io.BytesIO()
    with zipfile.ZipFile(buffer, "w") as archive:
        if xml is not None:
            archive.writestr("Home.xml", xml)
        for name, data in (extra or {}).items():
            archive.writestr(name, data)
    return buffer.getvalue()


# Two levels, a square room of 4 x 3 m with four walls, a door, and one
# round wall upstairs -- the shape of what SweetHome3D 6+ writes.
HOME = """<?xml version='1.0'?>
<home version='7000' name='/tmp/test.sh3d' wallHeight='250'>
  <level id='lv0' name='Ground' elevation='0' floorThickness='12'
         height='250' elevationIndex='0'/>
  <level id='lv1' name='Upper' elevation='262' floorThickness='12'
         height='240' elevationIndex='1'/>
  <level id='lvh' name='Hidden' elevation='600' height='240'
         viewable='false'/>
  <furnitureGroup id='g' name='g'>
    <doorOrWindow id='d0' level='lv0' name='Door' x='200' y='0'
                  elevation='0' width='90' depth='10' height='210'
                  angle='0'/>
  </furnitureGroup>
  <wall id='w0' level='lv0' xStart='0' yStart='0' xEnd='400' yEnd='0'
        height='250' thickness='10'/>
  <wall id='w1' level='lv0' xStart='400' yStart='0' xEnd='400' yEnd='300'
        thickness='10'/>
  <wall id='w2' level='lv0' xStart='400' yStart='300' xEnd='0' yEnd='300'
        height='250' heightAtEnd='200' thickness='20'/>
  <wall id='w3' level='lv0' xStart='0' yStart='300' xEnd='0' yEnd='0'
        height='250' thickness='10'/>
  <wall id='w4' level='lv1' xStart='0' yStart='0' xEnd='400' yEnd='0'
        height='240' thickness='10' arcExtent='1.5707964'/>
  <wall id='wh' level='lvh' xStart='0' yStart='0' xEnd='100' yEnd='0'/>
  <room id='r0' level='lv0' name='Kitchen'>
    <point x='0' y='0'/><point x='400' y='0'/>
    <point x='400' y='300'/><point x='0' y='300'/>
  </room>
</home>
"""


class ConvertTest(unittest.TestCase):

    def setUp(self):
        self.house = sh3d.convert(make_sh3d(HOME), name="test")

    def test_levels_in_metres_hidden_ones_left_out(self):
        levels = self.house["levels"]
        self.assertEqual([lv["id"] for lv in levels], ["lv0", "lv1"])
        self.assertEqual(levels[1]["elevation"], 2.62)
        self.assertEqual(levels[1]["height"], 2.4)

    def test_walls_in_metres_plan_y_is_z(self):
        ground = [w for w in self.house["walls"] if w["level"] == "lv0"]
        self.assertEqual(len(ground), 4)
        self.assertEqual(ground[1]["a"], [4.0, 0.0])
        self.assertEqual(ground[1]["b"], [4.0, 3.0])
        self.assertEqual(ground[0]["thickness"], 0.1)

    def test_wall_height_defaults_and_slopes(self):
        ground = [w for w in self.house["walls"] if w["level"] == "lv0"]
        # no height: the level's
        self.assertEqual(ground[1]["height"], 2.5)
        # heightAtEnd: a sloped top
        self.assertEqual(ground[2]["height"], 2.5)
        self.assertEqual(ground[2]["height_end"], 2.0)

    def test_round_wall_is_cut_into_pieces_on_its_arc(self):
        upper = [w for w in self.house["walls"] if w["level"] == "lv1"]
        self.assertGreater(len(upper), 4)
        self.assertEqual(upper[0]["a"], [0.0, 0.0])
        self.assertEqual(upper[-1]["b"], [4.0, 0.0])
        # a quarter circle through both ends: every corner on one radius
        radius = 4.0 / (2 * math.sin(math.pi / 4))
        centre = (2.0, 2.0)
        for wall in upper:
            self.assertAlmostEqual(math.dist(wall["a"], centre), radius,
                                   places=2)
        # one wall: closed at the two ends of the arc only
        self.assertEqual(upper[0]["caps"], [True, False])
        self.assertEqual(upper[-1]["caps"], [False, True])
        self.assertNotIn("caps", self.house["walls"][0])
        # a positive extent bends to the left: towards -z on screen
        middle = upper[len(upper) // 2]["a"]
        self.assertLess(middle[1], 0)

    def test_rooms_and_openings(self):
        rooms = self.house["rooms"]
        self.assertEqual(len(rooms), 1)
        self.assertEqual(rooms[0]["name"], "Kitchen")
        self.assertEqual(rooms[0]["points"][2], [4.0, 3.0])
        doors = self.house["openings"]
        self.assertEqual(len(doors), 1)
        self.assertEqual((doors[0]["x"], doors[0]["w"], doors[0]["h"]),
                         (2.0, 0.9, 2.1))

    def test_bounds(self):
        self.assertEqual(self.house["bounds"]["min"][0], 0.0)
        self.assertEqual(self.house["bounds"]["max"], [4.0, 3.0])
        self.assertEqual(self.house["name"], "test")


class LevellessTest(unittest.TestCase):

    def test_a_home_without_levels_has_one(self):
        xml = ("<home wallHeight='260'><wall xStart='0' yStart='0' "
               "xEnd='100' yEnd='0'/></home>")
        house = sh3d.convert(make_sh3d(xml))
        self.assertEqual(len(house["levels"]), 1)
        self.assertEqual(house["walls"][0]["level"], house["levels"][0]["id"])
        self.assertEqual(house["walls"][0]["height"], 2.6)


class RefusalTest(unittest.TestCase):

    def assertRefused(self, data, fragment):
        with self.assertRaises(sh3d.HouseError) as caught:
            sh3d.convert(data)
        self.assertIn(fragment, str(caught.exception))

    def test_not_a_zip(self):
        self.assertRefused(b"hello", "no zip")

    def test_old_file_without_home_xml(self):
        self.assertRefused(make_sh3d(None, {"Home": b"\xac\xed"}), "5.3")

    def test_zip_without_a_home(self):
        self.assertRefused(make_sh3d(None, {"x.txt": b"x"}), "no Home.xml")

    def test_broken_xml(self):
        self.assertRefused(make_sh3d("<home><wall"), "does not parse")

    def test_empty_home(self):
        self.assertRefused(make_sh3d("<home/>"), "no walls")


if __name__ == "__main__":
    unittest.main()
