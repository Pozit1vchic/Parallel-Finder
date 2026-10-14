"""Replay visually checked development counterexamples, not a holdout score."""
import argparse
import json
from pathlib import Path

# Full-resolution source frames were reviewed before writing this audit.
# All three visibly show the character's back and a hood over his head.
CHECKS = [
    {'time': 47.4, 'view': 'a person seen from behind',
     'hood': 'a person wearing a hood over their head'},
    {'time': 47.8, 'view': 'a person seen from behind',
     'hood': 'a person wearing a hood over their head'},
    {'time': 48.2, 'view': 'a person seen from behind',
     'hood': 'a person wearing a hood over their head'},
]

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('input', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    report = json.loads(args.input.read_text(encoding='utf-8'))
    rows = []
    for expected in CHECKS:
        found = [r for r in report['results'] if 'edit-reference' in r['folder']
                 and abs(r['time']-expected['time']) < 1e-6]
        if len(found) != 1:
            raise ValueError('missing/duplicate reviewed frame')
        row = found[0]
        for facet in ('view', 'hood'):
            actual = row['facets'][facet]
            rows.append({'time': expected['time'], 'facet': facet,
                         'expected': expected[facet], 'predicted': actual['top'],
                         'correct': actual['top'] == expected[facet],
                         'margin': actual['margin']})
    result = {'developmentCounterexamplesOnly': True, 'notIndependentAccuracy': True,
              'checks': len(rows), 'correct': sum(r['correct'] for r in rows),
              'productionAdmissionAllowed': False, 'results': rows}
    with args.output.open('x', encoding='utf-8') as out:
        json.dump(result, out, ensure_ascii=False, indent=2)
    print(json.dumps({k:v for k,v in result.items() if k != 'results'}))

if __name__ == '__main__':
    main()
