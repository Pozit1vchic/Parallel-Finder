"""Local X-CLIP development replay on previously sampled edit frames."""
import argparse
import json
import time
from pathlib import Path

import numpy as np
import torch
from PIL import Image
from transformers import XCLIPModel, XCLIPProcessor

PROMPTS = [
    'a person putting on the hood of a hoodie',
    'a person taking off the hood of a hoodie',
    'a person adjusting a hood with their hands',
    'a person raising their hands behind their head',
    'a person lowering their arms',
    'a person turning their head',
    'a person turning their body',
    'a person walking away from the camera',
    'a person walking toward the camera',
    'a person talking on a phone',
    'a person looking at a phone',
    'a person standing still',
    'a close-up of a person looking at another person',
    'a person touching another person',
    'a person smiling',
]
REGIONS = [(37.8,39.0),(41.4,42.0),(42.8,43.4),(43.6,45.0),
           (45.2,46.4),(47.4,48.4),(48.6,50.2),(51.2,54.0)]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--model', type=Path, required=True)
    parser.add_argument('--frames', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--regions', type=Path)
    args = parser.parse_args()
    if args.output.exists():
        raise FileExistsError(args.output)
    files = sorted(args.frames.glob('*.jpg'))
    if not files:
        files = sorted(args.frames.glob('*.png'))
    if len(files) != 289:
        raise ValueError('Expected the reviewed 289 frames sampled at 5 fps')
    torch.set_num_threads(8)
    processor = XCLIPProcessor.from_pretrained(args.model, local_files_only=True)
    model = XCLIPModel.from_pretrained(args.model, local_files_only=True).eval()
    rows = []
    regions = json.loads(args.regions.read_text(encoding='utf-8-sig')) if args.regions else REGIONS
    for start,end in regions:
        indices = np.rint(np.linspace(start*5,end*5,8)).astype(int)
        images = [np.asarray(Image.open(files[i]).convert('RGB')) for i in indices]
        inputs = processor(text=PROMPTS, images=images, return_tensors='pt', padding=True)
        begin = time.perf_counter()
        with torch.inference_mode():
            logits = model(**inputs).logits_per_video[0].tolist()
        ranking = sorted(zip(PROMPTS,logits),key=lambda item:item[1],reverse=True)
        reversed_inputs = dict(inputs)
        reversed_inputs['pixel_values'] = inputs['pixel_values'].flip(1)
        static_inputs = dict(inputs)
        static_inputs['pixel_values'] = inputs['pixel_values'][:, :1].repeat(1,8,1,1,1)
        with torch.inference_mode():
            reversed_logits = model(**reversed_inputs).logits_per_video[0].tolist()
            static_logits = model(**static_inputs).logits_per_video[0].tolist()
        rows.append(dict(start=start,end=end,sampledFrameIndices=indices.tolist(),
                         uniqueFrames=len(set(indices.tolist())),
                         reverseLogits=dict(zip(PROMPTS,reversed_logits)),
                         staticLogits=dict(zip(PROMPTS,static_logits)),
                         logits=[dict(prompt=p,logit=s) for p,s in ranking],
                         inferenceSeconds=time.perf_counter()-begin))
        print(f'Completed region {start}-{end}: {ranking[0][0]}',flush=True)
    with args.output.open('x',encoding='utf-8') as output:
        json.dump(dict(scope='Development regions may span edit cuts; logits are not calibrated confidence',
                       modelRevision='a2e27a78a2b5d802e894b8a1ef14f3a8ce490963',
                       prompts=PROMPTS,regions=rows),output,indent=2)


if __name__ == '__main__':
    main()
