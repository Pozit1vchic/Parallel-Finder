"""Development-only video action and reversal audit, without app integration.

Fixed prompts and reviewed intervals are compared both forward and backward.
Softmax is relative to these prompts, not calibrated correctness probability.
"""
import argparse
import hashlib
import json
import time
from pathlib import Path
import cv2
import numpy as np
import torch
from transformers import XCLIPModel, XCLIPProcessor


PROMPTS = {
    'hood-on': 'A person puts a hood onto their head.',
    'hood-off': 'A person takes a hood off their head.',
    'hands-behind-head': 'A person moves both hands behind their head.',
    'walking': 'A person is walking.',
    'head-turn': 'A person turns their head.',
    'body-turn': 'A person turns their body.',
    'standing': 'A person stands still.',
    'wearing-hood': 'A person wears a hood without moving.',
    'phone': 'A person talks on a phone.',
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--video', type=Path, required=True)
    parser.add_argument('--reference', type=Path, required=True)
    parser.add_argument('--model', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--device', choices=['cpu', 'cuda', 'auto'], default='auto')
    args = parser.parse_args()
    if args.output.exists():
        raise FileExistsError(args.output)
    device = 'cpu'
    if args.device == 'cuda':
        device = 'cuda'
    elif args.device == 'auto' and torch.cuda.is_available():
        free, _ = torch.cuda.mem_get_info()
        if free >= 3 * 1024**3:
            device = 'cuda'
    torch.set_num_threads(4)
    model = XCLIPModel.from_pretrained(args.model, local_files_only=True,
                                      use_safetensors=True).eval().to(device)
    processor = XCLIPProcessor.from_pretrained(args.model, local_files_only=True)
    reference = json.loads(args.reference.read_text(encoding='utf-8-sig'))
    capture = cv2.VideoCapture(str(args.video))
    fps = capture.get(cv2.CAP_PROP_FPS)
    frames = []
    while True:
        ok, frame = capture.read()
        if not ok:
            break
        frames.append(cv2.cvtColor(frame, cv2.COLOR_BGR2RGB))
        if len(frames) > 2000:
            raise ValueError('Development audit requires a short clip')
    capture.release()
    if not frames or fps <= 0:
        raise ValueError('Source decode failed')
    rows = []
    labels = list(PROMPTS)
    for interval in reference['intervals']:
        start, end = interval['start'], interval['end']
        times = np.linspace(start, end, model.config.vision_config.num_frames,
                            endpoint=False)
        indices = [min(len(frames)-1, int(t*fps)) for t in times]
        sequence = [frames[index] for index in indices]
        row = dict(id=interval['id'], indices=indices, times=[index/fps for index in indices])
        for direction, images in [('forward', sequence), ('reversed', sequence[::-1])]:
            inputs = processor(text=list(PROMPTS.values()), images=images,
                               return_tensors='pt', padding=True)
            if 'pixel_values' not in inputs:
                raise ValueError('Processor did not prepare the video frames')
            if inputs['pixel_values'].ndim == 4:
                inputs['pixel_values'] = inputs['pixel_values'].unsqueeze(0)
            inputs = {key: value.to(device) for key, value in inputs.items()}
            began = time.perf_counter()
            with torch.inference_mode():
                logits = model(**inputs).logits_per_video[0].float().cpu()
            scores = logits.softmax(dim=-1).tolist()
            order = sorted(range(len(labels)), key=lambda i: -scores[i])
            row[direction] = dict(elapsedSeconds=time.perf_counter()-began,
                                  top=labels[order[0]],
                                  scores={label: scores[i] for i, label in enumerate(labels)})
        print(interval['id'], row['forward']['top'], row['reversed']['top'], flush=True)
        rows.append(row)
    model_file = args.model/'model.safetensors'
    with args.output.open('x', encoding='utf-8') as destination:
        json.dump(dict(scope='development action/reversal audit; not held-out accuracy',
                       device=device, modelSha256=hashlib.file_digest(model_file.open('rb'), 'sha256').hexdigest(),
                       prompts=PROMPTS, relativeScoresOnly=True, intervals=rows), destination, indent=2)


if __name__ == '__main__':
    main()
