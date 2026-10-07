"""Controlled five-source matcher A/B; timings exclude decode/inference.

Run with two pf_matcher_probe executables built from the baseline and candidate.
Uses recorded supported pose geometry with synthetic independent pixel layouts;
this measures search scaling and result preservation, not semantic precision.
"""
import argparse
import json
import os
from pathlib import Path
import random
import subprocess
import uuid


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline", type=Path, required=True)
    parser.add_argument("--candidate", type=Path, required=True)
    parser.add_argument("--count", type=int, default=300)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    if not 10 <= args.count <= 10000:
        parser.error("count must be between 10 and 10000")
    repo = Path(__file__).resolve().parents[2]
    output = args.output or repo / "build" / "matcher-scaling" / uuid.uuid4().hex[:8]
    output.mkdir(parents=True, exist_ok=False)
    frames = json.loads((repo / "tests/fixtures/dean-partial-aiming-pose.json").read_text())[0]["frames"]
    windows = []
    for i in range(args.count):
        rng = random.Random(i)
        pixels = [rng.uniform(.15, .8) for _ in range(432)]
        sequence = [min(.95, max(.05, p + .005 * ((f % 5) - 2))) for f in range(24) for p in pixels]
        offset = i * 5
        poses = [{"time": offset + f["time"] - frames[0]["time"], "points": f["points"]} for f in frames]
        windows.append({"source": f"source-{i % 5}", "scene": i, "track": 1, "static": True,
                        "sceneStart": offset, "sceneEnd": offset + 5, "face": [1, 0], "body": [1, 0],
                        "faceConfidence": 1, "bodyConfidence": 1, "context": [1, 0], "frames": poses,
                        "sequence": sequence, "sequencePts": [offset + f * .25 for f in range(24)]})
    inputs = output / "windows.json"
    inputs.write_text(json.dumps(windows, separators=(",", ":")))
    params = output / "params.json"
    params.write_text(json.dumps({"individualPairs": True, "recoverUnusedShots": False,
                                 "maxUniqueResults": 500, "maxComparisonThreads": 1}))
    reports = {}
    for label, exe in [("baseline", args.baseline), ("candidate", args.candidate)]:
        result = subprocess.run([str(exe.resolve()), str(inputs.resolve()), "--all", str(params.resolve())],
                                env={**os.environ, "PF_DEBUG_ANALYSIS": "1"}, capture_output=True,
                                check=True, timeout=900)
        (output / f"{label}.json").write_bytes(result.stdout)
        (output / f"{label}.log").write_bytes(result.stderr)
        reports[label] = json.loads(result.stdout)
    identical = reports["baseline"]["results"] == reports["candidate"]["results"]
    summary = {"sources": 5, "windows": args.count, "matcherOnly": True, "syntheticPixelLayouts": True,
               "baselineMs": reports["baseline"]["elapsedMs"], "candidateMs": reports["candidate"]["elapsedMs"],
               "exactResultEquality": identical, "pairs": len(reports["candidate"]["results"])}
    (output / "summary.json").write_text(json.dumps(summary, indent=2))
    print(json.dumps(summary, indent=2))
    if not identical:
        raise SystemExit("Result regression: inspect both reports")


if __name__ == "__main__":
    main()
