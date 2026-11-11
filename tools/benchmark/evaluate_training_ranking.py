"""Audit existing ranking fields against partial, training-only preferences.

Reports preference coverage and ordering, not Precision or calibrated relevance.
No weights are fitted and no held-out judgments are loaded.
"""
import argparse
import itertools
import json
import math

from score_prediction_review import IDENTITY_FIELDS, digest, load


def evaluate(report, judgments):
    known = [j for j in judgments if j["grade"] != "unknown"]
    preferences = [(a, b) for a, b in itertools.permutations(known, 2)
                   if a["grade"] > b["grade"]]
    fields = ["returnedOrder", "similarity", "rankScore", "bestMatchScore",
              "evidenceQuality", "poseSimilarity"]
    result = {}
    for field in fields:
        correct = tied = unavailable = 0
        violations = []
        for preferred, weaker in preferences:
            a, b = preferred["returnedArrayIndex"], weaker["returnedArrayIndex"]
            if field == "returnedOrder":
                sa, sb = -a, -b
            else:
                sa, sb = report[a].get(field), report[b].get(field)
                if any(type(s) not in (int, float) or not math.isfinite(s) or s < 0 for s in (sa, sb)):
                    unavailable += 1
                    continue
            if sa > sb:
                correct += 1
            elif sa == sb:
                tied += 1
            else:
                violations.append({"preferredReturnedIndex": a, "weakerReturnedIndex": b})
        comparable = len(preferences) - unavailable
        result[field] = {
            "totalPreferences": len(preferences), "comparable": comparable,
            "unavailable": unavailable, "strictlyCorrect": correct, "tied": tied,
            "violations": violations,
            "strictPreferenceAccuracy": correct / comparable if comparable else None,
        }
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--report", required=True)
    parser.add_argument("--labels", required=True)
    parser.add_argument("--output", required=True)
    args = parser.parse_args()
    from pathlib import Path
    output = Path(args.output)
    if output.exists():
        raise ValueError("Use a new output artifact")
    labels = load(args.labels)
    if labels["split"] != "tuning" or labels["reportSha256"] != digest(args.report):
        raise ValueError("Require training labels for this exact report")
    plan = load(labels["plan"])
    if labels["planSha256"] != digest(labels["plan"]):
        raise ValueError("Training review plan changed")
    rows = load(args.report)["results"]
    seen = set()
    for judgment in labels["judgments"]:
        index, grade = judgment["returnedArrayIndex"], judgment["grade"]
        if type(index) is not int or index in seen or not 0 <= index < len(rows):
            raise ValueError("Invalid or duplicate selected index")
        seen.add(index)
        if grade != "unknown" and (type(grade) is not int or grade not in (0, 1, 2)):
            raise ValueError("Invalid relevance grade")
        if judgment["record"] != {k: rows[index][k] for k in IDENTITY_FIELDS}:
            raise ValueError("Selected source/track/interval changed")
        if grade != "unknown" and judgment["trackAssociationVerified"] is not True:
            raise ValueError("Graded preference requires matched-track verification")
        if not judgment["notes"].strip() or {a["kind"] for a in judgment["visualArtifacts"]} != {"temporal", "matchedTrackOverlay"}:
            raise ValueError("Require explained temporal and matched-track evidence")
        for artifact in judgment["visualArtifacts"]:
            if digest(artifact["path"]) != artifact["sha256"]:
                raise ValueError("Visual evidence changed")
    if sorted(seen) != sorted(plan["returnedArrayIndices"]):
        raise ValueError("Judgments do not cover the frozen selected indices")
    result = {
        "scope": "Partial selected training preferences; no fitting or held-out evaluation",
        "reportSha256": digest(args.report), "labelsSha256": digest(args.labels),
        "selectionSize": len(seen), "metrics": evaluate(rows, labels["judgments"]),
        "limits": "One selected strong-positive anchor and a selected weak/unknown sample do not establish overall relevance accuracy, Precision, calibration or a replacement ranking policy.",
    }
    output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
