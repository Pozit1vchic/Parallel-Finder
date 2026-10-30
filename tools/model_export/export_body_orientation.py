"""Reproduce the pinned JointBDOE-S research export; never downloads assets.

This is investigation tooling, not a production model installer. Redistribution,
body-angle convention, subject association and film-domain accuracy still need
validation before activation. The original checkpoint is loaded weights-only.
"""
import argparse
import hashlib
import importlib
import json
from pathlib import Path
import pickletools
import subprocess
import sys
import zipfile

SOURCE_COMMIT = "362f999e22bd50a4e73aca882b58c13f8a96a13c"
CHECKPOINT_SHA256 = "bc6d63ee0f685a888e5ff94a84d8244ce23a817223010e100459137bacae3e27"
CHECKPOINT_URL = "https://huggingface.co/HoyerChou/JointBDOE/resolve/main/coco_s_1024_e500_t010_w005_best.pt"


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", required=True, type=Path)
    parser.add_argument("--checkpoint", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    source = args.source_root.resolve()
    revision = subprocess.check_output(["git", "-C", str(source), "rev-parse", "HEAD"], text=True).strip()
    if revision != SOURCE_COMMIT:
        parser.error("Use the pinned upstream source revision: " + SOURCE_COMMIT)
    if subprocess.run(["git", "-C", str(source), "diff", "--quiet", "HEAD"], check=False).returncode:
        parser.error("Upstream tracked source must be unchanged")
    if digest(args.checkpoint) != CHECKPOINT_SHA256:
        parser.error("Checkpoint does not match the reviewed upstream JointBDOE-S asset")
    temporary = args.output.with_suffix(".onnx.part")
    metadata_temporary = args.output.with_suffix(".json.part")
    if any(path.exists() for path in (args.output, args.output.with_suffix(".json"), temporary, metadata_temporary)):
        parser.error("Choose a new output path; research exports are not overwritten")

    import numpy as np
    import torch
    if torch.__version__.split("+")[0] != "2.9.1":
        parser.error("This export recipe is pinned to torch 2.9.1")
    sys.path.insert(0, str(source))
    allowed = []
    with zipfile.ZipFile(args.checkpoint) as archive:
        names = [name for name in archive.namelist() if name.endswith("data.pkl")]
        if len(names) != 1:
            parser.error("Expected one checkpoint object graph")
        # Imports are restricted by the exact checkpoint/source hashes above.
        globals_used = sorted({arg for op, arg, _ in pickletools.genops(archive.read(names[0]))
                               if op.name == "GLOBAL"})
    for name in globals_used:
        module, attribute = name.split(" ")
        imported = "builtins" if module == "__builtin__" else module
        allowed.append((getattr(importlib.import_module(imported), attribute), name.replace(" ", ".")))
    allowed.extend([np.dtypes.Float64DType, np.dtypes.Float32DType, np.dtypes.Int64DType])
    with torch.serialization.safe_globals(allowed):
        checkpoint = torch.load(args.checkpoint, map_location="cpu", weights_only=True)
    model = (checkpoint.get("ema") or checkpoint["model"]).float().eval()
    for layer in model.modules():
        if isinstance(layer, torch.nn.Upsample) and not hasattr(layer, "recompute_scale_factor"):
            layer.recompute_scale_factor = None
    torch.set_num_threads(4)

    class Export(torch.nn.Module):
        def __init__(self, model):
            super().__init__()
            self.model = model

        def forward(self, image):
            return self.model(image)[0]

    wrapper = Export(model)
    example = torch.zeros(1, 3, 1024, 1024)
    with torch.inference_mode():
        result = wrapper(example)
    if list(result.shape) != [1, 65280, 7]:
        parser.error("Unexpected body-orientation output contract: " + str(list(result.shape)))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    torch.onnx.export(wrapper, example, str(temporary), opset_version=17,
                      input_names=["image"], output_names=["people_and_orientation"], dynamo=False)
    import onnx
    onnx.checker.check_model(str(temporary))
    metadata = dict(status="research-only", source="https://github.com/hnuzhy/JointBDOE",
                    sourceCommit=revision, checkpointUrl=CHECKPOINT_URL,
                    checkpointSha256=CHECKPOINT_SHA256, onnxSha256=digest(temporary),
                    torchVersion=torch.__version__, inputShape=[1, 3, 1024, 1024],
                    outputShape=list(result.shape), opset=17,
                    preprocessing="RGB / 255, aspect-preserving letterbox with 114 padding",
                    output="cx cy width height objectness person-score raw-angle/360",
                    orientationConfidence="not supplied by the detection score",
                    remaining="angle convention, subject association, real view controls, redistribution review")
    metadata_temporary.write_text(json.dumps(metadata, indent=2) + "\n", encoding="utf8")
    temporary.rename(args.output)
    metadata_temporary.rename(args.output.with_suffix(".json"))
    print(json.dumps(metadata, indent=2))


if __name__ == "__main__":
    main()
