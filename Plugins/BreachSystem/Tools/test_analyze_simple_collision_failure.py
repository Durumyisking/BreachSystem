"""Independent geometry fixtures for the offline failure analyzer."""
import tempfile
import unittest
from pathlib import Path

from analyze_simple_collision_failure import inspect_stage, parse_snapshot


def rectangle(x0, y0, x1, y1):
    return [(x0, y0), (x1, y0), (x1, y1), (x0, y1)]


def region(outer, holes=()):
    return {"outer": outer, "holes": list(holes)}


class FailureAnalysisTests(unittest.TestCase):
    def kinds(self, regions):
        return {issue["kind"] for issue in inspect_stage(regions)["issues"]}

    def test_valid_region_with_clockwise_hole(self):
        wall = region(rectangle(0, 0, 10, 10), [rectangle(2, 2, 4, 4)[::-1]])
        self.assertEqual(self.kinds([wall]), set())

    def test_crossing_without_duplicate_vertices(self):
        self.assertIn("proper_crossing", self.kinds([region([(0, 0), (4, 4), (0, 4), (4, 0)])]))

    def test_actual_microscopic_crossing(self):
        points = [(-19.701397417179329, 23.070325900514590), (-19.513894934338488, 23.172633592219313), (-19.513894934338520, 23.172633592219300), (-19.513894934338477, 23.172633592219313)]
        self.assertIn("proper_crossing", self.kinds([region(points)]))

    def test_adjacent_backtracking_edges_overlap(self):
        points = [(0, 0), (4, 0), (2, 0), (2, 2), (0, 2)]
        self.assertIn("collinear_overlap", self.kinds([region(points)]))

    def test_vertex_on_nonadjacent_edge(self):
        points = [(0, 0), (4, 0), (4, 4), (2, 0), (0, 4)]
        self.assertIn("vertex_on_edge", self.kinds([region(points)]))

    def test_hole_outside_outer(self):
        self.assertIn("hole_outside_outer", self.kinds([region(rectangle(0, 0, 10, 10), [rectangle(20, 20, 22, 22)])]))

    def test_hole_touching_outer(self):
        self.assertIn("collinear_overlap", self.kinds([region(rectangle(0, 0, 10, 10), [rectangle(0, 2, 2, 4)])]))

    def test_nested_holes(self):
        holes = [rectangle(2, 2, 8, 8), rectangle(3, 3, 4, 4)]
        self.assertIn("nested_holes", self.kinds([region(rectangle(0, 0, 10, 10), holes)]))

    def test_crossing_between_different_regions(self):
        self.assertIn("proper_crossing", self.kinds([region(rectangle(0, 0, 4, 4)), region(rectangle(2, 2, 6, 6))]))

    def test_zero_length_edge(self):
        self.assertIn("zero_length_edge", self.kinds([region([(0, 0), (1, 0), (1, 0), (0, 1)])]))

    def test_parser_preserves_loop_order_and_double(self):
        payload = "SCHEMA BreachSimpleCollisionFailure 1\nAREA_THRESHOLD 450\nSTAGE A_RemainingRegions\nREGION 0 AREA 4\nLOOP -1 COUNT 3\n1.0000000000000002 2\n3 2\n3 4\nTRIANGULATION stage=A_RemainingRegions region=0 success=1\nMISSING stage=D_CollisionRegions region=0 loop=-1 edge=(1,2)\n"
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "snapshot.txt"
            path.write_text(payload, encoding="utf-8")
            parsed = parse_snapshot(path)
        self.assertEqual(parsed["stages"][0]["regions"][0]["outer"], [(1.0000000000000002, 2.0), (3.0, 2.0), (3.0, 4.0)])
        self.assertEqual(parsed["metadata"]["AREA_THRESHOLD"], "450")
        self.assertEqual(len(parsed["missing_edges"]), 1)


if __name__ == "__main__":
    unittest.main()
