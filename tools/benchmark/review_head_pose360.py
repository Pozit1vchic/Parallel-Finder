"""Read-only, source-frame experiment on fixed review intervals.

No cache updates, identity assertions or production classifier changes.
YuNet faces must be associated with the cached person's measured nose.
"""
import argparse
import json
import math
from pathlib import Path
import subprocess
import time
import torch

import cv2
import numpy as np
import onnxruntime as ort
from PIL import Image
from torchvision import transforms


def angles(rotation):
    r = rotation
    sy = math.hypot(r[0, 0], r[1, 0])
    if sy < 1e-6:
        return [math.degrees(math.atan2(-r[1, 2], r[1, 1])),
                math.degrees(math.atan2(-r[2, 0], sy)), 0.]
    return [math.degrees(math.atan2(r[2, 1], r[2, 2])),
            math.degrees(math.atan2(-r[2, 0], sy)),
            math.degrees(math.atan2(r[1, 0], r[0, 0]))]


def review(root, model_root):
    torch.set_num_threads(4)
    observations = root / 'batya-observations'
    experiment = root / 'headpose-experiment'
    detector = cv2.FaceDetectorYN.create(str(model_root / 'face_detection_yunet_2023mar.onnx'),
        '', (640, 360), .85, .3, 100)
    options = ort.SessionOptions(); options.intra_op_num_threads = 6
    session = ort.InferenceSession(str(experiment / 'head-pose-6drepnet360.onnx'),
        options, providers=['CPUExecutionProvider'])
    prepare = transforms.Compose([transforms.Resize(256), transforms.CenterCrop(224),
        transforms.ToTensor(), transforms.Normalize([.485, .456, .406], [.229, .224, .225])])
    cases = json.loads((observations / 'independent-motion-reviewed.json').read_text(encoding='utf-8-sig'))['cases']
    reports = {}
    for video in range(1, 4):
        reports[video] = {row['caseId']: row for row in json.loads(
            (observations / f'independent-{video}-final-compound.json').read_text(encoding='utf-8-sig'))['results']}
    previous = experiment / 'source-review.json'
    saved = json.loads(previous.read_text()) if previous.exists() else {}
    results = saved.get('results', []); inference_ms = saved.get('inferenceMilliseconds', 0.)
    completed = {row['caseId'] for row in results}
    started = time.perf_counter()
    for case in cases:
        if case['status'] in ('cut-crossing-excluded', 'development-overlap-excluded'):
            continue
        if case['caseId'] in completed:
            continue
        frames = reports[case['videoId']][case['caseId']]['leftFrames']
        source_report = json.loads((observations / f"native-{case['caseId']}-cuts.json").read_text(encoding='utf-8-sig'))
        fps = source_report['fps']
        indices = [round((f['time'] - case['start']) * fps) for f in frames]
        selected = '+'.join(f'eq(n,{index})' for index in indices)
        command = ['D:/msys2/ucrt64/bin/ffmpeg.exe', '-v', 'error', '-threads', '2',
            '-ss', str(case['start']), '-t', str(case['end'] - case['start'] + .05), '-i', source_report['source'],
            '-vf', f"select='{selected}',scale=1280:720:flags=fast_bilinear", '-fps_mode', 'passthrough',
            '-pix_fmt', 'rgb24', '-f', 'rawvideo', '-']
        raw = subprocess.run(command, check=True, capture_output=True).stdout
        images = np.frombuffer(raw, np.uint8).reshape(-1, 720, 1280, 3)
        if len(images) != len(frames):
            raise ValueError(f"Source observation alignment mismatch: {case['caseId']}")
        measured = []
        for frame, image in zip(frames, images):
            note = {'time': frame['time'], 'accepted': False}
            nose = frame['points'][0]
            if nose[2] < .5:
                measured.append(note); continue
            smaller = cv2.resize(image[:, :, ::-1], (640, 360), interpolation=cv2.INTER_AREA)
            _, faces = detector.detect(smaller)
            candidates = []
            if faces is not None:
                for face in faces:
                    if math.hypot(face[8] * 2 - nose[0], face[9] * 2 - nose[1]) <= max(16., face[2] * .9):
                        candidates.append(face)
            if len(candidates) != 1:
                note['reason'] = 'missing-or-ambiguous-associated-face'; measured.append(note); continue
            face = candidates[0] * 2
            x, y, w, h = map(float, face[:4])
            # Loose, unaligned crop matching the author's evaluation convention.
            x0, y0 = max(0, math.floor(x - .4 * w)), max(0, math.floor(y - .4 * h))
            x1, y1 = min(1280, math.ceil(x + 1.4 * w)), min(720, math.ceil(y + 1.12 * h))
            crop = image[y0:y1, x0:x1]
            tensor = prepare(Image.fromarray(crop)).numpy()[None]
            before = time.perf_counter()
            rotation = session.run(None, {'images': tensor})[0][0]
            inference_ms += (time.perf_counter() - before) * 1000
            note.update({'accepted': True, 'pitchYawRoll': angles(rotation), 'rotation': rotation.tolist(),
                         'crop': [x0, y0, x1, y1]})
            measured.append(note)
        row = {'caseId': case['caseId'], 'samples': measured}
        results.append(row)
        (experiment / 'source-review.json').write_text(json.dumps({'results': results,
            'inferenceMilliseconds': inference_ms, 'elapsedSeconds': time.perf_counter() - started,
            'productionEnabled': False, 'professionalMovementAccuracyEstablished': False}, indent=2))
        values = [s['pitchYawRoll'] for s in measured if s['accepted']]
        print(case['caseId'], 'accepted', len(values), '/', len(measured),
              'angleRanges', np.ptp(values, axis=0).round(2).tolist() if values else [], flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('root', type=Path)
    parser.add_argument('models', type=Path)
    args = parser.parse_args()
    review(args.root, args.models)
