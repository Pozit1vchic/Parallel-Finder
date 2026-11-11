"""Check structural invariants of real native result JSON, not visual quality."""
import argparse
import collections
import hashlib
import json
import math
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--report", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if args.output.exists():
        raise ValueError("Use a new output path")
    raw = args.report.read_bytes()
    report = json.loads(raw.decode("utf-8-sig"))
    protocol = json.loads(Path(__file__).with_name("chernobyl-validation-plan.json").read_text(encoding="utf-8-sig"))
    durations = {e["fileName"].casefold(): e["durationSeconds"] for e in protocol["episodes"]}
    errors, ids, pair_keys = [], set(), set()
    classes = collections.Counter()
    missing_pose_similarity = 0
    for position, result in enumerate(report["results"]):
        def fail(message):
            errors.append({"position": position, "resultId": result.get("id"), "issue": message})
        if result.get("id") in ids:
            fail("Duplicate result ID")
        ids.add(result.get("id"))
        classes[result.get("classification", "missing")] += 1
        sides = []
        for side in ("left", "right"):
            source = str(result.get(side + "Source", "")).replace("\\", "/")
            file_name = source.rsplit("/", 1)[-1].casefold()
            duration = durations.get(file_name)
            if duration is None:
                fail(side + " source absent from frozen episode inventory")
            for start_key, end_key in ((side + "Start", side + "End"),
                                       (side + "ClipStart", side + "ClipEnd"),
                                       (side + "SceneStart", side + "SceneEnd")):
                start, end = result.get(start_key), result.get(end_key)
                valid = (isinstance(start, (int, float)) and isinstance(end, (int, float))
                         and math.isfinite(start) and math.isfinite(end) and 0 <= start < end)
                if not valid:
                    fail(f"Invalid finite positive range {start_key}/{end_key}")
                elif duration is not None and end > duration + 0.04:
                    fail(f"Range {end_key} exceeds source duration plus one frame")
            sides.append((source.casefold(), str(result.get(side + "TrackId")),
                          result.get(side + "Start"), result.get(side + "End")))
        for key in ("similarity", "rankScore", "bestMatchScore", "evidenceQuality"):
            value = result.get(key)
            if not isinstance(value, (int, float)) or not math.isfinite(value) or not 0 <= value <= 1:
                fail("Invalid unit score: " + key)
        # The native organization API explicitly uses -1 for unavailable
        # comparison-pose evidence. Missing evidence is not a measured zero.
        value = result.get("poseSimilarity")
        if value == -1:
            missing_pose_similarity += 1
        elif not isinstance(value, (int, float)) or not math.isfinite(value) or not 0 <= value <= 1:
            fail("Invalid pose similarity or missing-evidence sentinel")
        pair_key = tuple(sorted(sides))
        if pair_key in pair_keys:
            fail("Exact same pair of source/track/matched intervals appears again, possibly reversed")
        pair_keys.add(pair_key)
    output = {"schemaVersion": 1, "reportSha256": hashlib.sha256(raw).hexdigest(),
              "scope": "Finite ranges/scores, inventory bounds and exact duplicate pair invariants only",
              "visualQualityStatus": "Not established by structural checks",
              "results": len(report["results"]), "classificationCounts": dict(classes),
              "unavailablePoseSimilarity": missing_pose_similarity,
              "errorCount": len(errors), "errors": errors}
    args.output.write_text(json.dumps(output, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"{len(report['results'])} results; {len(errors)} structural issues")
    return bool(errors)


if __name__ == "__main__":
    raise SystemExit(main())
