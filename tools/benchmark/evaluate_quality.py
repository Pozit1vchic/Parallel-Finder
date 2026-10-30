"""Evaluate labelled classification/retrieval reports without inventing negatives.

Classification input is pf_matcher_probe --labelled-actions output. Retrieval
input is {queries:[{id, positiveIds, negativeIds, rankedIds, tags?}]}. Candidate
IDs absent from both judgement sets remain unjudged and invalidate precision,
not automatically become false positives. See quality-dataset.md.
"""
import argparse
import collections
import json
from pathlib import Path
import statistics


def evaluate(report, ks=(1, 5, 10)):
    result = {"scope": report.get("scope", "local labelled sample"), "metrics": {}}
    rows = report.get("results", []) if report.get("labelled") else []
    if rows:
        confusion = collections.defaultdict(collections.Counter)
        for row in rows:
            expected, predicted = row.get("expected"), row.get("gesture")
            if not isinstance(expected, str) or not expected or not isinstance(predicted, str):
                raise ValueError("Every classification case needs expected and gesture strings")
            confusion[expected][predicted] += 1
        correct = sum(row["expected"] == row["gesture"] for row in rows)
        result["metrics"].update(classificationCases=len(rows), classificationCorrect=correct,
                                 classificationAccuracy=correct / len(rows))
        result["confusion"] = {key: dict(value) for key, value in sorted(confusion.items())}
    queries = report.get("queries", [])
    if queries:
        identities = [row["id"] for row in queries]
        if len(set(identities)) != len(identities):
            raise ValueError("Query IDs must be unique")
        recalls = {k: [] for k in ks}
        precisions = {k: [] for k in ks}
        reciprocal, ranks = [], []
        unjudged = 0
        for query in queries:
            positives, negatives = set(query["positiveIds"]), set(query["negativeIds"])
            ranked = query["rankedIds"]
            if positives & negatives or len(set(ranked)) != len(ranked):
                raise ValueError("Conflicting judgements or duplicate candidate IDs")
            judged = positives | negatives
            unjudged += sum(candidate not in judged for candidate in ranked)
            rank = next((i + 1 for i, candidate in enumerate(ranked) if candidate in positives), None)
            if positives:
                reciprocal.append(1 / rank if rank else 0)
                if rank:
                    ranks.append(rank)
            for k in ks:
                top = ranked[:k]
                hits = len(set(top) & positives)
                if positives:
                    recalls[k].append(hits / len(positives))
                if all(candidate in judged for candidate in top):
                    # Standard Precision@K: unfilled slots are not successful retrievals.
                    precisions[k].append(hits / k)
        metrics = result["metrics"]
        metrics.update(queryCount=len(queries), unjudgedRetrieved=unjudged,
                       MRR=statistics.mean(reciprocal) if reciprocal else None,
                       knownPositiveMedianRank=statistics.median(ranks) if ranks else None)
        for k in ks:
            metrics[f"Recall@{k}"] = statistics.mean(recalls[k]) if recalls[k] else None
            metrics[f"Precision@{k}"] = statistics.mean(precisions[k]) if len(precisions[k]) == len(queries) else None
        result["tags"] = sorted({tag for query in queries for tag in query.get("tags", [])})
    if not result["metrics"]:
        raise ValueError("No labelled classification or retrieval cases")
    if "elapsedMs" in report:
        result["elapsedMs"] = report["elapsedMs"]
    return result


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("report", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    result = evaluate(json.loads(args.report.read_text(encoding="utf-8-sig")))
    rendered = json.dumps(result, ensure_ascii=False, indent=2) + "\n"
    if args.output:
        args.output.write_text(rendered, encoding="utf-8")
    else:
        print(rendered, end="")
