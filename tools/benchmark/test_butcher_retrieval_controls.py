import unittest

from run_butcher_retrieval_controls import application_rankings, probe_rankings, queries
from evaluate_quality import evaluate


REFERENCE = {"candidates": [
    {"id":"a", "sceneStart":1, "sceneEnd":2, "positiveIds":["b"], "negativeIds":["c"]},
    {"id":"b", "sceneStart":3, "sceneEnd":4, "positiveIds":["a"], "negativeIds":["c"]},
    {"id":"c", "sceneStart":5, "sceneEnd":6, "positiveIds":[], "negativeIds":[]} ]}


class TestControls(unittest.TestCase):
    def test_family_members_are_reviewed_and_unknown_results_stay_unjudged(self):
        results = [{"headOnlyComparison":False,"leftSceneStart":10,"leftSceneEnd":11,
                    "leftFamily":{"members":[{"sceneStart":1,"sceneEnd":2}]},
                    "rightSceneStart":15,"rightSceneEnd":16,"rightFamily":{}}]
        result = queries(REFERENCE, application_rankings(REFERENCE, results))
        self.assertEqual(result[0]["rankedIds"], ["unjudged:15.000000-16.000000"])
        metrics = evaluate({"queries":result},ks=(1,))["metrics"]
        self.assertEqual(metrics["unjudgedRetrieved"],1)
        self.assertIsNone(metrics["Precision@1"])
        self.assertEqual(metrics["Recall@1"],0)

    def test_probe_pairs_are_symmetric_and_head_only_is_excluded(self):
        report = {"windowCount":3,"results":[
            {"leftIndex":0,"rightIndex":1,"headOnlyComparison":False},
            {"leftIndex":1,"rightIndex":2,"headOnlyComparison":True}]}
        ranks = probe_rankings(REFERENCE, report)
        self.assertEqual(ranks,{"a":["b"],"b":["a"],"c":[]})
        self.assertEqual(evaluate({"queries":queries(REFERENCE,ranks)},ks=(1,))["metrics"]["Recall@1"],1)


if __name__ == "__main__": unittest.main()
