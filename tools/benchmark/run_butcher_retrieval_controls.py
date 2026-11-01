"""Evaluate manually reviewed Butcher scenes against production windows/results.

Never labels unreviewed output as negative. A tiny replay of three chosen
windows is reported separately from the complete application output.
"""
import argparse
import hashlib
import json
import subprocess
import tempfile
from pathlib import Path

from evaluate_quality import evaluate


ROOT = Path(__file__).resolve().parents[2]
REFERENCE = ROOT / "tests/fixtures/butcher-retrieval-controls.json"


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def selected_windows(reference, windows):
    chosen = []
    for candidate in reference["candidates"]:
        window = windows[candidate["windowIndex"]]
        if (Path(window["source"]).name != reference["sourceName"]
                or not window["static"] or window["track"] != candidate["track"]
                or abs(window["sceneStart"] - candidate["sceneStart"]) > .01
                or abs(window["sceneEnd"] - candidate["sceneEnd"]) > .01):
            raise ValueError("Frozen observation no longer matches reviewed scene: " + candidate["id"])
        chosen.append(window)
    return chosen


def queries(reference, rankings):
    return [{"id": c["id"], "positiveIds": c["positiveIds"],
             "negativeIds": c["negativeIds"], "rankedIds": rankings[c["id"]]}
            for c in reference["candidates"] if c["positiveIds"]]


def probe_rankings(reference, report):
    candidates = reference["candidates"]
    if report["windowCount"] != len(candidates):
        raise ValueError("Probe did not use the reviewed pool")
    ranks = {c["id"]: [] for c in candidates}
    for row in report["results"]:
        if row["headOnlyComparison"]:
            continue
        a, b = row["leftIndex"], row["rightIndex"]
        if not (0 <= a < len(candidates) and 0 <= b < len(candidates)) or a == b:
            raise ValueError("Invalid probe pair")
        left, right = candidates[a]["id"], candidates[b]["id"]
        if right not in ranks[left]: ranks[left].append(right)
        if left not in ranks[right]: ranks[right].append(left)
    return ranks


def application_rankings(reference, results):
    candidates = reference["candidates"]
    ranks = {c["id"]: [] for c in candidates}
    def match(start, end):
        found = [c["id"] for c in candidates if abs(c["sceneStart"] - start) <= .01
                 and abs(c["sceneEnd"] - end) <= .01]
        if len(found) > 1: raise ValueError("Ambiguous scene mapping")
        return found[0] if found else None
    for row in results:
        if row["headOnlyComparison"]: continue
        sides = []
        for side in ("left", "right"):
            start, end = row[side+"SceneStart"], row[side+"SceneEnd"]
            ranges = [(start, end)]
            ranges += [(member["sceneStart"], member["sceneEnd"])
                       for member in row.get(side+"Family", {}).get("members", [])]
            known = {identity for a,b in ranges if (identity := match(a,b)) is not None}
            sides.append((known, f"unjudged:{start:.6f}-{end:.6f}"))
        for (known, _), (others, fallback) in ((sides[0], sides[1]), (sides[1], sides[0])):
            for identity in known:
                for candidate in sorted(others) if others else [fallback]:
                    if candidate not in ranks[identity]: ranks[identity].append(candidate)
    return ranks


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("video", "windows", "params", "application", "probe", "output"):
        parser.add_argument("--" + name, type=Path, required=True)
    args = parser.parse_args()
    reference = json.loads(REFERENCE.read_text(encoding="utf8"))
    for name, expected in (("video", "sourceSha256"), ("windows", "windowsSha256"),
                           ("params", "paramsSha256"), ("application", "applicationSha256")):
        if digest(getattr(args, name)) != reference[expected]:
            raise ValueError(name + " differs from the reviewed capture")
    windows = json.loads(args.windows.read_text(encoding="utf8"))
    chosen = selected_windows(reference, windows)
    application = json.loads(args.application.read_text(encoding="utf-8-sig"))
    if len(application["inputVideos"]) != 1 or Path(application["inputVideos"][0]).name != reference["sourceName"]:
        raise ValueError("Application output comes from a different source")
    with tempfile.TemporaryDirectory() as directory:
        pool = Path(directory) / "windows.json"
        pool.write_text(json.dumps(chosen, separators=(",", ":")), encoding="utf8")
        output = subprocess.run([str(args.probe.resolve()), str(pool), "--all", str(args.params.resolve())],
                                capture_output=True, check=True, timeout=120)
    probe = json.loads(output.stdout)
    application_queries = queries(reference, application_rankings(reference, application["results"]))
    probe_queries = queries(reference, probe_rankings(reference, probe))
    result = {"schemaVersion": 1, "referenceSha256": digest(REFERENCE),
              "applicationSha256": digest(args.application), "probeSha256": digest(args.probe),
              "review": reference["review"],
              "scope": "Two directions of one positive pair plus one hard negative; not population accuracy. "
                       "Body-action matches only. All unreviewed scenes remain unjudged.",
              "applicationResultCount": len(application["results"]),
              "application": {"queries": application_queries,
                              "evaluation": evaluate({"queries": application_queries}, ks=(1,))},
              "threeWindowReplay": {"queries": probe_queries,
                                    "evaluation": evaluate({"queries": probe_queries}, ks=(1,))}}
    args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf8")
    print(json.dumps({"application":result["application"]["evaluation"],
                      "threeWindowReplay":result["threeWindowReplay"]["evaluation"]}, indent=2))


if __name__ == "__main__": main()
