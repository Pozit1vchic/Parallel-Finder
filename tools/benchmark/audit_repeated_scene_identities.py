"""Find possible split identities in visually repeated scenes, for human review.

The input cache and captures are local artifacts. This is not an identity
classifier: a person can move, leave the frame, or be replaced between two
similar-looking shots. In particular, a matching background alone proves
nothing about the identity inside a detection box.
"""
import argparse
from collections import defaultdict
import io
import json
import math
from pathlib import Path
import re
import struct

import numpy as np
from PIL import Image


def cache_frames(cache, candidates, assume_legacy_unmerged=False):
    keys = {}
    timed = {}
    for path in cache.glob("*.pfc"):
        with path.open("rb") as stream:
            header = stream.read(24)
            if len(header) != 24:
                continue
            magic, version, key_size, payload_size = struct.unpack("<8sIIQ", header)
            if magic != b"PFCACHE1" or version != 1 or key_size > 16384:
                continue
            key = stream.read(key_size).decode("utf-8", errors="replace")
            timed_match = re.search(r"character-preview-v3-source-time\|(\d+)$", key)
            if timed_match:
                bits = int(timed_match[1])
                if bits > (1 << 64) - 1:
                    continue
                time = struct.unpack("<d", struct.pack("<Q", bits))[0]
                if not math.isfinite(time) or time < 0:
                    continue
                if time in timed:
                    raise ValueError("Cache contains conflicting preview namespaces; use an isolated source cache")
                timed[time] = (path, payload_size)
                continue
            match = re.search(r"character-preview-v2-clean-frame\|(\d+)\|(\d+)$", key)
            if match:
                identity = (int(match[1]), int(match[2]))
                if identity in keys:
                    raise ValueError("Cache contains conflicting preview namespaces; use an isolated source cache")
                keys[identity] = (path, payload_size)
    frames = {}
    for candidate in candidates:
        for index, preview in enumerate(candidate["previews"]):
            key = (int(candidate["id"]), index)
            if preview["time"] in timed:
                frames[preview["time"]] = timed[preview["time"]]
            elif assume_legacy_unmerged and key in keys:
                frames[preview["time"]] = keys[key]
    return frames


def frame_vector(entry):
    path, expected_size = entry
    with path.open("rb") as stream:
        header = stream.read(24)
        _, _, key_size, _ = struct.unpack("<8sIIQ", header)
        stream.seek(key_size, 1)
        payload = stream.read()
    if len(payload) != expected_size:
        raise ValueError(f"Truncated cache entry: {path}")
    with Image.open(io.BytesIO(payload)) as image:
        pixels = np.asarray(image.convert("L").resize((32, 18)), dtype=np.float32).reshape(-1)
    pixels -= pixels.mean()
    length = np.linalg.norm(pixels)
    return pixels / length if length > 1e-9 else np.zeros_like(pixels)


def box_iou(a, b):
    intersection = max(0, min(a[2], b[2]) - max(a[0], b[0])) * max(0, min(a[3], b[3]) - max(a[1], b[1]))
    def area(box):
        return max(0, box[2] - box[0]) * max(0, box[3] - box[1])
    union = area(a) + area(b) - intersection
    return intersection / union if union > 0 else 0


def audit(candidates, tracks, assignments, cache, assume_legacy_unmerged=False):
    frames = cache_frames(cache, candidates, assume_legacy_unmerged)
    identities = {int(row["track"]): int(row["identity"]) for row in assignments}
    by_time = defaultdict(list)
    for track in tracks:
        track_id = int(track["id"])
        if track_id not in identities:
            continue
        for observation in track["observations"]:
            if observation["time"] not in frames or not observation["body"]:
                continue
            vector = np.asarray(observation["body"], dtype=np.float32)
            norm = np.linalg.norm(vector)
            if not np.isfinite(norm) or norm <= 1e-9:
                continue
            by_time[observation["time"]].append((track_id, identities[track_id], observation["box"], vector / norm))
    times = sorted(by_time)
    if not times:
        return {"uniqueFrames": 0, "candidatePairs": []}
    vectors = np.stack([frame_vector(frames[time]) for time in times])
    similarities = vectors @ vectors.T
    matches = defaultdict(list)
    for i, time in enumerate(times):
        for j in np.flatnonzero(similarities[i] > .88):
            if j <= i or times[j] - time < 4:
                continue
            for track_a, group_a, box_a, body_a in by_time[time]:
                for track_b, group_b, box_b, body_b in by_time[times[j]]:
                    if group_a == group_b or body_a.shape != body_b.shape:
                        continue
                    overlap, body = box_iou(box_a, box_b), float(body_a @ body_b)
                    if overlap >= .7 and body >= .85:
                        matches[(group_a, group_b)].append({
                            "leftTime": time, "rightTime": times[j], "leftTrack": track_a,
                            "rightTrack": track_b, "scene": float(similarities[i, j]),
                            "boxIou": overlap, "body": body,
                        })
    result = []
    for (left, right), observations in sorted(matches.items()):
        # Distinct times on both sides and consistent offset avoid counting one
        # repeated preview as many independent confirmations.
        accepted = []
        for match in sorted(observations, key=lambda row: -row["scene"]):
            if any(abs(match["leftTime"] - previous["leftTime"]) < .1
                   or abs(match["rightTime"] - previous["rightTime"]) < .1 for previous in accepted):
                continue
            offset = match["rightTime"] - match["leftTime"]
            if accepted and abs(offset - (accepted[0]["rightTime"] - accepted[0]["leftTime"])) > .5:
                continue
            accepted.append(match)
        if len(accepted) >= 2:
            result.append({"leftCandidate": left, "rightCandidate": right, "matches": accepted})
    return {"uniqueFrames": len(times), "candidatePairs": result}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--candidates", type=Path, required=True)
    parser.add_argument("--tracks", type=Path, required=True)
    parser.add_argument("--replay", type=Path, required=True)
    parser.add_argument("--cache", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--assume-legacy-unmerged", action="store_true",
                        help="Interpret v2 candidate/index keys only for a capture known to predate post-processing merges")
    args = parser.parse_args()
    def load(path):
        return json.loads(path.read_text(encoding="utf-8-sig"))
    result = audit(load(args.candidates), load(args.tracks)["tracks"],
                   load(args.replay)["assignments"], args.cache, args.assume_legacy_unmerged)
    args.output.write_text(json.dumps(result, indent=2), encoding="utf-8")
    print(f"{result['uniqueFrames']} frames; {len(result['candidatePairs'])} pairs to review")


if __name__ == "__main__":
    main()
