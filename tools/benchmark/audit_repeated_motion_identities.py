"""Triage split identities using existing scene sequences and pose windows.

Run pf_matcher_probe WINDOWS --scene-neighbours first. This is a research
diagnostic, not a rule for merging people: similar shots, clothing and pose
may still show different people, and selected hits require visual review.
"""
import argparse
from collections import defaultdict
import json
from pathlib import Path

import numpy as np


def cosine(left, right):
    a, b = np.asarray(left, dtype=np.float64), np.asarray(right, dtype=np.float64)
    if a.shape != b.shape or not a.size:
        return -1
    denominator = np.linalg.norm(a) * np.linalg.norm(b)
    return float(a @ b / denominator) if denominator > 1e-9 else -1


def pose_distance(left, right):
    points = [(x, y) for x, y in zip(left["points"], right["points"])
              if x[2] >= .5 and y[2] >= .5]
    if len(points) < 3:
        return None
    a = np.asarray([[x[0], x[1]] for x, _ in points])
    b = np.asarray([[y[0], y[1]] for _, y in points])
    spread = min(np.linalg.norm(np.ptp(a, axis=0)), np.linalg.norm(np.ptp(b, axis=0)))
    if spread < 20:
        return None
    return float(np.median(np.linalg.norm(a - b, axis=1)) / spread)


def audit(windows, assignments, neighbours):
    identities = {int(row["track"]): int(row["identity"]) for row in assignments}
    scenes, track_windows = {}, {}
    for window in windows:
        scene, track = window["scene"], int(window["track"])
        if window["sequencePts"]:
            scenes.setdefault(scene, window)
        if track not in identities:
            continue
        entry = track_windows.setdefault((scene, track), {"body": window["body"], "frames": {}})
        entry["frames"].update({round(frame["time"], 6): frame for frame in window["frames"]})
    by_scene = defaultdict(list)
    for (scene, track), entry in track_windows.items():
        if entry["body"] and len(entry["frames"]) >= 3:
            by_scene[scene].append((track, entry))

    findings = defaultdict(list)
    for pair in neighbours:
        if pair["sampleDifference"] >= .025:
            continue
        left, right = pair["leftScene"], pair["rightScene"]
        if left not in scenes or right not in scenes:
            continue
        a, b = scenes[left], scenes[right]
        sa, sb = np.asarray(a["sequence"]).reshape(-1, 432), np.asarray(b["sequence"]).reshape(-1, 432)
        for i in range(len(sa) - 2):
            for j in range(len(sb) - 2):
                error = float(np.abs(sa[i:i+3] - sb[j:j+3]).mean())
                if error >= .025:
                    continue
                for track_a, entry_a in by_scene[left]:
                    for track_b, entry_b in by_scene[right]:
                        if identities[track_a] == identities[track_b]:
                            continue
                        body = cosine(entry_a["body"], entry_b["body"])
                        if body < .85:
                            continue
                        matches = []
                        for offset in range(3):
                            ta, tb = a["sequencePts"][i+offset], b["sequencePts"][j+offset]
                            fa = min(entry_a["frames"].values(), key=lambda f: abs(f["time"] - ta))
                            fb = min(entry_b["frames"].values(), key=lambda f: abs(f["time"] - tb))
                            if abs(fa["time"] - ta) > .1 or abs(fb["time"] - tb) > .1:
                                continue
                            distance = pose_distance(fa, fb)
                            if distance is not None and distance < .25:
                                matches.append((fa["time"], fb["time"], distance))
                        if len({v[0] for v in matches}) >= 2 and len({v[1] for v in matches}) >= 2:
                            findings[(identities[track_a], identities[track_b])].append({
                                "leftTrack": track_a, "rightTrack": track_b, "sceneError": error,
                                "bodySimilarity": body, "poseMatches": matches,
                            })
    return [{"leftCandidate": left, "rightCandidate": right,
             "evidence": sorted(rows, key=lambda row: row["sceneError"])}
            for (left, right), rows in sorted(findings.items())]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("windows", "replay", "neighbours", "output"):
        parser.add_argument(f"--{name}", type=Path, required=True)
    args = parser.parse_args()
    def load(path):
        return json.loads(path.read_text(encoding="utf-8-sig"))
    findings = audit(load(args.windows), load(args.replay)["assignments"], load(args.neighbours)["pairs"])
    args.output.write_text(json.dumps({"pairs": findings}, indent=2), encoding="utf-8")
    print(f"{len(findings)} pairs to review")


if __name__ == "__main__":
    main()
