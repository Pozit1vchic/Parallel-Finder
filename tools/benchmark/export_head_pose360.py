"""Export the author's audited 6DRepNet360 definitions, not its test launcher.

The source/weights must be downloaded separately from the official repository.
Weights are loaded with weights_only=True; PyTorch is a build dependency only.
"""
import argparse
import ast
import hashlib
import json
import math
from pathlib import Path
from types import SimpleNamespace

import numpy as np
import onnxruntime as ort
import torch
from torch import nn
from torchvision.models.resnet import Bottleneck


def load_model(root):
    namespace = {'torch': torch, 'nn': nn, 'math': math, 'np': np}
    utils = ast.parse((root / 'sixdrepnet360/utils.py').read_text())
    names = {'normalize_vector', 'cross_product', 'compute_rotation_matrix_from_ortho6d',
             'compute_euler_angles_from_rotation_matrices'}
    definitions = [n for n in utils.body if isinstance(n, ast.FunctionDef) and n.name in names]
    if len(definitions) != len(names):
        raise ValueError('Unexpected official utility definitions')
    exec(compile(ast.Module(body=definitions, type_ignores=[]), '<audited-rotation-functions>', 'exec'), namespace)
    namespace['utils'] = SimpleNamespace(**{name: namespace[name] for name in names})
    source = ast.parse((root / 'sixdrepnet360/test.py').read_text())
    definitions = [n for n in source.body if isinstance(n, ast.ClassDef) and n.name == 'SixDRepNet360']
    if len(definitions) != 1:
        raise ValueError('Unexpected official model definition')
    exec(compile(ast.Module(body=definitions, type_ignores=[]), '<audited-model-definition>', 'exec'), namespace)
    model = namespace['SixDRepNet360'](Bottleneck, [3, 4, 6, 3], 6)
    weights = root / '6DRepNet360_Full-Rotation_300W_LP+Panoptic.pth'
    state = torch.load(weights, map_location='cpu', weights_only=True)
    model.load_state_dict(state, strict=True)
    return model.eval(), namespace['compute_euler_angles_from_rotation_matrices']


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('root', type=Path)
    args = parser.parse_args()
    print('Loading audited model and tensor-only weights', flush=True)
    torch.set_num_threads(6)
    torch.manual_seed(2412)
    model, euler = load_model(args.root)
    print('Exporting fixed 224x224 rotation model', flush=True)
    output = args.root / 'head-pose-6drepnet360.onnx'
    example = torch.randn(1, 3, 224, 224)
    torch.onnx.export(model, example, str(output), input_names=['images'],
        output_names=['rotation'], opset_version=17, dynamo=False)
    print('Checking twelve independent tensor inputs', flush=True)
    options = ort.SessionOptions()
    options.intra_op_num_threads = 6
    session = ort.InferenceSession(str(output), options, providers=['CPUExecutionProvider'])
    checks = []
    with torch.inference_mode():
        for index in range(12):
            tensor = torch.randn(1, 3, 224, 224)
            expected = model(tensor).numpy()
            actual = session.run(None, {'images': tensor.numpy()})[0]
            if not np.allclose(actual, expected, atol=3e-5, rtol=3e-5):
                raise ValueError(f'Export parity failed: {index}')
            checks.append(float(np.max(np.abs(expected - actual))))
    report = {'source': 'https://github.com/thohemp/6DRepNet360',
        'weightsSHA256': hashlib.file_digest((args.root / '6DRepNet360_Full-Rotation_300W_LP+Panoptic.pth').open('rb'), 'sha256').hexdigest(),
        'onnxSHA256': hashlib.file_digest(output.open('rb'), 'sha256').hexdigest(),
        'exportParityCases': len(checks), 'maximumRotationElementError': max(checks),
        'professionalMovementAccuracyEstablished': False}
    (args.root / 'export-report.json').write_text(json.dumps(report, indent=2))
    print(json.dumps(report, indent=2))
