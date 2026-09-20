"""Export every local YOLO-pose checkpoint and build a GitHub Releases manifest.

The application consumes ONNX files, never the original Ultralytics .pt files.
This script is the bridge between the private model download directory and a
release asset bundle:

    python export_pose_release.py \
      --input-dir D:\\YOLO_Download_Project\\models \
      --output-dir D:\\Parallel-Finder\\release-models

Upload every generated .onnx file and manifest.json to the same GitHub release.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
from pathlib import Path

os.environ.setdefault("YOLO_AUTOINSTALL", "False")

from ultralytics import YOLO


def canonical_stem(stem: str) -> str:
    # Older local bundles sometimes use yolo8n instead of the canonical
    # upstream name yolov8n. Keep the release catalog stable.
    if stem.startswith("yolo8"):
        return "yolov8" + stem[len("yolo8") :]
    return stem


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def export_checkpoint(checkpoint: Path, output_dir: Path, image_size: int, batch: int) -> Path:
    model = YOLO(str(checkpoint))
    exported = Path(
        model.export(
            format="onnx",
            imgsz=image_size,
            batch=batch,
            dynamic=False,
            simplify=False,
            opset=17,
            nms=False,
        )
    )
    target = output_dir / f"{canonical_stem(checkpoint.stem)}.onnx"
    shutil.copy2(exported, target)
    return target


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--input-dir",
        type=Path,
        default=Path(r"D:\YOLO_Download_Project\models"),
    )
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=Path(r"D:\Parallel-Finder\release-models"),
    )
    parser.add_argument("--imgsz", type=int, default=640)
    parser.add_argument("--batch", type=int, choices=(1,), default=1)
    parser.add_argument(
        "--release-base",
        default="https://github.com/Pozit1vchic/Parallel-Finder/releases/download/v0.1.0-models/",
    )
    args = parser.parse_args()

    if not args.input_dir.is_dir():
        raise SystemExit(f"input directory does not exist: {args.input_dir}")
    args.output_dir.mkdir(parents=True, exist_ok=True)
    checkpoints = sorted(args.input_dir.glob("*pose*.pt"))
    if not checkpoints:
        raise SystemExit(f"no *pose*.pt files found in {args.input_dir}")

    assets = []
    for checkpoint in checkpoints:
        target = export_checkpoint(checkpoint, args.output_dir, args.imgsz, args.batch)
        assets.append(
            {
                "filename": target.name,
                "url": args.release_base.rstrip("/") + "/" + target.name,
                "sizeBytes": target.stat().st_size,
                "sha256": sha256(target),
                "license": "AGPL-3.0",
                "minimumAppVersion": "0.1.0",
                "input": [args.batch, 3, args.imgsz, args.imgsz],
            }
        )
        print(f"exported {checkpoint.name} -> {target.name}")

    manifest = {
        "schemaVersion": 1,
        "generatedFrom": str(args.input_dir),
        "models": assets,
    }
    manifest_path = args.output_dir / "manifest.json"
    manifest_path.write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"manifest: {manifest_path}")


if __name__ == "__main__":
    main()
