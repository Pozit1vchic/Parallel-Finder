"""Research-only weak-face replay; it does not distinguish pictures from people.

The output contains biometric vectors and must stay local. The greedy consensus
is intentionally diagnostic, not an identity guarantee or production policy.
"""
import argparse
import json
import math
from pathlib import Path


def cosine(a, b):
    if not a or len(a) != len(b):
        return -1.0
    norm = math.sqrt(sum(x*x for x in a) * sum(x*x for x in b))
    return sum(x*y for x, y in zip(a, b)) / norm if norm else -1.0


def replay(snapshot):
    changed = []
    for track in snapshot['tracks']:
        observations = track['observations']
        weak = []
        for item in observations:
            if item.get('weakFace') and (not weak or item['time'] - weak[-1]['time'] >= .15 - 1e-9):
                weak.append(item)
        if len(weak) < 4:
            continue
        eligible = [item for item in weak if all(
            cosine(item['weakFace'], other['face']) >= .5
            for other in observations if other['face'])]
        best = []
        for seed in eligible:
            cohort = [seed]
            for item in eligible:
                if item is not seed and all(cosine(item['weakFace'], other['weakFace']) >= .7 for other in cohort):
                    cohort.append(item)
            if len(cohort) > len(best):
                best = cohort
        if (len(best) < 3 or len(best) * 4 < len(weak) * 3
                or max(item['time'] for item in best) - min(item['time'] for item in best) < .3 - 1e-9):
            continue
        for item in best:
            item['face'] = item['weakFace']
        changed.append(dict(track=track['id'], weak=len(weak), promoted=len(best)))
    return changed


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    if args.source.resolve() == args.output.resolve() or args.output.exists():
        parser.error('Output must be a new file, separate from the input')
    snapshot = json.loads(args.source.read_text(encoding='utf-8-sig'))
    changed = replay(snapshot)
    args.output.write_text(json.dumps(snapshot, separators=(',', ':')), encoding='utf-8')
    print(json.dumps({'scope': 'experimental; portraits and false matches remain possible', 'tracks': changed}))


if __name__ == '__main__':
    main()
