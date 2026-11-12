"""Audit frozen, cached head rotations; no fitting or production score changes."""
import argparse
import itertools
import json
import math
from pathlib import Path

from score_prediction_review import IDENTITY_FIELDS, digest, load
from evaluate_training_ranking import transfer_judgments


def valid_rotation(matrix):
    if len(matrix) != 9 or any(type(x) not in (int, float) or not math.isfinite(x) for x in matrix):
        return False
    # Existing core HeadOrientation.cpp admission criterion, not a fitted gate.
    for a in range(3):
        for b in range(3):
            dot = sum(matrix[a * 3 + k] * matrix[b * 3 + k] for k in range(3))
            if abs(dot - (1. if a == b else 0.)) > .002:
                return False
    r = matrix
    determinant = (r[0] * (r[4] * r[8] - r[5] * r[7])
                   - r[1] * (r[3] * r[8] - r[5] * r[6])
                   + r[2] * (r[3] * r[7] - r[4] * r[6]))
    return abs(determinant - 1.) <= .002


def rotation_distance(a, b):
    if not valid_rotation(a) or not valid_rotation(b):
        raise ValueError("SO(3) distance requires admitted rotations")
    return math.degrees(math.acos(max(-1., min(1., (sum(x * y for x, y in zip(a, b)) - 1.) / 2.))))


def summarize_side(side):
    start, end = side["start"], side["end"]
    if not all(type(t) in (int, float) and math.isfinite(t) for t in (start, end)) or start < 0 or end <= start:
        raise ValueError("Invalid clip range")
    samples = side["samples"]
    previous = -math.inf
    for sample in samples:
        time = sample["time"]
        if type(time) not in (int, float) or not math.isfinite(time) or time <= previous:
            raise ValueError("Requested times must be finite and strictly increasing")
        previous = time
    # Match the core classifier's existing one-microsecond boundary tolerance.
    eligible = [s for s in samples if start - 1e-6 <= s["time"] < end - 1e-6]
    valid = [s["cacheStatus"] == "hit" and s["observed"] is True and valid_rotation(s["rotation"])
             for s in eligible]
    runs = []
    current = []
    for sample, admitted in zip(eligible, valid):
        if admitted:
            current.append(sample)
        else:
            if current:
                runs.append(current)
                current = []
    if current:
        runs.append(current)
    excursions = [max(rotation_distance(a["rotation"], b["rotation"])
                      for a, b in itertools.combinations(run, 2)) for run in runs if len(run) >= 2]
    steps = [rotation_distance(a["rotation"], b["rotation"])
             for run in runs for a, b in zip(run, run[1:])]
    net = (rotation_distance(eligible[0]["rotation"], eligible[-1]["rotation"])
           if len(eligible) >= 2 and valid[0] and valid[-1] else None)
    times = [s["time"] for s, admitted in zip(eligible, valid) if admitted]
    return {
        "requestedSamples": len(samples), "eligibleSamples": len(eligible),
        "excludedBeforeStart": sum(s["time"] < start - 1e-6 for s in samples),
        "excludedAtOrAfterEnd": sum(s["time"] >= end - 1e-6 for s in samples),
        "validObservedSamples": sum(valid),
        "observedRequestedFraction": sum(valid) / len(valid) if valid else None,
        "missingCacheSamples": sum(s["cacheStatus"] != "hit" for s in eligible),
        "unobservedSamples": sum(s["cacheStatus"] == "hit" and s["observed"] is not True for s in eligible),
        "invalidObservedRotations": sum(s["cacheStatus"] == "hit" and s["observed"] is True
                                        and not valid_rotation(s["rotation"]) for s in eligible),
        "maxRequestedStepSeconds": max((b["time"] - a["time"] for a, b in zip(eligible, eligible[1:])), default=None),
        "validObservedSpanFraction": (times[-1] - times[0]) / (end - start) if len(times) >= 2 else None,
        "validRunLengths": [len(run) for run in runs],
        "netAngleDegrees": net,
        "netEndpointsAcrossExplicitGap": net is not None and not all(valid),
        "excursionDegrees": max(excursions) if excursions else None,
        "withinRunPathDegrees": sum(steps) if steps else None,
    }


def preference_counts(cases, field):
    preferences = [(a, b) for a, b in itertools.permutations(cases, 2)
                   if a["grade"] != "unknown" and b["grade"] != "unknown" and a["grade"] > b["grade"]]
    correct = tied = unavailable = 0
    for a, b in preferences:
        sa, sb = ((-r["currentPosition"]) if field == "currentReturnedOrder" else r[field] for r in (a, b))
        if any(s is None or not math.isfinite(s) for s in (sa, sb)):
            unavailable += 1
        elif sa > sb:
            correct += 1
        elif sa == sb:
            tied += 1
    comparable = len(preferences) - unavailable
    return {"totalPreferences": len(preferences), "comparable": comparable, "unavailable": unavailable,
            "strictlyCorrect": correct, "tied": tied,
            "strictPreferenceAccuracy": correct / comparable if comparable else None}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--replay", required=True)
    parser.add_argument("--requests", required=True)
    parser.add_argument("--plan", required=True)
    parser.add_argument("--labels", nargs="+", required=True)
    parser.add_argument("--reference-report", required=True)
    parser.add_argument("--current-report", required=True)
    parser.add_argument("--output", required=True)
    args = parser.parse_args()
    output = Path(args.output)
    if output.exists():
        raise ValueError("Use a new output artifact")
    plan, requests, replay = load(args.plan), load(args.requests), load(args.replay)
    heldout = "reports" in plan
    binding_valid = (requests.get("reviewPlanSha256") == digest(args.plan) if heldout
                     else plan["requestSha256"] == digest(args.requests))
    if replay["schemaVersion"] != 1 or replay["requestSha256"] != digest(args.requests) or not binding_valid:
        raise ValueError("Replay/request/frozen-plan mismatch")
    reference = load(args.reference_report)["results"]
    if requests["referenceReportSha256"] != digest(args.reference_report):
        raise ValueError("Reference report changed")
    native_reports = []
    for path, expected in requests["nativePoseReports"].items():
        if digest(path) != expected:
            raise ValueError("Native selected-track pose evidence changed")
        native_reports.append(load(path))
    indices = set(plan["reports"]["after"]["returnedArrayIndices"] if heldout else plan["returnedArrayIndices"])
    judgments = {}
    for path in args.labels:
        labels = load(path)
        expected_split = "held-out-predictions" if heldout else "tuning"
        if labels["split"] != expected_split or labels["reportSha256"] != digest(args.reference_report) or labels["planSha256"] != digest(labels["plan"]):
            raise ValueError("Training judgment provenance mismatch")
        for j in labels["judgments"]:
            index = j["returnedArrayIndex"]
            if index not in indices:
                continue
            if index in judgments or j["record"] != {k: reference[index][k] for k in IDENTITY_FIELDS}:
                raise ValueError("Ambiguous or changed selected comparison")
            if not reference[index]["headOnlyComparison"] or (j["grade"] != "unknown" and
                    (type(j["grade"]) is not int or j["grade"] not in (0, 1, 2) or j["trackAssociationVerified"] is not True)):
                raise ValueError("Require head judgments with verified tracks or abstentions")
            for artifact in j["visualArtifacts"]:
                if digest(artifact["path"]) != artifact["sha256"]:
                    raise ValueError("Reviewed visual evidence changed")
            judgments[index] = j
    if set(judgments) != indices:
        raise ValueError("Frozen head judgments incomplete")
    transferred = transfer_judgments(list(judgments.values()), load(args.current_report)["results"])
    positions = {old["returnedArrayIndex"]: new["returnedArrayIndex"]
                 for old, new in zip(judgments.values(), transferred)}
    requested = {p["returnedArrayIndex"]: p for p in requests["pairs"]}
    if len(requested) != len(requests["pairs"]) or set(requested) != indices:
        raise ValueError("Request selection changed")
    cases = []
    seen = set()
    for pair in replay["pairs"]:
        index = pair["returnedArrayIndex"]
        if index in seen or index not in indices:
            raise ValueError("Replay selection changed")
        seen.add(index)
        case = {"returnedArrayIndex": index, "currentPosition": positions[index], "grade": judgments[index]["grade"]}
        for side in ("left", "right"):
            source = pair[side]
            original = reference[index]
            if (source["source"], source["trackId"], source["start"], source["end"]) != tuple(
                    original[side + field] for field in ("Source", "TrackId", "Start", "End")):
                raise ValueError("Replay source/track/range changed")
            if [s["time"] for s in source["samples"]] != [f["time"] for f in requested[index][side]["frames"]]:
                raise ValueError("Replay sample times changed")
            native = [r for report in native_reports for r in report["results"]
                      if r["reviewOriginalReturnedIndex"] == index and r.get(side + "Frames")]
            if len(native) != 1 or native[0][side + "Frames"] != requested[index][side]["frames"]:
                raise ValueError("Requests must retain the complete native matched-track pose frames")
            case[side] = summarize_side(source)
        for name, measurement in (("minExcursionDegrees", "excursionDegrees"),
                                  ("minNetAngleDegrees", "netAngleDegrees"),
                                  ("minWithinRunPathDegrees", "withinRunPathDegrees")):
            values = [case[side][measurement] for side in ("left", "right")]
            case[name] = min(values) if all(v is not None for v in values) else None
        cases.append(case)
    if seen != indices:
        raise ValueError("Replay selection incomplete")
    result = {
        "scope": ("Frozen source-held-out head prediction-scope angular audit; no independent human confirmation or full-episode accuracy claim"
                  if heldout else "Frozen training-only angular diagnostics; not calibrated relevance, independent validation or a production ranking policy"),
        "planSha256": digest(args.plan), "replaySha256": digest(args.replay),
        "referenceReportSha256": digest(args.reference_report), "currentReportSha256": digest(args.current_report),
        "labelsSha256": {path: digest(path) for path in args.labels}, "cases": cases,
        "metrics": {field: preference_counts(cases, field) for field in
                    ("currentReturnedOrder", "minExcursionDegrees", "minNetAngleDegrees", "minWithinRunPathDegrees")},
        "limits": "Angles may include estimator noise, body/camera motion. Path sums exclude explicit gaps but cannot prove continuous observation between requested samples. Endpoint differences across gaps are marked. No threshold or weight was fitted.",
    }
    output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
