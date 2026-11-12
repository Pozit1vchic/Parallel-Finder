import copy
import unittest

from verify_identity_independent_ranking import verify


class RankingPolicyVerificationTests(unittest.TestCase):
    def setUp(self):
        self.after = []
        for name, similarity, score, identity in (("a", .9, .819, False), ("b", .8, .728, True), ("c", .7, .637, True)):
            self.after.append({
                "leftSource": "left", "rightSource": name, "leftTrackId": 1, "rightTrackId": 1,
                "leftStart": 0., "leftEnd": 2., "rightStart": 10., "rightEnd": 12.,
                "similarity": similarity, "rankScore": similarity, "evidenceQuality": .8,
                "identityVerified": identity, "faceVerified": False, "identityEvidenceRequired": False,
                "leftSceneFamily": "shared" if name in "ac" else "other", "rightSceneFamily": "right-" + name,
                "classification": "body_pose", "bestMatchScore": score, "matchTier": "strong",
                "redundancyPenalty": .04 if name == "c" else 0.,
                "id": len(self.after), "continuityRank": len(self.after),
            })
        self.before = copy.deepcopy([self.after[1], self.after[0], self.after[2]])
        for i, row in enumerate(self.before):
            row["id"] = row["continuityRank"] = i
            row.pop("identityEvidenceRequired")
            if not row["identityVerified"]:
                row["bestMatchScore"] = .69615
                row["matchTier"] = "possible"

    def test_valid_policy_change_preserves_measurements(self):
        self.assertEqual(verify(self.before, self.after)["changedPositions"], 2)

    def test_changed_classification_is_not_a_ranking_change(self):
        self.after[0]["classification"] = "different"
        with self.assertRaisesRegex(ValueError, "Measured or classification"):
            verify(self.before, self.after)

    def test_wrong_score_is_rejected(self):
        self.after[0]["bestMatchScore"] += .1
        with self.assertRaisesRegex(ValueError, "score formula"):
            verify(self.before, self.after)

    def test_identity_tier_and_missing_edit_policy_are_rejected(self):
        self.after[0]["matchTier"] = "possible"
        with self.assertRaisesRegex(ValueError, "tier still depends"):
            verify(self.before, self.after)
        self.after[0]["matchTier"] = "strong"
        self.after[0].pop("identityEvidenceRequired")
        with self.assertRaisesRegex(ValueError, "retained for range edits"):
            verify(self.before, self.after)

    def test_membership_and_duplicates_are_rejected(self):
        with self.assertRaisesRegex(ValueError, "membership"):
            verify(self.before, self.after[:-1])
        with self.assertRaisesRegex(ValueError, "duplicates"):
            verify(self.before, self.after + [self.after[0]])

    def test_diversity_penalty_is_checked(self):
        self.after[2]["redundancyPenalty"] = 0.
        with self.assertRaisesRegex(ValueError, "penalty mismatch"):
            verify(self.before, self.after)

    def test_weaker_greedy_selection_is_rejected(self):
        self.after[0], self.after[1] = self.after[1], self.after[0]
        for i, row in enumerate(self.after):
            row["id"] = i
        with self.assertRaisesRegex(ValueError, "weaker remaining"):
            verify(self.before, self.after)


if __name__ == "__main__":
    unittest.main()
