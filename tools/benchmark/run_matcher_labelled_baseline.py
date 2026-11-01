"""Replay a tiny reviewed matcher fixture; no unreviewed pairs become negatives."""
import argparse
import hashlib
import json
import subprocess
from pathlib import Path

from evaluate_quality import evaluate


ROOT = Path(__file__).resolve().parents[2]


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def make_queries(reference, report):
    candidates = reference["candidates"]
    ids = [entry["id"] for entry in candidates]
    indices = [entry["index"] for entry in candidates]
    if len(ids) != len(set(ids)) or sorted(indices) != list(range(len(indices))):
        raise ValueError("Candidate IDs and indices must uniquely cover the fixture")
    by_index = {entry["index"]: entry["id"] for entry in candidates}
    if report["windowCount"] != len(candidates):
        raise ValueError("Probe input count does not match reviewed candidate list")
    rankings = {entry["id"]: [] for entry in candidates}
    for pair in report["results"]:
        left, right = pair["leftIndex"], pair["rightIndex"]
        if left not in by_index or right not in by_index or left == right:
            raise ValueError("Probe returned an invalid pair")
        rankings[by_index[left]].append(by_index[right])
        rankings[by_index[right]].append(by_index[left])
    return [{"id": entry["id"], "positiveIds": entry["positiveIds"],
             "negativeIds": entry["negativeIds"], "rankedIds": rankings[entry["id"]]}
            for entry in candidates]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--probe", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    reference_path = Path(__file__).with_name("matcher-retrieval-baseline-v1.json")
    reference = json.loads(reference_path.read_text(encoding="utf-8"))
    fixture = ROOT / reference["fixture"]
    if digest(fixture) != reference["fixtureSha256"]:
        raise ValueError("Fixture changed: re-review judgments before running this baseline")
    # No config argument: use the probe's documented fast-search defaults.
    result = subprocess.run([str(args.probe.resolve()), str(fixture), "--all"],
                            capture_output=True, check=True, timeout=120)
    report = json.loads(result.stdout)
    retrieval = {"scope": reference["scope"], "queries": make_queries(reference, report)}
    # Only one candidate per query exists; P@5/P@10 would be padding, not a
    # measurement of ranked discrimination. Retain the complete rankings.
    metrics = evaluate(retrieval, ks=(1,))
    output = {"schemaVersion": 1, "referenceSha256": digest(reference_path),
              "fixtureSha256": digest(fixture), "probeSha256": digest(args.probe),
              "configuration": "pf_matcher_probe --all defaults; no result organization",
              "sourceVideosAvailable": all(Path(path).is_file() for path in report["inputVideos"]),
              "reviewEvidence": reference["reviewEvidence"],
              "limitations": reference["reviewLimitations"],
              "retrieval": retrieval, "evaluation": metrics}
    args.output.write_text(json.dumps(output, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(metrics, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
