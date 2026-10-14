"""Report observed support for rejected tracks without changing admission.

This reproduces summary averaging for diagnostics. Scores are model evidence,
not ground truth. No identity may be inferred from an action label.
"""
import argparse
import json
import math
from pathlib import Path


def prototype(observations, kind):
    samples = [o[kind] for o in observations if o[kind]
               and all(math.isfinite(v) for v in o[kind])]
    if not samples:
        return [], 0
    dimension = len(samples[0])
    samples = [s for s in samples if len(s) == dimension]
    return [sum(s[i] for s in samples) for i in range(dimension)], len(samples)


def cosine(left, right):
    if not left or len(left) != len(right):
        return None
    norms = sum(v*v for v in left) * sum(v*v for v in right)
    if norms <= 1e-24:
        return None
    return max(-1., min(1., sum(a*b for a, b in zip(left, right))/math.sqrt(norms)))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--tracks', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    snapshot = json.loads(args.tracks.read_text(encoding='utf-8-sig'))
    tracks = snapshot['tracks']
    prepared = {t['id']: {k: prototype(t['observations'], k)
                         for k in ('face', 'body')} for t in tracks}
    anchors = [t for t in tracks if t['sourceLead']
               and prepared[t['id']]['face'][1] >= 2]
    rows = []
    for track in tracks:
        evidence = prepared[track['id']]
        comparisons = []
        for anchor in anchors:
            other = prepared[anchor['id']]
            face = cosine(evidence['face'][0], other['face'][0])
            body = cosine(evidence['body'][0], other['body'][0])
            times = {o['time'] for o in track['observations']}
            co_visible = any(o['time'] in times for o in anchor['observations'])
            comparisons.append(dict(anchor=anchor['id'], face=face, body=body,
                                    coVisible=co_visible))
        votes = sum(c['face'] is not None and c['face'] >= .363
                    for c in comparisons)
        body_support = any(c['face'] is not None and c['face'] >= .363
                           and c['body'] is not None and c['body'] >= .76
                           for c in comparisons)
        observations = []
        for observation in track['observations']:
            if not observation['face']:
                continue
            scores = [cosine(observation['face'], prepared[a['id']]['face'][0])
                      for a in anchors]
            observations.append(dict(time=observation['time'],
                                     faceVotes=sum(s is not None and s >= .363 for s in scores)))
        rows.append(dict(track=track['id'], selected=track['sourceLead'],
                         start=track['observations'][0]['time'],
                         end=track['observations'][-1]['time'],
                         faceSamples=evidence['face'][1],
                         bodySamples=evidence['body'][1], faceVotes=votes,
                         anchorCount=len(anchors), bodySupport=body_support,
                         comparisons=comparisons, faceObservations=observations))
    with args.output.open('x', encoding='utf-8') as destination:
        json.dump(dict(scope='diagnostic support; not full admission replay or identity truth',
                       source=snapshot['source'], tracks=rows), destination, indent=2)


if __name__ == '__main__':
    main()
