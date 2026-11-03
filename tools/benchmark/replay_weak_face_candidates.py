"""Replay optional weak face observations; output is diagnostic, not ground truth.

Input is a local PF_DEBUG_TRACKS_JSON capture made with PF_AUDIT_WEAK_FACES=1.
Only weak observations remaining after the existing three-sample audit are
considered. The resulting file contains face vectors and must stay local.
"""
import argparse
import json
import math
from pathlib import Path


def cosine(left, right):
    if not left or len(left) != len(right):
        return -1.0
    dot = sum(a * b for a, b in zip(left, right))
    norm_left = sum(value * value for value in left)
    norm_right = sum(value * value for value in right)
    return dot / math.sqrt(norm_left * norm_right) if min(norm_left, norm_right) > 1e-12 else -1.0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--threshold', type=float, required=True)
    parser.add_argument('--minimum-samples', type=int, default=2)
    args = parser.parse_args()
    if args.source.resolve() == args.output.resolve() or args.output.exists():
        parser.error('Output must be a new file, separate from the input')
    if not 0 <= args.threshold <= 1 or args.minimum_samples < 2:
        parser.error('Invalid threshold or sample count')
    tracks = json.loads(args.source.read_text(encoding='utf-8-sig'))
    changed, total = [], 0
    for track in tracks['tracks']:
        observations = track['observations']
        weak = []
        for observation in observations:
            if observation.get('weakFace') and (not weak or observation['time'] - weak[-1]['time'] >= .15 - 1e-9):
                weak.append(observation)
        if len(weak) < args.minimum_samples or weak[-1]['time'] - weak[0]['time'] < .15 * (args.minimum_samples - 1) - 1e-9:
            continue
        if any(cosine(a['weakFace'], b['weakFace']) < args.threshold for i, a in enumerate(weak) for b in weak[i+1:]):
            continue
        if any(cosine(a['weakFace'], b['face']) < .5 for a in weak for b in observations if b['face']):
            continue
        for observation in weak:
            observation['face'] = observation['weakFace']
            total += 1
        changed.append(track['id'])
    args.output.write_text(json.dumps(tracks, separators=(',', ':')), encoding='utf-8')
    print(json.dumps({'promotedObservations': total, 'tracks': changed}))


if __name__ == '__main__':
    main()
