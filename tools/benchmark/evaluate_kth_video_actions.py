"""Evaluate the existing temporal action model on frozen KTH cases.

Scores remain uncalibrated evidence. Static/reversed controls and action-vector
retrieval are evaluated on identical clips, with all unavailable cases retained.
"""
import argparse
import collections
import json
import math
from pathlib import Path
import re

from evaluate_kth_controls import read, sha256
from evaluate_quality import evaluate

POLICY = Path(__file__).with_name("kth-semantic-policy.json")


def decision(scores, frozen, kind, policy):
    index = policy[kind]["index"]
    return (scores[index] >= policy["minimumTemporalScore"]
            and scores[index] - frozen[index] >= policy["minimumFrozenMargin"]
            and all(scores[index] - scores[other] >= policy["minimumCompetitorMargin"]
                    for other in policy[kind]["competitors"]))


def binary_metrics(outcomes):
    count = collections.Counter("tp" if target and predicted else "fn" if target else "fp" if predicted else "tn"
                                for target, predicted in outcomes)
    tp, fp, fn, tn = (count[key] for key in ("tp", "fp", "fn", "tn"))
    return {"tp": tp, "fp": fp, "fn": fn, "tn": tn,
            "precision": tp / (tp + fp) if tp + fp else 0.,
            "recall": tp / (tp + fn) if tp + fn else 0.,
            "f1": 2 * tp / (2 * tp + fp + fn) if 2 * tp + fp + fn else 0.}


def normalize(values):
    norm = math.sqrt(sum(value * value for value in values))
    return [value / norm for value in values] if norm else None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cases-root", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    manifest = read(args.cases_root / "manifest.json")
    policy = read(POLICY)
    cases = {}
    for batch in manifest["batches"]:
        for row in read(args.cases_root / batch):
            if row["id"] in cases:
                raise ValueError("Duplicate case ID")
            cases[row["id"]] = row
    for row in manifest["unavailable"]:
        if row["id"] in cases:
            raise ValueError("Unavailable case collides with observed case")
        cases[row["id"]] = dict(row, expectedDatasetAction=row["action"])
    if len(cases) != manifest["planned"]:
        raise ValueError("Planned cases were omitted")
    predictions, input_hashes = {}, {}
    for batch in manifest["batches"]:
        path = args.cases_root / batch.replace("cases-", "predictions-")
        report = read(path)
        input_hashes[path.name] = sha256(path)
        if report["modelSha256"] != policy["modelSha256"]:
            raise ValueError("Action model differs from the frozen policy")
        for row in report["results"]:
            if row["id"] not in cases or row["id"] in predictions:
                raise ValueError("Unexpected or duplicate prediction")
            expected = cases[row["id"]]
            if any(row[key] != expected[key] for key in ("source", "start", "end")):
                raise ValueError("Action inference changed the prepared clip")
            for value in row["variants"].values():
                scores = value["scores"]
                if len(scores) != 80 or any(type(v) not in (int, float) or not math.isfinite(v) or not 0 <= v <= 1 for v in scores):
                    raise ValueError("Invalid action scores")
            predictions[row["id"]] = row
    if len(predictions) != manifest["available"]:
        raise ValueError("Missing model predictions; do not silently drop failures")
    decisions, walking, running, gait = {}, [], [], []
    per_action = collections.defaultdict(collections.Counter)
    representations = {name: {} for name in ("forward", "reverse", "repeat_midpoint", "positive_temporal_residual")}
    for case_id, row in cases.items():
        predicted = predictions.get(case_id)
        walk = run = False
        if predicted:
            variants = predicted["variants"]
            scores = variants["forward"]["scores"]
            frozen = variants["repeat_midpoint"]["scores"]
            walk, run = (decision(scores, frozen, kind, policy) for kind in ("walking", "running"))
            for name in ("forward", "reverse", "repeat_midpoint"):
                representations[name][case_id] = normalize(variants[name]["scores"])
            representations["positive_temporal_residual"][case_id] = normalize([max(0., a - b) for a, b in zip(scores, frozen)])
        if walk and run:
            raise ValueError("Mutually exclusive walk/run margins both passed")
        action = row["expectedDatasetAction"]
        expected_walk = action in policy["expectedWalkingActions"]
        expected_run = action in policy["expectedRunningActions"]
        walking.append((expected_walk, walk));running.append((expected_run, run))
        gait.append((expected_walk or expected_run, walk or run))
        label = "walking" if walk else "running" if run else "unknown"
        decisions[case_id] = label
        per_action[action][label] += 1
    retrieval = {}
    for name, embeddings in representations.items():
        queries = []
        for case_id, row in cases.items():
            identity = re.fullmatch(r"person(\d{2})_[a-z]+_d([1-4])_uncomp\.avi", Path(row["source"]).name)
            if not identity:
                raise ValueError("Unexpected KTH source identity")
            subject, scenario = identity.groups()
            candidates = []
            for other_id, other in cases.items():
                other_identity = re.fullmatch(r"person(\d{2})_[a-z]+_d([1-4])_uncomp\.avi", Path(other["source"]).name)
                if not other_identity:
                    raise ValueError("Unexpected candidate source identity")
                if other_identity[1] == subject and other_identity[2] != scenario:
                    candidates.append(other_id)
            positive = [key for key in candidates if cases[key]["expectedDatasetAction"] == row["expectedDatasetAction"]]
            negative = [key for key in candidates if cases[key]["expectedDatasetAction"] != row["expectedDatasetAction"]]
            vector = embeddings.get(case_id)
            ranked = sorted((key for key in candidates if vector and embeddings.get(key)),
                            key=lambda key: (-sum(a * b for a, b in zip(vector, embeddings[key])), key))
            queries.append({"id": case_id, "positiveIds": positive, "negativeIds": negative, "rankedIds": ranked})
        retrieval[name] = evaluate({"scope": "Raw 80-score action representation; known subject and cross-scenario six-action proxy", "queries": queries}, ks=(1, 3, 5))
    walking_metrics, running_metrics = binary_metrics(walking), binary_metrics(running)
    summary = {"schemaVersion": 1, "split": manifest["split"], "scope": manifest["scope"],
               "caseCount": len(cases), "unavailable": manifest["unavailable"], "policySha256": sha256(POLICY),
               "predictionFileSha256": input_hashes, "walking": walking_metrics, "running": running_metrics,
               "gait": binary_metrics(gait), "supportedActionMacroF1": (walking_metrics["f1"] + running_metrics["f1"]) / 2,
               "unknownRate": sum(value == "unknown" for value in decisions.values()) / len(cases),
               "labelsByDatasetAction": {key: dict(value) for key, value in per_action.items()},
               "representations": retrieval}
    args.output.write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({key: summary[key] for key in ("split", "caseCount", "walking", "running", "gait", "supportedActionMacroF1", "unknownRate")}, indent=2))


if __name__ == "__main__":
    main()
