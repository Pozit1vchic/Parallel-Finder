import unittest

try:
    from .audit_duplicate_result_intervals import find_duplicate_intervals
except ImportError:
    from audit_duplicate_result_intervals import find_duplicate_intervals


def result(identifier, left, right, left_range, right_range):
    return {
        "id": identifier, "leftSource": left, "rightSource": right,
        "leftSceneStart": left_range[0], "leftSceneEnd": left_range[1],
        "rightSceneStart": right_range[0], "rightSceneEnd": right_range[1],
    }


class DuplicateIntervalAuditTests(unittest.TestCase):
    def test_detects_reversed_same_pair_with_scene_overlap(self):
        rows = [
            result(3, r"D:\a.mp4", r"D:\b.mp4", (10, 12), (30, 34)),
            result(9, r"d:\B.mp4", r"d:\A.mp4", (30.1, 34), (10.1, 12)),
        ]
        found = find_duplicate_intervals(rows)
        self.assertEqual(len(found), 1)
        self.assertEqual(found[0]["orientation"], "swapped")
        self.assertGreaterEqual(found[0]["overlapLeft"], .7)
        self.assertGreaterEqual(found[0]["overlapRight"], .7)

    def test_does_not_collapse_only_one_shared_side(self):
        rows = [
            result(1, "a", "b", (10, 12), (30, 34)),
            result(2, "a", "b", (10.1, 12), (40, 44)),
        ]
        self.assertEqual(find_duplicate_intervals(rows), [])

    def test_does_not_call_distinct_temporal_scenes_duplicates(self):
        rows = [
            result(1, "a", "b", (10, 12), (30, 34)),
            result(2, "a", "b", (20, 22), (50, 54)),
        ]
        self.assertEqual(find_duplicate_intervals(rows), [])

    def test_falls_back_to_match_bounds_for_older_reports(self):
        row = {"id": 1, "leftSource": "a", "rightSource": "b",
               "leftStart": 1, "leftEnd": 2, "rightStart": 4, "rightEnd": 5}
        self.assertEqual(find_duplicate_intervals([row, row.copy()])[0]["firstId"], 1)

    def test_rejects_invalid_threshold(self):
        with self.assertRaises(ValueError):
            find_duplicate_intervals([], 0)


if __name__ == "__main__":
    unittest.main()
