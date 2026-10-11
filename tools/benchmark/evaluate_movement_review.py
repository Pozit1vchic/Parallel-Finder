"""Compare previously saved manual labels with read-only grouping probe output.

Provisional contact-sheet labels are deliberately reported as provisional;
this tool never treats them as independent professional accuracy ground truth.
"""
import argparse
import json
from pathlib import Path


def read(path):
    return json.loads(Path(path).read_text(encoding="utf-8-sig"))


def evaluate(review_path):
    review = read(review_path)
    directory = Path(review_path).parent
    predictions = {}
    for video in sorted({case["videoId"] for case in review["cases"]}):
        for region, suffix in (("Body", ""), ("Head", "-head")):
            rows = read(directory / f"independent-{video}{suffix}-predictions.json")["results"]
            for row in rows:
                key = (row["caseId"], region)
                if key in predictions:
                    raise ValueError(f"Duplicate prediction: {key}")
                predictions[key] = row
    comparisons = []
    excluded = []
    for case in review["cases"]:
        if case["status"] != "provisional-contact-sheet-label":
            excluded.append({"caseId": case["caseId"], "reason": case["status"]})
            continue
        for region in ("Head", "Body"):
            expected = case.get(f"expected{region}")
            if expected is None:
                continue
            row = predictions[(case["caseId"], region)]
            if (row["leftStart"], row["leftEnd"], row["leftTrackId"]) != (
                case["start"], case["end"], case["track"]
            ):
                raise ValueError(f"Prediction belongs to a different interval: {case['caseId']}")
            actual = row["leftGroup"]
            comparisons.append({
                "caseId": case["caseId"], "region": region,
                "expected": expected, "actual": actual,
                "matchesProvisionalLabel": actual == expected,
                "observedSamples": row["leftSamples"],
            })
    return {
        "scope": "movement classification only; no identity accuracy conclusion",
        "annotationLimit": review["annotationLimit"],
        "professionalAccuracyEstablished": False,
        "comparisons": comparisons,
        "excluded": excluded,
    }


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("review", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    if args.output.resolve() == args.review.resolve():
        parser.error("Output must not overwrite manual labels")
    args.output.write_text(json.dumps(evaluate(args.review), ensure_ascii=False, indent=2), encoding="utf-8")
