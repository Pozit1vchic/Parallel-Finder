import unittest
from evaluate_quality import evaluate
from evaluate_identity_controls import evaluate as evaluate_identities


class QualityMetricsTests(unittest.TestCase):
    def test_identity_control_counts_misses_and_false_merges(self):
        a={"time":0,"box":[0,0,10,10]};b={"time":1,"box":[0,0,10,10]}
        assignments=[dict(a,identity="same"),dict(b,identity="same")]
        labels={"scope":"test","controls":[{"id":"different-people","left":a,"right":b,"sameIdentity":False}]}
        self.assertEqual(evaluate_identities(assignments,labels)["counts"]["falsePositive"],1)

    def test_identity_control_refuses_to_drop_an_unresolved_anchor(self):
        a={"time":0,"box":[0,0,10,10]}
        labels={"scope":"test","controls":[{"id":"known","left":a,"right":a,"sameIdentity":True}]}
        with self.assertRaises(ValueError):
            evaluate_identities([],labels)
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
