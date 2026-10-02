"""Compare the application's exact landmarks against OpenCV's SFace alignment.

Audit-only dependency: opencv-python-headless, numpy. Never used by the app.
Fixed detector landmarks ensure this changes alignment only, not detection.
Outputs are diagnostic evidence, never independent identity ground truth.
"""
import argparse
import json
from pathlib import Path

import cv2
import numpy as np


def cosine(a, b):
    a = np.asarray(a, dtype=np.float64).ravel()
    b = np.asarray(b, dtype=np.float64).ravel()
    if not a.size or a.size != b.size:
        return None
    return float(a @ b / (np.linalg.norm(a) * np.linalg.norm(b)))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("probe")
    parser.add_argument("recognizer")
    parser.add_argument("output")
    args = parser.parse_args()
    output = Path(args.output)
    if output.exists():
        raise SystemExit("Refusing to overwrite reviewed alignment diagnostics")
    output.mkdir(parents=True)
    cv2.setNumThreads(1)
    recognizer = cv2.FaceRecognizerSF.create(args.recognizer, "")
    samples = json.loads(Path(args.probe).read_text(encoding="utf-8-sig"))
    rows = []
    features = {}
    for sample in samples:
        if not sample["accepted"]:
            rows.append({"id": sample["id"], "accepted": False})
            continue
        image = cv2.imdecode(np.fromfile(sample["image"], dtype=np.uint8), cv2.IMREAD_COLOR)
        face = np.zeros((1, 15), dtype=np.float32)
        face[0, 4:14] = sample["landmarks"]
        face[0, 14] = sample["detectorScore"]
        official = recognizer.alignCrop(image, face)
        inverse = np.asarray(sample["canonicalToSource"], dtype=np.float64).reshape(2, 3)
        existing = cv2.warpAffine(image, inverse, (112, 112),
                                  flags=cv2.INTER_LINEAR | cv2.WARP_INVERSE_MAP)
        reference_feature = recognizer.feature(official).ravel()
        old_feature = recognizer.feature(existing).ravel()
        features[sample["id"]] = (sample["face"], reference_feature, old_feature)
        combined = np.concatenate((existing, official), axis=1)
        cv2.imwrite(str(output / f"aligned-{sample['id']:03d}.png"), combined)
        rows.append({"id": sample["id"], "time": sample["time"], "track": sample["track"],
                     "accepted": True, "landmarks": sample["landmarks"],
                     "applicationVsOfficialCosine": cosine(sample["face"], reference_feature),
                     "applicationVsSameTransformOpenCvCosine": cosine(sample["face"], old_feature),
                     "originalObservationVsProbeCosine": cosine(sample["observedFace"], sample["face"])})
    pairs = []
    for a, left in features.items():
        for b, right in features.items():
            if a >= b:
                continue
            pairs.append({"a": a, "b": b, "applicationCosine": cosine(left[0], right[0]),
                          "officialAlignmentCosine": cosine(left[1], right[1]),
                          "sameTransformOpenCvCosine": cosine(left[2], right[2])})
    report = {"opencv": cv2.__version__, "samples": rows, "pairs": pairs,
              "policy": "Fixed detector landmarks; alignment-only diagnostic, not identity ground truth"}
    (output / "comparison.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
