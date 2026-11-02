import unittest

from audit_identity_covisions import audit


def row(track, box, identity="1", time=1.0):
    return dict(track=track, box=box, identity=identity, time=time)


class IdentityCovisionAuditTests(unittest.TestCase):
    def test_disjoint_same_identity_is_reported_once_per_track_pair(self):
        rows = [row("a", [0, 0, 10, 10]), row("b", [20, 0, 30, 10]),
                row("a", [0, 0, 10, 10], time=2), row("b", [20, 0, 30, 10], time=2)]
        self.assertEqual(audit(rows), [dict(identity="1", tracks=["a", "b"],
                                            observations=2, firstTime=1., lastTime=2.)])

    def test_different_or_unresolved_identities_are_ignored(self):
        self.assertEqual(audit([row("a", [0, 0, 10, 10]),
                                row("b", [20, 0, 30, 10], identity="2"),
                                row("c", [40, 0, 50, 10], identity="")]), [])

    def test_nested_boxes_are_not_assumed_to_be_different_people(self):
        self.assertEqual(audit([row("a", [0, 0, 100, 100]),
                                row("b", [20, 20, 30, 30])]), [])

    def test_invalid_zero_area_does_not_create_evidence(self):
        self.assertEqual(audit([row("a", [0, 0, 10, 10]),
                                row("b", [20, 0, 20, 10])]), [])

    def test_different_times_and_repeated_same_track_are_ignored(self):
        self.assertEqual(audit([row("a", [0, 0, 10, 10]),
                                row("a", [20, 0, 30, 10]),
                                row("b", [40, 0, 50, 10], time=1.01)]), [])


if __name__ == "__main__":
    unittest.main()
