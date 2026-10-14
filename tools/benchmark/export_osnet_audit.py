"""Export a separately downloaded upstream OSNet checkpoint for local audits.

Does not install or replace application models. Source/checkpoint provenance
must be recorded by the caller; checkpoint loading uses weights_only=True.
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import numpy as np
import onnxruntime as ort
import torch


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--weights', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--report', type=Path, required=True)
    args = parser.parse_args()
    if args.output.exists() or args.report.exists():
        raise FileExistsError('Audit outputs must be new')
    spec = importlib.util.spec_from_file_location('audited_osnet', args.source)
    source = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(source)
    checkpoint = torch.load(args.weights, map_location='cpu', weights_only=True)
    state = checkpoint.get('state_dict', checkpoint)
    state = {key.removeprefix('module.'): value for key, value in state.items()}
    model = source.osnet_x1_0(num_classes=state['classifier.weight'].shape[0], pretrained=False)
    model.load_state_dict(state, strict=True)
    model.eval()
    torch.set_num_threads(1)
    torch.manual_seed(20261013)
    sample = torch.randn(1, 3, 256, 128)
    with torch.no_grad():
        expected = model(sample).numpy()
        torch.onnx.export(model, sample, str(args.output), input_names=['images'],
                          output_names=['embedding'], opset_version=17, dynamo=False)
    options = ort.SessionOptions()
    options.intra_op_num_threads = 1
    session = ort.InferenceSession(str(args.output), options, providers=['CPUExecutionProvider'])
    actual = session.run(None, {'images': sample.numpy()})[0]
    relative_error = np.linalg.norm(actual-expected) / max(np.linalg.norm(expected), 1e-12)
    cosine = float(np.dot(actual.ravel(), expected.ravel())
                   / max(np.linalg.norm(actual)*np.linalg.norm(expected), 1e-12))
    if (actual.shape != (1, 512) or not np.isfinite(actual).all()
            or relative_error > 1e-4 or cosine < .99999
            or np.max(abs(actual-expected)) > 1e-3):
        raise ValueError('ONNX descriptor parity failed')
    def sha(path):
        return hashlib.sha256(path.read_bytes()).hexdigest()
    with args.report.open('x', encoding='utf-8') as destination:
        json.dump(dict(sourceSha256=sha(args.source), weightsSha256=sha(args.weights),
                       onnxSha256=sha(args.output), inputShape=[1, 3, 256, 128],
                       outputShape=list(actual.shape), maxAbsoluteError=float(np.max(abs(actual-expected))),
                       relativeL2Error=float(relative_error), cosine=cosine,
                       scope='export parity on one fixed tensor; not identity accuracy'), destination, indent=2)


if __name__ == '__main__':
    main()
