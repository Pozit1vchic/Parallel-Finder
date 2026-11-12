"""Verify native head extent rows against a frozen audit and historical report.

Emits a native ResultsRail fixture for both existing evidence tiers. This is
prediction-scope ranking validation, not full Matcher precision or recall.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path

from verify_identity_independent_ranking import verify


def load(path):
    return json.loads(Path(path).read_text(encoding="utf-8-sig"))


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def identity(row):
    return tuple(row[k] for k in ("leftSource", "rightSource", "leftTrackId", "rightTrackId",
        "leftStart", "leftEnd", "rightStart", "rightEnd", "matchType", "headOnlyComparison"))


def preferences(order, grades):
    positions = {identity(row): i for i, row in enumerate(order)}
    available = {key: grade for key, grade in grades.items() if key in positions}
    pairs = [(a, b) for a in available for b in available if available[a] > available[b]]
    return {"preferences": len(pairs),
            "correct": sum(positions[a] < positions[b] for a, b in pairs)}


def check(reference, candidate, audit):
    rows = candidate["results"]
    index = {identity(row): row for row in rows}
    if len(index) != len(rows):
        raise ValueError("Duplicate oriented result identities")
    expected, grades = {}, {}
    for case in audit["cases"]:
        original = reference["results"][case["returnedArrayIndex"]]
        key = identity(original)
        if key in expected:
            raise ValueError("Repeated audited identity")
        expected[key] = case["minExcursionDegrees"]
        if type(case["grade"]) is int:
            grades[key] = case["grade"]
    if not expected.keys() <= index.keys():
        raise ValueError("Audited pair missing from native output")
    for row in rows:
        target = expected.get(identity(row))
        target = -1. if target is None else target
        value = row["headMotionExcursionDegrees"]
        if type(value) not in (int, float) or not math.isfinite(value) or not math.isclose(value, target, rel_tol=0, abs_tol=1e-6):
            raise ValueError(f"Native angular measurement mismatch for id {row['id']}")
    # Only after validating every added field may the historical policy verifier
    # exclude it from its exact measured/classification-field comparison.
    compatibility = verify(reference["results"], [
        {k: v for k, v in row.items() if k != "headMotionExcursionDegrees"} for row in rows])
    fixtures, metrics = [], {}
    for tier in ("strong", "possible"):
        before = sorted((row for row in rows if row["matchTier"] == tier),
            key=lambda row: (-(row["bestMatchScore"] - row["redundancyPenalty"]), row["id"]))
        slots = [i for i, row in enumerate(before) if row["evidenceScope"] == "head"
            and row["matchType"] == "motion" and row["headMotionExcursionDegrees"] >= 0]
        after = list(before)
        ordered = sorted((before[i] for i in slots), key=lambda row: -row["headMotionExcursionDegrees"])
        for i, row in zip(slots, ordered):
            after[i] = row
        fixtures.append({"tier": tier, "ids": [row["id"] for row in after]})
        metrics[tier] = {"before": preferences(before, grades), "after": preferences(after, grades)}
    return {"compatibility": compatibility, "auditedMeasurements": len(expected),
        "tierPreferences": metrics, "limits": "Assistant judgments of returned head predictions; no independent human confirmation or full retrieval metrics."}, {
        "results": rows, "expectedBestOrders": fixtures}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("reference", "candidate", "audit", "labels", "output", "ui-fixture"):
        parser.add_argument("--" + name, required=True)
    args = parser.parse_args()
    if Path(args.output).exists() or Path(args.ui_fixture).exists():
        raise ValueError("Use new output paths")
    audit = load(args.audit)
    if digest(args.reference) != audit["referenceReportSha256"] or audit["labelsSha256"] != {args.labels: digest(args.labels)}:
        raise ValueError("Frozen reference/labels hash mismatch")
    labels = load(args.labels)
    if labels["planSha256"] != audit["planSha256"] or labels["reportSha256"] != digest(args.reference):
        raise ValueError("Frozen plan/report binding mismatch")
    if {c["returnedArrayIndex"]: c["grade"] for c in audit["cases"]} != {
            c["returnedArrayIndex"]: c["grade"] for c in labels["judgments"]}:
        raise ValueError("Audited cases differ from complete frozen judgments")
    result, fixture = check(load(args.reference), load(args.candidate), audit)
    result["inputs"] = {name: digest(getattr(args, name)) for name in ("reference", "candidate", "audit", "labels")}
    fixture["provenance"] = result["inputs"]
    Path(args.output).write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    Path(args.ui_fixture).write_text(json.dumps(fixture, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
