"""Export the pinned YOLO26m-pose checkpoint to a static-batch ONNX asset."""
from pathlib import Path
import os
import hashlib
import json

# Never let Ultralytics mutate the Python installation behind our back.  All
# export dependencies are expected to be provisioned in D:\\PythonLibs.
os.environ.setdefault("YOLO_AUTOINSTALL", "False")

from ultralytics import YOLO

ROOT = Path(__file__).resolve().parent
OUT = Path(os.environ.get("PF_MODEL_OUTPUT", r"D:\PF_CUDA\models"))
BATCH = int(os.environ.get("PF_MODEL_BATCH", "1"))
if BATCH not in (1, 8, 16):
    raise ValueError("PF_MODEL_BATCH must be one of 1, 8 or 16")
OUT.mkdir(parents=True, exist_ok=True)

model = YOLO("yolo26m-pose.pt")
exported = Path(model.export(
    format="onnx",
    imgsz=640,
    batch=BATCH,
    dynamic=False,
    simplify=False,
    opset=17,
    nms=False,
))
target = OUT / f"yolo26m-pose-640-b{BATCH}.onnx"
target.write_bytes(exported.read_bytes())
sha256 = hashlib.sha256(target.read_bytes()).hexdigest()
print(json.dumps({
    "filename": target.name,
    "sizeBytes": target.stat().st_size,
    "sha256": sha256,
    "batch": BATCH,
    "input": [BATCH, 3, 640, 640],
}, indent=2))
print(target)
