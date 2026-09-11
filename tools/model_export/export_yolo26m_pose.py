"""Export the pinned YOLO26m-pose checkpoint to a static-batch ONNX asset."""
from pathlib import Path
import os
from ultralytics import YOLO

ROOT = Path(__file__).resolve().parent
OUT = Path(os.environ.get("PF_MODEL_OUTPUT", r"D:\PF_CUDA\models"))
OUT.mkdir(parents=True, exist_ok=True)

model = YOLO("yolo26m-pose.pt")
exported = Path(model.export(
    format="onnx",
    imgsz=640,
    batch=1,
    dynamic=False,
    simplify=False,
    opset=17,
    nms=False,
))
target = OUT / "yolo26m-pose-640-b1.onnx"
target.write_bytes(exported.read_bytes())
print(target)
