import unittest
from evaluate_quality import evaluate


class QualityMetricsTests(unittest.TestCase):
    def test_missing_positive_and_false_positive_affect_ranking_metrics(self):
        metrics = evaluate({"queries": [{"id": "q", "positiveIds": ["a", "b"],
            "negativeIds": ["c"], "rankedIds": ["c", "a"]}]}, ks=(1, 2))["metrics"]
        self.assertEqual(metrics["Recall@1"], 0)
        self.assertEqual(metrics["Precision@2"], .5)
        self.assertEqual(metrics["Recall@2"], .5)
        self.assertEqual(metrics["MRR"], .5)

    def test_unjudged_is_not_fabricated_negative(self):
        metrics = evaluate({"queries": [{"id": "q", "positiveIds": ["a"],
            "negativeIds": [], "rankedIds": ["unreviewed", "a"]}]}, ks=(1,))["metrics"]
        self.assertIsNone(metrics["Precision@1"])
        self.assertEqual(metrics["unjudgedRetrieved"], 1)

    def test_conflicting_labels_are_errors(self):
        with self.assertRaises(ValueError):
            evaluate({"queries": [{"id": "q", "positiveIds": ["a"],
                "negativeIds": ["a"], "rankedIds": ["a"]}]})


if __name__ == "__main__":
    unittest.main()
