import unittest

from audit_repeated_motion_identities import audit


def window(track, scene, start, *, shift=0, body=None, repeated=False):
    frames = []
    for i in range(3):
        time = start if repeated else start + i * .25
        frames.append({"time": time, "points": [[shift, 0, 1], [shift+50, 0, 1], [shift, 100, 1]]})
    return {"track": track, "scene": scene, "body": body or [1., 0.], "frames": frames,
            "sequencePts": [start+i*.25 for i in range(3)], "sequence": [.5] * (3*432)}


class RepeatedMotionAuditTests(unittest.TestCase):
    def test_independent_positions_and_body_are_required(self):
        a = window(1, 1, 0)
        b = window(2, 2, 10, shift=2)
        assignments = [{"track": "1", "identity": "1"}, {"track": "2", "identity": "2"}]
        neighbour = [{"leftScene": 1, "rightScene": 2, "sampleDifference": 0}]
        self.assertEqual(len(audit([a, b], assignments, neighbour)), 1)
        self.assertEqual(audit([a, window(2, 2, 10, shift=300)], assignments, neighbour), [])
        self.assertEqual(audit([a, window(2, 2, 10, body=[0., 1.])], assignments, neighbour), [])
        self.assertEqual(audit([a, window(2, 2, 10, repeated=True)], assignments, neighbour), [])


if __name__ == "__main__":
    unittest.main()
