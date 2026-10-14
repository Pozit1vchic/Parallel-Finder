"""Read-only, development-only visual semantics experiment.

This is not an action classifier or an identity gate. Independent facet
similarities are recorded, never presented as calibrated probabilities.
Only model weights are downloaded; source video/images stay on this host.
The user's analysis cache and application settings are never accessed.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import time
import urllib.request

FACETS = {
    'hood': ['a person wearing a hood over their head',
             'a person wearing a hoodie with the hood down',
             'a person wearing a hat or baseball cap',
             'a person without a hood or a hat'],
    'hands': ['a person pulling a hood over their head with both hands',
              'a person taking a hood off their head with both hands',
              'a person with both hands behind their head',
              'a person with their hands away from their head'],
    'phone': ['a person holding a phone to their ear',
              'a person looking at a phone in their hand',
              'a person touching their face without a phone',
              'a person with no phone visible'],
    'view': ['a person seen from the front', 'a person seen in three-quarter view',
             'a person seen in side profile', 'a person seen from behind'],
    'framing': ['a close-up shot of a face', 'a medium shot of a person from the waist up',
                'a full-body shot of a person'],
    'posture': ['a person standing', 'a person sitting', 'a person lying down'],
    'expression': ['a person smiling', 'a person with a neutral facial expression',
                   'a person crying'],
}
WEIGHT_SHA = '5806e77cd80f8b59890b7e101eabd078d9fb84e6937f9e85e4ecb61988df416f'
WEIGHT_URL = f'https://openaipublic.azureedge.net/clip/models/{WEIGHT_SHA}/ViT-B-16.pt'


def digest(path):
    h = hashlib.sha256()
    with path.open('rb') as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b''):
            h.update(chunk)
    return h.hexdigest()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--root', type=Path, required=True)
    parser.add_argument('--frames', type=Path, action='append', required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--threads', type=int, default=8)
    args = parser.parse_args()
    if args.output.exists():
        raise FileExistsError(args.output)
    if not 1 <= args.threads <= 24:
        raise ValueError('invalid CPU thread count')
    source = args.root / 'openai-clip'
    commit = subprocess.check_output(['git', '-C', str(source), 'rev-parse', 'HEAD'], text=True).strip()
    target = args.root / 'ViT-B-16.pt'
    if not target.exists():
        partial = args.root / 'ViT-B-16.pt.part'
        with urllib.request.urlopen(WEIGHT_URL, timeout=60) as response, partial.open('xb') as out:
            size = 0
            for chunk in iter(lambda: response.read(1024 * 1024), b''):
                out.write(chunk)
                size += len(chunk)
                if size % (32 * 1024 * 1024) == 0:
                    print(f'downloaded {size // (1024 * 1024)} MiB', flush=True)
        if digest(partial) != WEIGHT_SHA:
            raise ValueError('official model digest mismatch')
        partial.rename(target)
    if digest(target) != WEIGHT_SHA:
        raise ValueError('model digest mismatch')
    sys.path.insert(0, str(source))
    import clip
    import torch
    from PIL import Image
    torch.set_num_threads(args.threads)
    model, prepare = clip.load(str(target), device='cpu', jit=False)
    prompts = [text for values in FACETS.values() for text in values]
    started = time.perf_counter()
    rows = []
    with torch.inference_mode():
        texts = model.encode_text(clip.tokenize(prompts)).float()
        texts /= texts.norm(dim=-1, keepdim=True)
        for folder in args.frames:
            fps = 5 if folder.parent.name.startswith('edit-reference') else 2
            paths = sorted(folder.glob('*.jpg'))
            # The author confirmed edit-part [36,54]; do not label the intro.
            if fps == 5:
                paths = [p for p in paths if 36 <= (int(p.stem)-1)/fps <= 54]
            for begin in range(0, len(paths), 8):
                batch = paths[begin:begin+8]
                images = []
                kept = []
                for path in batch:
                    with Image.open(path) as image:
                        # Omit title cards based only on background pixels.
                        tiny = image.convert('L').resize((32,32))
                        if sum(v < 20 for v in tiny.getdata()) / 1024 > .95:
                            continue
                        images.append(prepare(image.convert('RGB')))
                        kept.append(path)
                if not images:
                    continue
                vectors = model.encode_image(torch.stack(images)).float()
                vectors /= vectors.norm(dim=-1, keepdim=True)
                scores = (vectors @ texts.T).tolist()
                for path, values in zip(kept, scores):
                    facets = {}
                    offset = 0
                    for name, descriptions in FACETS.items():
                        data = values[offset:offset+len(descriptions)]
                        order = sorted(range(len(data)), key=lambda i: -data[i])
                        facets[name] = {'top': descriptions[order[0]],
                                        'margin': data[order[0]]-data[order[1]], 'cosines': data}
                        offset += len(descriptions)
                    rows.append({'folder': str(folder), 'time': (int(path.stem)-1)/fps,
                                 'facets': facets})
                if begin % 80 == 0:
                    print(f'{folder.parent.name}: {begin+len(batch)}/{len(paths)}', flush=True)
    report = {'researchOnly': True, 'notCalibratedProbabilities': True,
              'clipCommit': commit, 'modelSha256': WEIGHT_SHA, 'prompts': FACETS,
              'frames': len(rows), 'seconds': time.perf_counter()-started, 'results': rows}
    with args.output.open('x', encoding='utf-8') as out:
        json.dump(report, out, ensure_ascii=False, indent=2)
    print(json.dumps({'frames': len(rows), 'seconds': report['seconds']}), flush=True)


if __name__ == '__main__':
    main()
