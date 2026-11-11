"""Prepare observed single-subject boxes for the existing AVA audit tool.

One published action interval per source (the first), two seconds around its
median observable person frame. No action label influences box selection.
Cases without an observed single person remain explicit unavailable outcomes.
"""
import argparse
import json
from pathlib import Path

from evaluate_kth_controls import cases, read, sha256


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dataset-manifest", type=Path, required=True)
    parser.add_argument("--observations", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    dataset, grouped, _, contract = cases(args.dataset_manifest, args.observations)
    args.output.mkdir(parents=True, exist_ok=True)
    by_source = {}
    for windows in grouped.values():
        for window in windows:
            by_source.setdefault(window["source"], window)
    prepared, unavailable = [], []
    for index, (source, window) in enumerate(sorted(by_source.items())):
        observed = read(args.observations / (source + ".poses.json"))
        eligible = [f for f in observed["frames"] if f["detections"] == 1 and len(f["box"]) == 4
                    and window["evaluationStart"] <= f["time"] < window["evaluationEnd"]]
        case_id = f"case-{index:04d}"
        if not eligible:
            unavailable.append({"id": case_id, "source": source, "action": window["action"], "reason": "no single observed person"})
            continue
        anchor = eligible[len(eligible) // 2]
        length = min(2., window["evaluationEnd"] - window["evaluationStart"])
        start = max(window["evaluationStart"], min(anchor["time"] - length / 2, window["evaluationEnd"] - length))
        end = start + length
        if length < .5 or not start <= anchor["time"] < end:
            unavailable.append({"id": case_id, "source": source, "action": window["action"], "reason": "interval too short"})
            continue
        source_path = (args.dataset_manifest.parent / source).resolve()
        roi = {"source": str(source_path), "selectionPolicy": "single-subject-dataset",
               "observations": [{"time": anchor["time"], "width": observed["width"], "height": observed["height"],
                                 "people": [{"box": anchor["box"]}]}]}
        roi_path = args.output / (case_id + "-roi.json")
        roi_path.write_text(json.dumps(roi, indent=2) + "\n", encoding="utf-8")
        prepared.append({"id": case_id, "source": str(source_path), "start": start, "end": end,
                         "roi": str(roi_path.resolve()), "expectedDatasetAction": window["action"],
                         "sourceSha256": observed["contract"]["sourceSha256"]})
    batches = []
    for index in range(0, len(prepared), 16):
        path = args.output / f"cases-{index // 16:02d}.json"
        path.write_text(json.dumps(prepared[index:index + 16], indent=2) + "\n", encoding="utf-8")
        batches.append(path.name)
    report = {"schemaVersion": 1, "split": dataset["split"], "datasetManifestSha256": sha256(args.dataset_manifest),
              "observationContract": contract, "scope": "Raw video-action model; oracle single-subject dataset, no identity proof",
              "selection": "First annotated interval per source; observed median single-person frame; two-second clip",
              "planned": len(by_source), "available": len(prepared), "unavailable": unavailable, "batches": batches}
    (args.output / "manifest.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
