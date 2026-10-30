"""Evaluate reviewed identity membership; require every geometry anchor to resolve.

No image pixels or identity embeddings are needed in the judgement file.
This is a small association regression set, not a general recognition benchmark.
"""
import argparse
import json
from pathlib import Path


def evaluate(assignments, labels):
    def identity(anchor):
        def iou(a, b):
            intersection = max(0, min(a[2], b[2])-max(a[0], b[0])) * max(0, min(a[3], b[3])-max(a[1], b[1]))
            union = (a[2]-a[0])*(a[3]-a[1])+(b[2]-b[0])*(b[3]-b[1])-intersection
            return intersection/union if union > 0 else 0
        found = [row["identity"] for row in assignments if abs(row["time"]-anchor["time"]) < .002
                 and iou(row["box"], anchor["box"]) > .99]
        if len(found) != 1 or not found[0]:
            raise ValueError("Every reviewed anchor must resolve to exactly one observed identity")
        return found[0]
    counts = dict(truePositive=0, falsePositive=0, trueNegative=0, falseNegative=0)
    cases = []
    for row in labels["controls"]:
        if not isinstance(row["sameIdentity"], bool):
            raise ValueError("Identity labels must be explicit booleans")
        predicted = identity(row["left"]) == identity(row["right"])
        expected = row["sameIdentity"]
        counts[("true" if predicted == expected else "false") + ("Positive" if predicted else "Negative")] += 1
        cases.append(dict(id=row["id"], expected=expected, predicted=predicted))
    tp, fp, tn, fn = (counts[k] for k in ("truePositive", "falsePositive", "trueNegative", "falseNegative"))
    return dict(scope=labels["scope"], cases=cases, counts=counts,
                accuracy=(tp+tn)/len(cases) if cases else None,
                precision=tp/(tp+fp) if tp+fp else None,
                recall=tp/(tp+fn) if tp+fn else None)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("replay", type=Path)
    parser.add_argument("labels", type=Path)
    args = parser.parse_args()
    replay = json.loads(args.replay.read_text(encoding="utf-8-sig"))
    labels = json.loads(args.labels.read_text(encoding="utf-8-sig"))
    print(json.dumps(evaluate(replay["assignments"], labels), indent=2))
