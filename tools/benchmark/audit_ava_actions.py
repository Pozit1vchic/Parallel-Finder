"""Bounded, local AVA person-action audit; never classify app results here.

Use re-detected person boxes associated to the saved pose at the clip midpoint.
The AVA scores are model outputs, not calibrated accuracy or actor identity.
Repeat-frame and reverse controls test how much the output relies on movement.
"""
import argparse
import hashlib
import json
import math
import re
import subprocess
import time
from pathlib import Path

import numpy as np
import torch
import torch.nn.functional as functional
from PIL import Image, ImageDraw
from pytorchvideo.models.hub import slowfast_r50_detection


MODEL_SHA = '9bd6584a8212f91e4c7d96ec7d6bff31250e1c810fcee968471614a1b9658ac8'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cases', type=Path, required=True)
    parser.add_argument('--model-root', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--ffmpeg', type=Path, required=True)
    args = parser.parse_args()
    cases = json.loads(args.cases.read_text(encoding='utf-8-sig'))
    if not isinstance(cases, list) or not 1 <= len(cases) <= 16:
        raise ValueError('Audit requires 1–16 explicit short cases')
    checkpoint = args.model_root / 'SLOWFAST_8x8_R50_DETECTION.pyth'
    if hashlib.sha256(checkpoint.read_bytes()).hexdigest() != MODEL_SHA:
        raise ValueError('Checkpoint differs from the audited official download')
    labels_text = (args.model_root / 'ava_action_list.pbtxt').read_text()
    labels = {int(index): label for label, index in
              re.findall(r'name:\s*"([^"]+)"\s*(?:label_)?id:\s*(\d+)', labels_text)}
    if len(labels) != 60 or labels.get(14) != 'walk':
        raise ValueError('Unexpected AVA label indexing')
    prepared = []
    for case in cases:
        start, end = float(case['start']), float(case['end'])
        if not math.isfinite(start) or not math.isfinite(end) or start < 0 or not .5 <= end-start <= 3:
            raise ValueError('Explicit half-open source intervals must span .5–3 s')
        snapshot = json.loads(Path(case['roi']).read_text(encoding='utf-8-sig'))
        if Path(snapshot['source']).resolve() != Path(case['source']).resolve():
            raise ValueError('Person box belongs to another source')
        observation = snapshot['observations'][0]
        if not start <= observation['time'] < end or observation['association'] is None:
            raise ValueError('A locally associated midpoint box is required')
        person = observation['people'][observation['association']['candidate']]
        box = np.asarray(person['box'], dtype=np.float32)
        width, height = observation['width'], observation['height']
        if not np.isfinite(box).all() or box[0] >= box[2] or box[1] >= box[3]:
            raise ValueError('Invalid subject crop')
        if not Path(case['source']).is_file() or width > 1280 or height > 720:
            raise ValueError('Missing source or unbounded audit geometry')
        prepared.append((case, box, width, height))
    args.output.mkdir(exist_ok=False)
    torch.set_num_threads(8)
    model = slowfast_r50_detection(pretrained=False).eval()
    state = torch.load(checkpoint, map_location='cpu', weights_only=True)
    model.load_state_dict(state['model_state'], strict=True)
    del state
    report = {'scope': 'AVA development person-action audit, not professional accuracy, identity or app admission',
              'checkpointSha256': MODEL_SHA, 'pytorchvideo': '0.1.5', 'rows': []}
    for case, box, width, height in prepared:
        clock = time.perf_counter()
        raw = subprocess.run([str(args.ffmpeg), '-v', 'error', '-ss', str(case['start']),
            '-i', case['source'], '-t', str(case['end']-case['start']),
            '-vf', f'scale={width}:{height}', '-fps_mode', 'passthrough', '-an',
            '-f', 'rawvideo', '-pix_fmt', 'rgb24', 'pipe:1'], check=True, capture_output=True).stdout
        pixels = width*height*3
        if len(raw) % pixels or not 5 <= len(raw)//pixels <= 192:
            raise ValueError('Unexpected source frame count')
        frames = np.frombuffer(raw, dtype=np.uint8).reshape(-1, height, width, 3)
        # Same uniform integer temporal sampling and bilinear short-side resize
        # as the author's detection tutorial, without importing obsolete transforms.
        indices = torch.linspace(0, len(frames)-1, 32).long().numpy()
        selected = frames[indices].copy()
        canvas = Image.new('RGB', (4*320, 2*204), '#141414')
        draw = ImageDraw.Draw(canvas)
        for cell, index in enumerate(np.linspace(0, len(frames)-1, 8).astype(int)):
            image = Image.fromarray(frames[index]); image.thumbnail((320, 180))
            x, y = (cell % 4)*320, (cell//4)*204
            canvas.paste(image, (x, y)); draw.text((x+4, y+182), f"{case['id']} source frame {index}", fill='white')
        canvas.save(args.output / (case['id'] + '.jpg'), quality=90)
        clip = torch.from_numpy(selected).permute(3, 0, 1, 2).float()/255
        if width < height:
            new_width, new_height = 256, int(math.floor(height/width*256))
            scale = new_height/height
        else:
            new_height, new_width = 256, int(math.floor(width/height*256))
            scale = new_width/width
        clip = functional.interpolate(clip, size=(new_height, new_width), mode='bilinear', align_corners=False)
        clip = (clip-.45)/.225
        box *= scale
        box[[0, 2]] = np.clip(box[[0, 2]], 0, new_width-1)
        box[[1, 3]] = np.clip(box[[1, 3]], 0, new_height-1)
        boxes = torch.from_numpy(np.concatenate(([0.], box)).astype(np.float32)).unsqueeze(0)
        row = {'id': case['id'], 'source': case['source'], 'start': case['start'], 'end': case['end'],
               'sourceFrames': len(frames), 'midpointBox': box.tolist(), 'reviewLabel': case.get('label'),
               'variants': {}}
        held = clip[:, 16:17].expand(-1, 32, -1, -1).contiguous()
        camera_controls = []
        for kind in ['zoom', 'pan']:
            theta = torch.zeros(32, 2, 3)
            phase = (torch.arange(32)-16)/31
            if kind == 'zoom':
                theta[:, 0, 0] = theta[:, 1, 1] = 1/(1+.3*phase)
            else:
                theta[:, 0, 0] = theta[:, 1, 1] = 1
                theta[:, 0, 2] = .25*phase
            batch = held.permute(1, 0, 2, 3)
            grid = functional.affine_grid(theta, batch.shape, align_corners=False)
            transformed = functional.grid_sample(batch, grid, mode='bilinear', padding_mode='border', align_corners=False)
            camera_controls.append(('repeat_camera_'+kind, transformed.permute(1, 0, 2, 3).contiguous()))
        for name, fast in [('forward', clip), ('reverse', clip.flip(1)),
                           ('repeat_midpoint', held)] + camera_controls:
            slow = fast.index_select(1, torch.linspace(0, 31, 8).long())
            with torch.inference_mode():
                prediction = model([slow.unsqueeze(0), fast.unsqueeze(0)], boxes)[0].numpy()
            if prediction.shape != (80,) or not np.isfinite(prediction).all():
                raise ValueError('Malformed model output')
            ranked = np.argsort(prediction)[::-1][:8]
            row['variants'][name] = {'walk': float(prediction[13]),
                'top': [{'id': int(i)+1, 'label': labels.get(int(i)+1, 'unlisted AVA class'), 'score': float(prediction[i])} for i in ranked],
                'scores': prediction.tolist()}
            print(case['id'], name, 'walk', round(float(prediction[13]), 4), flush=True)
        row['elapsedSeconds'] = time.perf_counter()-clock
        report['rows'].append(row)
        (args.output/'report.json').write_text(json.dumps(report, indent=2), encoding='utf-8')


if __name__ == '__main__':
    main()
