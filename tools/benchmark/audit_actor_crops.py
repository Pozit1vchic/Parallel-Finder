"""Compare image facets on reviewed actor crops and whole frames; no app integration."""
import argparse
import json
import sys
from pathlib import Path
from review_scene_semantics import FACETS, WEIGHT_SHA, digest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, required=True)
    parser.add_argument('--manifest', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(exist_ok=False)
    weight = args.root/'ViT-B-16.pt'
    if digest(weight) != WEIGHT_SHA:
        raise ValueError('Model fingerprint changed')
    sys.path.insert(0,str(args.root/'openai-clip'))
    import clip
    import torch
    from PIL import Image
    torch.set_num_threads(8)
    model, prepare = clip.load(str(weight), device='cpu', jit=False)
    cases = json.loads(args.manifest.read_text(encoding='utf-8'))
    prompts = [description for descriptions in FACETS.values() for description in descriptions]
    images = []
    for index, case in enumerate(cases):
        with Image.open(case['frame']) as opened:
            frame = opened.convert('RGB')
            left,top,right,bottom = case['roi']
            if not 0<=left<right<=frame.width or not 0<=top<bottom<=frame.height:
                raise ValueError('Actor ROI outside the actual frame')
            actor = frame.crop((left,top,right,bottom))
            actor.save(args.output/f'actor-{index}.png')
            images.extend((prepare(frame),prepare(actor)))
    with torch.inference_mode():
        text = model.encode_text(clip.tokenize(prompts)).float()
        text /= text.norm(dim=-1,keepdim=True)
        vectors = model.encode_image(torch.stack(images)).float()
        vectors /= vectors.norm(dim=-1,keepdim=True)
        scores = (vectors@text.T).tolist()
    rows = []
    for index, case in enumerate(cases):
        row = dict(case)
        for mode, values in zip(('whole','actor'),scores[index*2:index*2+2]):
            facets = {}
            offset = 0
            for name, descriptions in FACETS.items():
                values_for_facet = values[offset:offset+len(descriptions)]
                ordered = sorted(range(len(descriptions)),key=lambda i:-values_for_facet[i])
                facets[name] = dict(top=descriptions[ordered[0]],cosines=values_for_facet,
                    margin=values_for_facet[ordered[0]]-values_for_facet[ordered[1]])
                offset += len(descriptions)
            row[mode] = facets
        rows.append(row)
    with (args.output/'report.json').open('x',encoding='utf-8') as destination:
        json.dump(dict(scope='Manually chosen development ROIs; relative similarities, not calibrated accuracy',
                       modelSha256=WEIGHT_SHA,prompts=FACETS,cases=rows),destination,indent=2)


if __name__ == '__main__':
    main()
