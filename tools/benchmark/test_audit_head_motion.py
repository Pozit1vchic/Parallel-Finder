import math
import unittest
from audit_head_motion import valid_rotation, rotation_distance, summarize_side, preference_counts


def yaw(degrees):
    t = math.radians(degrees)
    return [math.cos(t), 0, math.sin(t), 0, 1, 0, -math.sin(t), 0, math.cos(t)]


def sample(time, degrees=0, observed=True):
    return {"time": time, "rotation": yaw(degrees) if observed else [],
            "observed": observed, "cacheStatus": "hit"}


class HeadMotionAuditTests(unittest.TestCase):
    def test_so3_uses_short_rotation_across_the_yaw_wrap(self):
        self.assertAlmostEqual(rotation_distance(yaw(179), yaw(-169)), 12)
        self.assertAlmostEqual(rotation_distance(yaw(-20), yaw(30)), 50)
        self.assertFalse(valid_rotation([1, 0, 0, 0, 1, 0, 0, 0, -1]))
        self.assertFalse(valid_rotation([0.] * 9))
        with self.assertRaises(ValueError):
            rotation_distance([math.nan] * 9, yaw(0))

    def test_exclusive_end_cannot_supply_motion(self):
        result = summarize_side({"start": 0, "end": 1,
                                 "samples": [sample(0), sample(.5), sample(1 - .5e-6, 70), sample(1, 90)]})
        self.assertEqual(result["excludedAtOrAfterEnd"], 2)
        self.assertAlmostEqual(result["excursionDegrees"], 0)
        self.assertAlmostEqual(result["netAngleDegrees"], 0)

    def test_explicit_gap_is_not_interpolated_or_filled_as_frontal(self):
        result = summarize_side({"start": 0, "end": 1,
                                 "samples": [sample(0, 0), sample(.2, 10), sample(.4, observed=False), sample(.6, 50), sample(.8, 60)]})
        self.assertEqual(result["validRunLengths"], [2, 2])
        self.assertAlmostEqual(result["excursionDegrees"], 10)
        self.assertAlmostEqual(result["withinRunPathDegrees"], 20)
        self.assertAlmostEqual(result["netAngleDegrees"], 60)
        self.assertTrue(result["netEndpointsAcrossExplicitGap"])

    def test_invalid_matrix_and_single_frame_are_not_measured_static(self):
        bad = sample(.5); bad["rotation"] = [0.] * 9
        result = summarize_side({"start": 0, "end": 1, "samples": [sample(0), bad]})
        self.assertEqual(result["invalidObservedRotations"], 1)
        self.assertIsNone(result["netAngleDegrees"])
        self.assertIsNone(result["excursionDegrees"])
        with self.assertRaises(ValueError):
            summarize_side({"start": 0, "end": 1, "samples": [sample(.5), sample(.4)]})

    def test_unknown_and_unavailable_do_not_become_negative_preferences(self):
        cases = [{"grade": 2, "currentPosition": 3, "score": None},
                 {"grade": 1, "currentPosition": 0, "score": 4.},
                 {"grade": "unknown", "currentPosition": 1, "score": 100.}]
        stats = preference_counts(cases, "score")
        self.assertEqual((stats["totalPreferences"], stats["unavailable"], stats["comparable"]), (1, 1, 0))
        self.assertIsNone(stats["strictPreferenceAccuracy"])
        self.assertEqual(preference_counts(cases, "currentReturnedOrder")["strictlyCorrect"], 0)


if __name__ == "__main__":
    unittest.main()
