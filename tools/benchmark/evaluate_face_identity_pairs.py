"""Evaluate fixed, manually reviewed identity labels on face-probe outputs.

Report individual pairs and misses; a small development sample does not prove
general recognition accuracy. Labels must be supplied before scoring.
"""
import argparse
import json
import math
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--probe', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    samples = json.loads(args.probe.read_text(encoding='utf-8-sig'))
    pairs = []
    for index, left in enumerate(samples):
        for right in samples[index+1:]:
            same = left['identity'] == right['identity']
            a, b = left['face'], right['face']
            cosine = None
            if a and len(a) == len(b):
                norm = sum(v*v for v in a) * sum(v*v for v in b)
                if norm > 1e-24 and all(math.isfinite(v) for v in a+b):
                    cosine = max(-1., min(1., sum(x*y for x, y in zip(a, b))/math.sqrt(norm)))
            pairs.append(dict(a=left['id'], b=right['id'], samePerson=same, cosine=cosine))
    metrics = []
    for threshold in (.363, .70):
        accepted = lambda p: p['cosine'] is not None and p['cosine'] >= threshold
        metrics.append(dict(threshold=threshold,
                            positives=sum(p['samePerson'] for p in pairs),
                            negatives=sum(not p['samePerson'] for p in pairs),
                            truePositives=sum(p['samePerson'] and accepted(p) for p in pairs),
                            falsePositives=sum(not p['samePerson'] and accepted(p) for p in pairs),
                            missedPositives=sum(p['samePerson'] and not accepted(p) for p in pairs)))
    with args.output.open('x', encoding='utf-8') as destination:
        json.dump(dict(scope='small reviewed development/validation sample; not professional accuracy',
                       source=str(args.probe.resolve()), pairs=pairs, metrics=metrics), destination, indent=2)


if __name__ == '__main__':
    main()
