import unittest

from run_matcher_labelled_baseline import make_queries
from evaluate_quality import evaluate


class MatcherLabelledBaselineTests(unittest.TestCase):
    def test_both_directions_keep_actual_result_order(self):
        reference = {"candidates": [
            {"id": "a", "index": 0, "positiveIds": ["c"], "negativeIds": ["b"]},
            {"id": "b", "index": 1, "positiveIds": [], "negativeIds": ["a"]},
            {"id": "c", "index": 2, "positiveIds": ["a"], "negativeIds": []},
        ]}
        report = {"windowCount": 3, "results": [
            {"leftIndex": 0, "rightIndex": 1}, {"leftIndex": 2, "rightIndex": 0}]}
        queries = make_queries(reference, report)
        self.assertEqual(queries[0]["rankedIds"], ["b", "c"])
        self.assertEqual(queries[2]["rankedIds"], ["a"])
        metrics = evaluate({"queries": queries}, ks=(1,))["metrics"]
        self.assertEqual(metrics["MRR"], .75)
        self.assertEqual(metrics["Recall@1"], .5)

    def test_invalid_returned_indices_fail(self):
        reference = {"candidates": [{"id": "a", "index": 0, "positiveIds": [], "negativeIds": []}]}
        with self.assertRaises(ValueError):
            make_queries(reference, {"windowCount": 1, "results": [{"leftIndex": 0, "rightIndex": 2}]})


if __name__ == "__main__":
    unittest.main()
