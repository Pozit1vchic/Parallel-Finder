"""Research-only image/text similarity audit; no product classification.

The input controls contain timestamps and pose-detector boxes, not image data.
The model files, source video and output remain local. Scores are cosine
similarities, not calibrated probabilities or a reliable picture detector.
"""
import argparse
import io
import json
import subprocess
import time
from pathlib import Path

import numpy as np
import onnxruntime as ort
from PIL import Image
from tokenizers import Tokenizer

PROMPTS = [
    'a real person in a video game scene',
    'a living person standing in a room',
    'a close-up of a human character in a video game',
    'a framed painting of a person on a wall',
    'a printed portrait of a person in a picture frame',
    'a person depicted in a poster on a wall',
    'a human face in a painting',
    'a person standing in front of a painting',
]
MEAN = np.array([.48145466, .4578275, .40821073], np.float32)
STD = np.array([.26862954, .26130258, .27577711], np.float32)


def crop_input(frame, box, factor):
    x1, y1, x2, y2 = box
    if x2 <= x1 or y2 <= y1:
        raise ValueError('Invalid detection box')
    cx, cy = (x1 + x2) / 2, (y1 + y2) / 2
    width, height = x2 - x1, y2 - y1
    crop = frame.crop((max(0, int(cx - factor * width / 2)),
        max(0, int(cy - factor * height / 2)), min(frame.width, int(cx + factor * width / 2)),
        min(frame.height, int(cy + factor * height / 2))))
    scale = 224 / min(crop.size)
    crop = crop.resize((round(crop.width * scale), round(crop.height * scale)), Image.Resampling.BICUBIC)
    left, top = (crop.width - 224) // 2, (crop.height - 224) // 2
    crop = crop.crop((left, top, left + 224, top + 224))
    tensor = (np.asarray(crop, np.float32) / 255.0 - MEAN) / STD
    return np.transpose(tensor, (2, 0, 1))[None]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--model-dir', type=Path, required=True, help='Directory containing text.onnx, vision.onnx, tokenizer.json')
    parser.add_argument('--video', type=Path, required=True)
    parser.add_argument('--controls', type=Path, required=True)
    parser.add_argument('--ffmpeg', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--scales', type=float, nargs='+', default=[1.0, 1.5, 2.0])
    args = parser.parse_args()
    if args.output.exists():
        parser.error('Output must be a new file')
    if any(not np.isfinite(scale) or scale <= 0 for scale in args.scales):
        parser.error('Scale must be positive')
    options = ort.SessionOptions()
    options.intra_op_num_threads, options.inter_op_num_threads = 4, 1
    text = ort.InferenceSession(str(args.model_dir / 'text.onnx'), sess_options=options, providers=['CPUExecutionProvider'])
    vision = ort.InferenceSession(str(args.model_dir / 'vision.onnx'), sess_options=options, providers=['CPUExecutionProvider'])
    tokenizer = Tokenizer.from_file(str(args.model_dir / 'tokenizer.json'))
    tokenizer.enable_truncation(max_length=77)
    tokenizer.enable_padding(length=77, pad_id=49407, pad_token='<|endoftext|>')
    encoded = tokenizer.encode_batch(PROMPTS)
    ids = np.array([entry.ids for entry in encoded], np.int64)
    mask = np.array([entry.attention_mask for entry in encoded], np.int64)
    # This particular ONNX export declares dynamic batch, but fails above one.
    vectors = np.concatenate([text.run(None, {'input_ids': ids[i:i+1], 'attention_mask': mask[i:i+1]})[0]
                              for i in range(len(PROMPTS))])
    vectors /= np.linalg.norm(vectors, axis=1, keepdims=True)
    controls = json.loads(args.controls.read_text(encoding='utf-8-sig'))
    width, height = controls['frameWidth'], controls['frameHeight']
    results, started = [], time.monotonic()
    for control in controls['controls']:
        raw = subprocess.check_output([str(args.ffmpeg), '-loglevel', 'error', '-ss', str(control['time']),
            '-i', str(args.video), '-frames:v', '1', '-vf', f'scale={width}:{height}',
            '-f', 'image2pipe', '-vcodec', 'png', 'pipe:1'])
        frame = Image.open(io.BytesIO(raw)).convert('RGB')
        for scale in args.scales:
            image_vector = vision.run(None, {'pixel_values': crop_input(frame, control['box'], scale)})[0]
            image_vector /= np.linalg.norm(image_vector, axis=1, keepdims=True)
            scores = (image_vector @ vectors.T)[0]
            results.append({'id': control['id'], 'expected': control['expected'], 'factor': scale,
                            'scores': [round(float(score), 5) for score in scores]})
    args.output.write_text(json.dumps({'prompts': PROMPTS, 'results': results,
        'elapsedSeconds': time.monotonic() - started}, indent=2), encoding='utf-8')
    print(f'Evaluated {len(controls["controls"])} detections at {len(args.scales)} scales')


if __name__ == '__main__':
    main()
