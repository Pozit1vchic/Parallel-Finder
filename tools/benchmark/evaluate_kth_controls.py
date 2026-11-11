"""Replay real KTH observations through production grouping and motion search.

Known subjects restrict comparisons; appearance/face admission is deliberately
not evaluated. KTH action classes are a retrieval proxy, not editing-pair truth.
No missing observation or unknown prediction is excluded from the denominator.
"""
import argparse
import collections
import hashlib
import json
import math
from pathlib import Path
import re
import subprocess

from evaluate_quality import evaluate


CONFIGURATION = {
    "requireAppearance": False, "allowStaticFrames": False,
    "individualPairs": False, "maxResultsPerShot": 0, "maxUniqueResults": 10000,
    "maxComparisonThreads": 4,
}
GAIT = {"walking", "jogging", "running"}


def read(path):
    return json.loads(path.read_text(encoding="utf-8-sig"))


def sha256(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def cases(manifest_path, observation_root):
    dataset = read(manifest_path)
    annotations = (manifest_path.parent / "00sequences.txt").read_bytes()
    if hashlib.sha256(annotations).hexdigest() != dataset["annotationsSha256"]:
        raise ValueError("Temporal annotation fingerprint changed")
    intervals = {}
    for line in annotations.decode("ascii").splitlines():
        match = re.fullmatch(r"(person\d{2}_[a-z]+_d[1-4])\s+frames\s+(.+)", line.strip())
        if match:
            ranges = [tuple(map(int, pair)) for pair in re.findall(r"(\d+)-(\d+)", match[2])]
            if not ranges:
                raise ValueError("Missing published temporal ranges")
            intervals[match[1]] = ranges
    grouped = collections.defaultdict(list)
    coverage = collections.Counter()
    contracts = set()
    for video in dataset["videos"]:
        report = read(observation_root / (video["file"] + ".poses.json"))
        contract = report["contract"]
        if not report.get("complete") or contract["sourceSha256"] != video["sha256"]:
            raise ValueError("Observation/source provenance mismatch")
        contracts.add(json.dumps({k: v for k, v in contract.items() if k != "sourceSha256"}, sort_keys=True))
        fps = report["sourceFps"]
        if not math.isfinite(fps) or fps <= 0:
            raise ValueError("Invalid source cadence")
        frames = report["frames"]
        for frame in frames:
            points = frame["points"]
            if points and len(points) != 17:
                raise ValueError("Expected the complete 17-joint detector output or a missing detection")
            for point in points:
                if len(point) != 3 or any(not isinstance(v, (int, float)) or not math.isfinite(v) for v in point) or not 0 <= point[2] <= 1:
                    raise ValueError("Invalid observed joint values")
        times = [frame["time"] for frame in frames]
        if any(not math.isfinite(time) for time in times) or any(a >= b for a, b in zip(times, times[1:])):
            raise ValueError("Observation times must be finite and strictly ordered")
        key = video["file"].removesuffix("_uncomp.avi")
        if key not in intervals:
            raise ValueError("Source has no published temporal annotations")
        for index, (first, last) in enumerate(intervals[key]):
            # Published frame numbers begin at 1 and end inclusively.
            start, end = (first - 1) / fps, last / fps
            if first < 1 or last < first or end > report["durationSeconds"] + 1 / fps:
                raise ValueError("Published interval is outside the decoded source")
            observed = [frame for frame in frames if start - 1e-8 <= frame["time"] < end - 1e-8]
            if len(observed) < 2:
                raise ValueError("Sampling does not cover a labelled interval")
            coverage["frames"] += len(observed)
            coverage["detected"] += sum(bool(frame["points"]) for frame in observed)
            coverage["multiple"] += sum(frame["detections"] > 1 for frame in observed)
            grouped[video["subject"]].append({
                "name": f"{key}:{first}-{last}", "source": video["file"],
                "scene": index, "track": video["subject"], "sceneStart": start, "sceneEnd": end,
                "evaluationStart": start, "evaluationEnd": end,
                "action": video["action"], "scenario": video["scenario"], "expected": video["action"],
                "frames": [{"time": frame["time"], "points": frame["points"]} for frame in observed],
            })
    if len(contracts) != 1:
        raise ValueError("Mixed model/preprocessing/sampling contracts are not comparable")
    return dataset, grouped, coverage, json.loads(next(iter(contracts)))


def run(probe, arguments, path):
    result = subprocess.run([str(probe.resolve()), *map(str, arguments)], capture_output=True, timeout=600, check=True)
    parsed = json.loads(result.stdout)
    path.write_bytes(result.stdout)
    return parsed


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dataset-manifest", type=Path, required=True)
    parser.add_argument("--observations", type=Path, required=True)
    parser.add_argument("--probe", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    dataset, groups, coverage, contract = cases(args.dataset_manifest, args.observations)
    args.output.mkdir(parents=True, exist_ok=True)
    config_path = args.output / "matcher-configuration.json"
    config_path.write_text(json.dumps(CONFIGURATION, indent=2) + "\n", encoding="utf-8")
    counts = collections.Counter()
    supported_counts = collections.Counter()
    raw_labels = collections.defaultdict(collections.Counter)
    queries = []
    matcher_times = []
    for subject, windows in sorted(groups.items()):
        fixture = args.output / f"person{subject:02d}-windows.json"
        fixture.write_text(json.dumps(windows, separators=(",", ":")) + "\n", encoding="utf-8")
        actions = run(args.probe, [fixture, "--labelled-actions"], args.output / f"person{subject:02d}-actions.json")
        if len(actions["results"]) != len(windows):
            raise ValueError("Classifier omitted labelled intervals")
        for window, result in zip(windows, actions["results"]):
            if result["caseId"] != window["name"]:
                raise ValueError("Classifier result order changed")
            if abs(result["start"] - window["evaluationStart"]) > 1e-8 or abs(result["end"] - window["evaluationEnd"]) > 1e-8:
                raise ValueError("Classifier changed the published half-open interval")
            raw_labels[window["action"]][result["gesture"]] += 1
            target = window["action"] in GAIT
            predicted = "step" in result["actions"] or result["gesture"] == "step"
            counts["tp" if target and predicted else "fn" if target else "fp" if predicted else "tn"] += 1
            counts["unknown"] += result["gesture"] == "unknown"
        supported = run(args.probe, [fixture, "--supported-actions"], args.output / f"person{subject:02d}-supported-actions.json")
        if len(supported["results"]) != len(windows):
            raise ValueError("Supported-run classification omitted labelled intervals")
        for window, result in zip(windows, supported["results"]):
            if result["caseId"] != window["name"]:
                raise ValueError("Supported-run result order changed")
            target = window["action"] in GAIT
            predicted = "step" in result["actions"] or result["gesture"] == "step"
            supported_counts["tp" if target and predicted else "fn" if target else "fp" if predicted else "tn"] += 1
            supported_counts["unknown"] += result["gesture"] == "unknown"
        search = run(args.probe, [fixture, "--all", config_path], args.output / f"person{subject:02d}-retrieval.json")
        if search["windowCount"] != len(windows):
            raise ValueError("Matcher input count changed")
        matcher_times.append(search["elapsedMs"])
        rankings = [dict() for _ in windows]
        for pair in search["results"]:
            left, right = pair["leftIndex"], pair["rightIndex"]
            if left == right or not 0 <= left < len(windows) or not 0 <= right < len(windows):
                raise ValueError("Matcher returned an invalid candidate")
            if windows[left]["scenario"] == windows[right]["scenario"]:
                continue
            score = pair["similarity"]
            if not math.isfinite(score):
                raise ValueError("Nonfinite match score")
            for a, b in [(left, right), (right, left)]:
                rankings[a][b] = max(score, rankings[a].get(b, -math.inf))
        for index, window in enumerate(windows):
            candidates = [i for i, item in enumerate(windows) if item["scenario"] != window["scenario"]]
            positives = [windows[i]["name"] for i in candidates if windows[i]["action"] == window["action"]]
            negatives = [windows[i]["name"] for i in candidates if windows[i]["action"] != window["action"]]
            ranked = [windows[i]["name"] for i in sorted(rankings[index], key=lambda i: (-rankings[index][i], windows[i]["name"]))]
            queries.append({"id": window["name"], "positiveIds": positives, "negativeIds": negatives,
                            "rankedIds": ranked, "tags": [window["action"], f"scenario-{window['scenario']}"]})
    tp, fp, fn, tn = (counts[key] for key in ("tp", "fp", "fn", "tn"))
    total = tp + fp + fn + tn
    summary = {
        "schemaVersion": 1, "split": dataset["split"], "datasetManifestSha256": sha256(args.dataset_manifest),
        "planSha256": dataset["planSha256"], "probeSha256": sha256(args.probe), "contract": contract,
        "scope": "Known-subject cross-scenario action proxy; production pose/grouping/motion code; no identity admission or TV-editing truth",
        "detectionCoverage": {"labelledSamples": coverage["frames"], "samplesWithDetection": coverage["detected"],
                              "multipleDetectionSamples": coverage["multiple"], "rate": coverage["detected"] / coverage["frames"]},
        "gaitIdentification": {"tp": tp, "fp": fp, "fn": fn, "tn": tn,
                               "precision": tp / (tp + fp) if tp + fp else 0.,
                               "recall": tp / (tp + fn) if tp + fn else 0.,
                               "f1": 2 * tp / (2 * tp + fp + fn) if 2 * tp + fp + fn else 0.,
                               "unknownRate": counts["unknown"] / total},
        "rawLabelsByDatasetAction": {key: dict(value) for key, value in raw_labels.items()},
        "retrieval": evaluate({"scope": "Strict six-action retrieval, within known subject, across scenarios", "queries": queries}),
        "matcherMillisecondsBySubject": matcher_times,
    }
    stp, sfp, sfn, stn = (supported_counts[key] for key in ("tp", "fp", "fn", "tn"))
    summary["supportedRunGaitIdentification"] = {
        "scope": "Any step facet on production observedPoseRuns; all original intervals remain in denominator",
        "tp": stp, "fp": sfp, "fn": sfn, "tn": stn,
        "precision": stp / (stp + sfp) if stp + sfp else 0.,
        "recall": stp / (stp + sfn) if stp + sfn else 0.,
        "f1": 2 * stp / (2 * stp + sfp + sfn) if 2 * stp + sfp + sfn else 0.,
        "unknownRate": supported_counts["unknown"] / total,
    }
    (args.output / "queries.json").write_text(json.dumps({"queries": queries}, indent=2) + "\n", encoding="utf-8")
    (args.output / "summary.json").write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"summary": str((args.output / 'summary.json').resolve()),
                      "detectionCoverage": summary["detectionCoverage"], "gaitIdentification": summary["gaitIdentification"],
                      "supportedRunGaitIdentification": summary["supportedRunGaitIdentification"],
                      "retrieval": summary["retrieval"]["metrics"]}, indent=2))


if __name__ == "__main__":
    main()
