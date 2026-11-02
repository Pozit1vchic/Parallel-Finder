"""Find obvious simultaneous, spatially separate detections in one identity.

This is a triage tool, not an identity accuracy metric: overlapping boxes,
missed detections, different shots and unresolved identities need other checks.
Review frames before treating any reported detector pair as ground truth.
"""
import argparse
import json
from collections import defaultdict
from pathlib import Path


def audit(assignments):
    rows = sorted((row for row in assignments if row.get("identity")), key=lambda row: row["time"])
    collisions = defaultdict(list)
    for i, left in enumerate(rows):
        j = i + 1
        while j < len(rows) and rows[j]["time"] - left["time"] <= 1e-6:
            right = rows[j]
            j += 1
            if left["identity"] != right["identity"] or left["track"] == right["track"]:
                continue
            a, b = left["box"], right["box"]
            area_a = max(0, a[2] - a[0]) * max(0, a[3] - a[1])
            area_b = max(0, b[2] - b[0]) * max(0, b[3] - b[1])
            smaller = min(area_a, area_b)
            intersection = max(0, min(a[2], b[2]) - max(a[0], b[0])) * max(0, min(a[3], b[3]) - max(a[1], b[1]))
            if smaller and intersection / smaller < 0.1:
                tracks = tuple(sorted((str(left["track"]), str(right["track"]))))
                collisions[(str(left["identity"]), *tracks)].append(left["time"])
    return [dict(identity=identity, tracks=[a, b], observations=len(times),
                 firstTime=min(times), lastTime=max(times))
            for (identity, a, b), times in sorted(collisions.items())]


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("replay", type=Path, help="single-video pf_character_probe replay")
    args = parser.parse_args()
    replay = json.loads(args.replay.read_text(encoding="utf-8-sig"))
    findings = audit(replay["assignments"])
    print(json.dumps(dict(scope="Obvious same-frame, disjoint-box conflicts only; review detections manually",
                          pairs=findings), indent=2))
