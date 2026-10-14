"""Export the pinned AVA person-action model and verify its runtime numerics.

No app integration, accuracy claim, or source-cache changes happen here.
"""
import argparse
import hashlib
import json
import time
from pathlib import Path

import numpy as np
import onnx
import onnxruntime as ort
import torch
from pytorchvideo.models.hub import slowfast_r50_detection
from audit_ava_actions import MODEL_SHA


class Detector(torch.nn.Module):
    def __init__(self, model):
        super().__init__()
        self.model = model

    def forward(self, clip, boxes):
        indices = torch.linspace(0, 31, 8, device=clip.device).long()
        slow = clip.index_select(2, indices)
        return self.model([slow, clip], boxes)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--model-root', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    checkpoint = args.model_root/'SLOWFAST_8x8_R50_DETECTION.pyth'
    if hashlib.sha256(checkpoint.read_bytes()).hexdigest() != MODEL_SHA:
        raise ValueError('Checkpoint differs from the audited official file')
    args.output.mkdir(exist_ok=False)
    torch.set_num_threads(8)
    model = slowfast_r50_detection(pretrained=False).eval()
    state = torch.load(checkpoint, map_location='cpu', weights_only=True)
    model.load_state_dict(state['model_state'], strict=True)
    del state
    wrapped = Detector(model).eval()
    generator = torch.Generator().manual_seed(20261014)
    clip = torch.randn((1, 3, 32, 256, 455), generator=generator)*.25
    boxes = torch.tensor([[0., 80., 15., 320., 252.]])
    target = args.output/'ava-slowfast-r50.onnx'
    clock = time.perf_counter()
    torch.onnx.export(wrapped, (clip, boxes), target, input_names=['clip', 'boxes'],
        output_names=['actions'], dynamic_axes={'clip': {3: 'height', 4: 'width'},
        'boxes': {0: 'subjects'}, 'actions': {0: 'subjects'}}, opset_version=17,
        do_constant_folding=True, dynamo=False)
    onnx.checker.check_model(str(target))
    options = ort.SessionOptions(); options.intra_op_num_threads = 8
    session = ort.InferenceSession(str(target), options, providers=['CPUExecutionProvider'])
    rows = []
    for width in [455, 614]:
        clip = torch.randn((1, 3, 32, 256, width), generator=generator)*.25
        for count in [1, 2]:
            boxes = torch.tensor([[0., width*.15, 10., width*.65, 250.],
                                  [0., width*.45, 35., width*.95, 240.]])[:count]
            with torch.inference_mode():
                expected = wrapped(clip, boxes).numpy()
            actual = session.run(['actions'], {'clip': clip.numpy(), 'boxes': boxes.numpy()})[0]
            if actual.shape != (count, 80) or not np.isfinite(actual).all():
                raise ValueError('Unexpected exported output')
            error = float(np.max(np.abs(expected-actual)))
            if error > 2e-4:
                raise ValueError(f'Export parity failed: {error}')
            rows.append({'width': width, 'subjects': count, 'maxAbsoluteDifference': error})
            print(rows[-1], flush=True)
    report = {'scope': 'Numerical ONNX export parity only, not semantic accuracy',
        'checkpointSha256': MODEL_SHA, 'modelSha256': hashlib.sha256(target.read_bytes()).hexdigest(),
        'modelBytes': target.stat().st_size, 'torch': torch.__version__, 'onnxruntime': ort.__version__,
        'elapsedSeconds': time.perf_counter()-clock, 'cases': rows}
    (args.output/'parity.json').write_text(json.dumps(report, indent=2), encoding='utf-8')


if __name__ == '__main__':
    main()
