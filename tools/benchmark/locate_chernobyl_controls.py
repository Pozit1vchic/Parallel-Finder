"""Locate frozen control intervals in full native output for actor-aware review.

Temporal overlap is a candidate association, never proof that the matched track
belongs to the labeled person. This report deliberately does not invent quality
metrics before that review. Output rank means returned array position.
"""
import argparse
import hashlib
import json
from pathlib import Path


def load(path):
    return json.loads(path.read_text(encoding="utf-8-sig"))


def overlap(a, b):
    start, end = max(a[0], b[0]), min(a[1], b[1])
    shorter = min(a[1] - a[0], b[1] - b[0])
    return max(0.0, end - start) / shorter if shorter > 0 else 0.0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--report", type=Path, required=True)
    parser.add_argument("--labels", type=Path, default=Path(__file__).with_name("chernobyl-holdout-labels.json"))
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if args.output.exists():
        raise ValueError("Use a new output path to preserve review evidence")
    labels = load(args.labels)
    protocol = load(Path(__file__).with_name("chernobyl-validation-plan.json"))
    episodes = {e["id"]: e["fileName"].casefold() for e in protocol["episodes"]}
    clips = {c["id"]: c for c in labels["clips"]}
    results = load(args.report)["results"]

    def side_matches(result, side, clip):
        source = str(result[side + "Source"]).replace("\\", "/").rsplit("/", 1)[-1].casefold()
        return (source == episodes[clip["episode"]]
                and overlap((float(result[side + "Start"]), float(result[side + "End"])),
                            (clip["start"], clip["end"])) >= 0.5)

    controls = []
    for pair in labels["pairs"]:
        a, b = clips[pair["a"]], clips[pair["b"]]
        hits = []
        for rank, result in enumerate(results, 1):
            forward = side_matches(result, "left", a) and side_matches(result, "right", b)
            reverse = side_matches(result, "left", b) and side_matches(result, "right", a)
            if forward or reverse:
                hits.append({"returnedArrayRank": rank, "resultId": result.get("id"),
                             "orientation": "forward" if forward else "reverse",
                             "trackIds": [result.get("leftTrackId"), result.get("rightTrackId")],
                             "bounds": [result["leftStart"], result["leftEnd"], result["rightStart"], result["rightEnd"]],
                             "classification": result.get("classification"),
                             "similarity": result.get("similarity"),
                             "actorAssociation": "pending visual track review"})
        controls.append({**pair, "temporalCandidateCount": len(hits), "candidates": hits})
    output = {
        "schemaVersion": 1,
        "scope": "Frozen judged control interval localization; not actor-confirmed Precision/Recall",
        "reportSha256": hashlib.sha256(args.report.read_bytes()).hexdigest(),
        "labelsSha256": hashlib.sha256(args.labels.read_bytes()).hexdigest(),
        "returnedResults": len(results),
        "controls": controls,
        "unjudgedPolicy": "All other returned matches remain unjudged",
        "metricsStatus": "Pending actor-aware review; no full-episode recall claim",
    }
    args.output.write_text(json.dumps(output, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"Located temporal candidates for {sum(bool(c['candidates']) for c in controls)}/{len(controls)} controls")


if __name__ == "__main__":
    main()
