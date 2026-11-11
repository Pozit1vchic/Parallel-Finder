"""Score a fully enumerated, provenance-checked top-10 visual prediction audit.

This is separate from independent positive-control Recall and never estimates
episode-wide Precision. Grade 1 denotes weak evidence, not a strict positive.
"""
import argparse
import hashlib
import json
from pathlib import Path


IDENTITY_FIELDS = (
    "id", "leftSource", "rightSource", "leftTrackId", "rightTrackId",
    "leftStart", "leftEnd", "rightStart", "rightEnd", "matchType",
    "headOnlyComparison",
)


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def load(path):
    return json.loads(Path(path).read_text(encoding="utf-8-sig"))


def score_grades(grades):
    if len(grades) != 10 or any(type(g) is not int and g != "unknown" for g in grades):
        raise ValueError("Require exactly ten explicit integer or unknown grades")
    if any(g not in (0, 1, 2, "unknown") for g in grades):
        raise ValueError("Invalid grade")
    counts = {str(g): grades.count(g) for g in (0, 1, 2, "unknown")}
    strict, weak, unresolved = counts["2"], counts["1"], counts["unknown"]
    return {
        "count": 10, "gradeCounts": counts,
        "strictPrecisionAt10Bounds": [strict / 10, (strict + unresolved) / 10],
        "lenientPrecisionAt10Bounds": [(strict + weak) / 10, (strict + weak + unresolved) / 10],
        "boundsAre": "Unresolved-label bounds, not statistical confidence intervals",
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--plan", type=Path, required=True)
    parser.add_argument("--labels", type=Path, required=True)
    parser.add_argument("--before", type=Path, required=True)
    parser.add_argument("--after", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if args.output.exists():
        raise ValueError("Use a new output artifact")
    labels = load(args.labels)
    if labels["planSha256"] != digest(args.plan):
        raise ValueError("Judgments do not match the frozen review plan")
    annotations = labels["judgments"]
    if len(annotations) != 20:
        raise ValueError("Require all ten predictions for both versions")
    scores = {}
    for variant in ("before", "after"):
        report_path = getattr(args, variant)
        if labels["reportSha256"][variant] != digest(report_path):
            raise ValueError("Report changed since annotation")
        results = load(report_path)["results"]
        selected = [a for a in annotations if a["variant"] == variant]
        if sorted(a["returnedArrayIndex"] for a in selected) != list(range(10)):
            raise ValueError("Missing, duplicate or out-of-range prediction index")
        selected.sort(key=lambda a: a["returnedArrayIndex"])
        for annotation in selected:
            record = results[annotation["returnedArrayIndex"]]
            if annotation["record"] != {k: record[k] for k in IDENTITY_FIELDS}:
                raise ValueError("Source/track/interval/scope provenance mismatch")
            if not annotation["notes"].strip():
                raise ValueError("Missing visual explanation")
            if annotation["grade"] != "unknown" and annotation["trackAssociationVerified"] is not True:
                raise ValueError("A graded prediction needs matched-track verification")
            if {a["kind"] for a in annotation["visualArtifacts"]} != {"temporal", "matchedTrackOverlay"}:
                raise ValueError("Require temporal and matched-track visual evidence")
            for artifact in annotation["visualArtifacts"]:
                if digest(artifact["path"]) != artifact["sha256"]:
                    raise ValueError("Reviewed visual artifact changed")
        scores[variant] = score_grades([a["grade"] for a in selected])
    output = {
        "schemaVersion": 1, "scope": load(args.plan)["scope"],
        "reviewer": labels["reviewer"], "independentHumanConfirmation": False,
        "labelsSha256": digest(args.labels), "planSha256": digest(args.plan),
        "reportSha256": labels["reportSha256"], "metrics": scores,
        "limits": "Top ten returned predictions only; assistant judgments; not random sampling, episode-wide Precision, independent truth or confidence calibration. Weak grade 1 is not a strict positive. Do not tune on these held-out judgments.",
    }
    args.output.write_text(json.dumps(output, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
