"""Independent Pillow/torchvision reference for the native model input."""
import argparse
import datetime
import json
from pathlib import Path
import subprocess
import numpy as np
from PIL import Image
import torch
from torchvision import transforms
import onnxruntime as ort


def validate(root, probe):
    torch.set_num_threads(2)
    directory = root / ('native-input-' + datetime.datetime.now().strftime('%Y%m%d-%H%M%S-%f'))
    directory.mkdir()
    prepare = transforms.Compose([transforms.Resize(256), transforms.CenterCrop(224),
        transforms.ToTensor(), transforms.Normalize([.485, .456, .406], [.229, .224, .225])])
    dimensions = [(16, 16), (97, 121), (121, 97), (256, 257), (257, 256),
                  (503, 617), (617, 503), (1159, 1301), (1301, 1159), (31, 247)]
    rng = np.random.default_rng(19821)
    rows, reference = [], []
    for pattern in range(3):
        for width, height in dimensions:
            y, x = np.indices((height + 10, width + 12))
            if pattern == 0:
                pixels = rng.integers(0, 256, (height + 10, width + 12, 3), dtype=np.uint8)
            elif pattern == 1:
                pixels = np.stack((x * 7 + y * 3, y * 11, x * 13), axis=2).astype(np.uint8)
            else:
                pixels = np.repeat((((x + y) % 2) * 255).astype(np.uint8)[:, :, None], 3, axis=2)
            name = str(len(rows))
            path = directory / (name + '.png')
            Image.fromarray(pixels).save(path)
            crop = [5, 3, width + 5, height + 3]
            reference.append(prepare(Image.fromarray(pixels).crop(crop)).numpy().ravel())
            rows.append({'id': name, 'path': str(path), 'crop': crop,
                         'tensorPath': str(directory / (name + '.float32'))})
    request = directory / 'request.json'
    request.write_text(json.dumps({'images': rows}))
    subprocess.run([str(probe), str(request), str(directory / 'native.json')], check=True)
    differences = []
    for row, expected in zip(rows, reference):
        actual = np.fromfile(row['tensorPath'], dtype=np.float32)
        if actual.shape != expected.shape:
            raise ValueError('Incorrect native shape')
        difference = float(np.max(np.abs(expected - actual)))
        differences.append(difference)
        np.testing.assert_allclose(actual, expected, rtol=0, atol=1e-7)
    model = root / 'head-pose-6drepnet360.onnx'
    model_rows = [{k: v for k, v in row.items() if k != 'tensorPath'} for row in rows[:12]]
    options = ort.SessionOptions(); options.intra_op_num_threads = 2
    session = ort.InferenceSession(str(model), options, providers=['CPUExecutionProvider'])
    reference_rotations = [session.run(None, {'images': x.reshape(1, 3, 224, 224)})[0].ravel()
                           for x in reference[:12]]
    rotations = {}
    for provider in ('cpu', 'cuda'):
        provider_request = directory / (provider + '-request.json')
        provider_request.write_text(json.dumps({'modelPath': str(model), 'provider': provider, 'images': model_rows}))
        provider_result = directory / (provider + '.json')
        subprocess.run([str(probe), str(provider_request), str(provider_result)], check=True)
        rotations[provider] = [np.asarray(row['rotation']) for row in json.loads(provider_result.read_text())['images']]
        for expected, actual in zip(reference_rotations, rotations[provider]):
            np.testing.assert_allclose(actual, expected, rtol=0, atol=3e-5)
    report = {'inputCases': len(rows), 'maximumInputError': max(differences),
              'modelCasesPerProvider': len(model_rows), 'providers': list(rotations),
              'maximumCpuRotationError': max(float(np.max(np.abs(a - b))) for a, b in zip(reference_rotations, rotations['cpu'])),
              'maximumCudaRotationError': max(float(np.max(np.abs(a - b))) for a, b in zip(reference_rotations, rotations['cuda'])),
              'professionalMovementAccuracyEstablished': False}
    (directory / 'validation.json').write_text(json.dumps(report, indent=2))
    print(json.dumps({'directory': str(directory), **report}), flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--root', type=Path, required=True)
    parser.add_argument('--probe', type=Path, required=True)
    args = parser.parse_args()
    validate(args.root, args.probe)
