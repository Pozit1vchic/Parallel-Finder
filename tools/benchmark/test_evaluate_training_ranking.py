import math
import unittest

from evaluate_training_ranking import evaluate, transfer_judgments
from score_prediction_review import IDENTITY_FIELDS


class TrainingPreferenceTests(unittest.TestCase):
    def test_transfer_tracks_comparisons_instead_of_presentation_ids(self):
        record = {field: field for field in IDENTITY_FIELDS}
        record["id"] = 0
        label = {"returnedArrayIndex": 0, "grade": 2, "record": record}
        candidate = [{**record, "leftTrackId": "other", "id": 0}, {**record, "id": 1}]
        moved = transfer_judgments([label], candidate)
        self.assertEqual(moved[0]["returnedArrayIndex"], 1)
        self.assertEqual(moved[0]["record"]["id"], 1)
        self.assertEqual(moved[0]["grade"], 2)
        self.assertEqual(label["returnedArrayIndex"], 0)
        self.assertEqual(label["record"]["id"], 0)

    def test_transfer_rejects_missing_changed_or_ambiguous_pairs(self):
        record = {field: field for field in IDENTITY_FIELDS}
        label = {"returnedArrayIndex": 0, "grade": 2, "record": record}
        for candidate in ([], [{**record, "leftStart": "different"}],
                          [{**record, "headOnlyComparison": "different"}], [record, record]):
            with self.subTest(candidate=candidate), self.assertRaises(ValueError):
                transfer_judgments([label], candidate)

    def test_scope_partition_exposes_a_cross_scope_only_gain(self):
        rows = [
            {"headOnlyComparison": True, "evidenceQuality": .95},
            {"headOnlyComparison": True, "evidenceQuality": .85},
            {"headOnlyComparison": False, "evidenceQuality": .1},
        ]
        labels = [{"returnedArrayIndex": i, "grade": grade}
                  for i, grade in enumerate((1, 2, 1))]
        all_scores = evaluate(rows, labels)["evidenceQuality"]
        self.assertEqual((all_scores["strictlyCorrect"], all_scores["comparable"]), (1, 2))
        head = evaluate(rows, labels, "withinHead")["evidenceQuality"]
        self.assertEqual((head["strictlyCorrect"], head["comparable"]), (0, 1))
        cross = evaluate(rows, labels, "crossScope")["evidenceQuality"]
        self.assertEqual((cross["strictlyCorrect"], cross["comparable"]), (1, 1))
        body = evaluate(rows, labels, "withinBody")["evidenceQuality"]
        self.assertEqual(body["totalPreferences"], 0)
        self.assertIsNone(body["strictPreferenceAccuracy"])

    def test_unknown_grade_does_not_become_a_negative(self):
        rows = [{"headOnlyComparison": True, "similarity": 1.},
                {"headOnlyComparison": True, "similarity": .2}]
        result = evaluate(rows, [{"returnedArrayIndex": 0, "grade": "unknown"},
                                 {"returnedArrayIndex": 1, "grade": 2}])
        self.assertEqual(result["similarity"]["totalPreferences"], 0)
        self.assertIsNone(result["returnedOrder"]["strictPreferenceAccuracy"])

    def test_unavailable_measurements_abstain_and_ties_do_not_win(self):
        labels = [{"returnedArrayIndex": 0, "grade": 2},
                  {"returnedArrayIndex": 1, "grade": 1}]
        for missing in (-1., math.nan, math.inf, None):
            rows = [{"headOnlyComparison": True, "poseSimilarity": missing},
                    {"headOnlyComparison": True, "poseSimilarity": .8}]
            result = evaluate(rows, labels)["poseSimilarity"]
            self.assertEqual((result["unavailable"], result["comparable"]), (1, 0))
            self.assertIsNone(result["strictPreferenceAccuracy"])
        rows = [{"headOnlyComparison": True, "similarity": .8}] * 2
        result = evaluate(rows, labels)["similarity"]
        self.assertEqual((result["strictlyCorrect"], result["tied"]), (0, 1))
        self.assertEqual(result["strictPreferenceAccuracy"], 0.)

    def test_bad_scope_is_rejected(self):
        with self.assertRaises(ValueError):
            evaluate([], [], "fittedScope")
        with self.assertRaises(ValueError):
            evaluate([{"headOnlyComparison": "false"}, {"headOnlyComparison": True}],
                     [{"returnedArrayIndex": 0, "grade": 2},
                      {"returnedArrayIndex": 1, "grade": 1}], "crossScope")


if __name__ == "__main__":
    unittest.main()
