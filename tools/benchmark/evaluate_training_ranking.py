"""Audit existing ranking fields against partial, training-only preferences.

Reports preference coverage and ordering, not Precision or calibrated relevance.
No weights are fitted and no held-out judgments are loaded.
"""
import argparse
import itertools
import json
import math

from score_prediction_review import IDENTITY_FIELDS, digest, load


def transfer_judgments(judgments, candidate):
    """Transfer only exact oriented source/track/range/scope identities.

    This does not verify source file contents or supply labels for new pairs.
    Missing/ambiguous identities are errors rather than silently excluded data.
    """
    fields = [field for field in IDENTITY_FIELDS if field != "id"]
    key = lambda row: tuple(row[field] for field in fields)
    positions = {}
    for index, row in enumerate(candidate):
        identity = key(row)
        if identity in positions:
            raise ValueError("Ambiguous candidate comparison identity")
        positions[identity] = index
    transferred = []
    for judgment in judgments:
        identity = key(judgment["record"])
        if identity not in positions:
            raise ValueError("Selected comparison missing or changed in candidate")
        index = positions[identity]
        transferred.append({**judgment, "returnedArrayIndex": index,
                            "record": {field: candidate[index][field] for field in IDENTITY_FIELDS}})
    return transferred


def evaluate(report, judgments, preference_scope="all"):
    if preference_scope not in ("all", "withinHead", "withinBody", "crossScope"):
        raise ValueError("Invalid preference scope")
    known = [j for j in judgments if j["grade"] != "unknown"]
    preferences = [(a, b) for a, b in itertools.permutations(known, 2)
                   if a["grade"] > b["grade"]]
    if preference_scope != "all":
        def included(pair):
            a, b = (report[j["returnedArrayIndex"]]["headOnlyComparison"] for j in pair)
            if type(a) is not bool or type(b) is not bool:
                raise ValueError("Comparison scope must be an explicit boolean")
            return ((a and b) if preference_scope == "withinHead" else
                    (not a and not b) if preference_scope == "withinBody" else a != b)
        preferences = [pair for pair in preferences if included(pair)]
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
    parser.add_argument("--candidate-report", help="Evaluate unchanged comparison identities in a different returned order")
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
    judgments = labels["judgments"]
    transfer = None
    if args.candidate_report:
        rows = load(args.candidate_report)["results"]
        judgments = transfer_judgments(judgments, rows)
        transfer = [{"referenceReturnedIndex": old["returnedArrayIndex"],
                     "candidateReturnedIndex": new["returnedArrayIndex"]}
                    for old, new in zip(labels["judgments"], judgments)]
    result = {
        "scope": "Partial selected training preferences; no fitting or held-out evaluation",
        "reportSha256": digest(args.candidate_report or args.report),
        "annotationReferenceReportSha256": digest(args.report),
        "labelsSha256": digest(args.labels), "judgmentTransfer": transfer,
        "transferLimit": ("Exact comparison identity only; source-content integrity must be verified separately."
                          if transfer is not None else None),
        "selectionSize": len(seen), "metrics": evaluate(rows, judgments),
        "scopeMetrics": {scope: evaluate(rows, judgments, scope)
                         for scope in ("withinHead", "withinBody", "crossScope")},
        "gradeCounts": {str(grade): sum(j["grade"] == grade for j in labels["judgments"])
                        for grade in (0, 1, 2, "unknown")},
        "limits": "Selected and potentially correlated training judgments do not establish overall relevance accuracy, Precision, calibration or a replacement ranking policy.",
    }
    output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
