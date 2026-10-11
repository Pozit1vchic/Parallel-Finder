"""Rebatch the pinned float32 YOLO26m ONNX without touching trained weights.

This is a diagnostic export, not an automatic replacement of a user's model.
The exact source digest guards the reviewed graph structure. Output parity and
whole-pipeline accuracy/performance still have to be tested before deployment.
"""
import argparse
import ast
import hashlib
import json
from pathlib import Path

import numpy as np
import onnx

SOURCE_SHA256 = "dc8741d097d642df343361cb956087e2991140ababcb6e063a6f077968c28fe1"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--batch", type=int, choices=(2, 4, 8), required=True)
    args = parser.parse_args()
    source = args.source.read_bytes()
    if hashlib.sha256(source).hexdigest() != SOURCE_SHA256:
        raise ValueError("This converter only supports the reviewed pinned batch-one graph")
    if args.output.exists():
        raise FileExistsError("Refusing to replace an existing model")
    model = onnx.load_from_string(source)
    weights_before = {t.name: t.SerializeToString() for t in model.graph.initializer}
    constants = {n.output[0]: n for n in model.graph.node if n.op_type == "Constant"}
    changes = []
    for node in model.graph.node:
        if node.op_type != "Reshape" or node.input[1] not in constants:
            continue
        constant = constants[node.input[1]]
        value = onnx.numpy_helper.to_array(constant.attribute[0].t).copy()
        if value.ndim == 1 and value.size >= 3 and value[0] == 1:
            # Reviewed attention and detection-head reshapes all carry batch
            # in their first slot. Anchor broadcasts and Tile repeats do not.
            value[0] = args.batch
            constant.attribute[0].t.CopyFrom(onnx.numpy_helper.from_array(value))
            changes.append(constant.name)
    if len(changes) != 17:
        raise ValueError(f"Unexpected reshape count: {len(changes)}")
    offsets = next(n for n in model.graph.node if n.name == "/model.23/Constant_38")
    if onnx.numpy_helper.to_array(offsets.attribute[0].t).tolist() != [[0]]:
        raise ValueError("Unexpected batch offset constant")
    offsets.attribute[0].t.CopyFrom(onnx.numpy_helper.from_array(np.arange(args.batch, dtype=np.int64).reshape(-1, 1)))
    model.graph.input[0].type.tensor_type.shape.dim[0].dim_value = args.batch
    model.graph.output[0].type.tensor_type.shape.dim[0].dim_value = args.batch
    for entry in model.metadata_props:
        if entry.key == "args":
            export_args = ast.literal_eval(entry.value)
            export_args["batch"] = args.batch
            entry.value = str(export_args)
    if {t.name: t.SerializeToString() for t in model.graph.initializer} != weights_before:
        raise ValueError("Trained initializer data changed")
    onnx.checker.check_model(model)
    # Inference validates the expanded graph's dimensional contracts; retain
    # the original graph's serialization structure and trained initializer data.
    inferred = onnx.shape_inference.infer_shapes(model, strict_mode=True)
    shape = [d.dim_value for d in inferred.graph.output[0].type.tensor_type.shape.dim]
    if shape != [args.batch, 300, 57]:
        raise ValueError(f"Unexpected output shape: {shape}")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    data = model.SerializeToString()
    with args.output.open("xb") as output:
        output.write(data)
    print(json.dumps({"sourceSha256": SOURCE_SHA256, "outputSha256": hashlib.sha256(data).hexdigest(),
                      "batch": args.batch, "sizeBytes": len(data), "reshapesChanged": len(changes),
                      "trainedWeightsIdentical": True, "output": str(args.output)}, indent=2))


if __name__ == "__main__":
    main()
