"""Evaluate sparse visual references; unmatched/unlabelled output is not negative truth."""
import argparse
import json
import re
from pathlib import Path


def source_path(value, root):
    path = Path(value).resolve()
    if not path.is_relative_to(root) or not path.is_file():
        raise ValueError(f'Real evaluation source must exist below {root}: {value}')
    return str(path).replace('\\', '/').casefold()


def evaluate(reference, sources, results, windows, root):
    names = {key: source_path(root / name, root) for key, name in reference['sources'].items()}
    supplied = {source_path(path, root) for path in sources}
    tolerance = reference['policy']['toleranceSeconds']
    def inside(source, timestamp, interval):
        return source_path(source, root) == names[interval[0]] and interval[1]-tolerance <= timestamp <= interval[2]+tolerance
    # Exact representative timestamps, not broad playback/export intervals.
    def hit(pair, case):
        return (inside(pair['leftSource'], pair['leftStart'], case['a'])
                and inside(pair['rightSource'], pair['rightStart'], case['b'])) or (
            inside(pair['leftSource'], pair['leftStart'], case['b'])
                and inside(pair['rightSource'], pair['rightStart'], case['a']))
    rows = []
    for case in reference['cases']:
        available = all(names[side[0]] in supplied for side in (case['a'], case['b']))
        matching = [i for i, pair in enumerate(results) if hit(pair, case)] if available else []
        coverage = []
        for side in (case['a'], case['b']):
            coverage.append([i for i,w in enumerate(windows) if any(inside(w['source'], f['time'], side) for f in w['frames'])])
        rows.append(dict(id=case['id'], label=case['label'], type=case['type'], evaluated=available,
                         policyExcluded=case.get('policyExcluded',False),
                         supportLimited=case.get('supportLimited',False), scope=case.get('scope','same-person'),
                         matchingResultIndices=matching, matched=bool(matching),
                         observedWindowCoverage=coverage))
    return dict(caseResults=rows, returnedPairs=len(results),
                knownPositiveHits=sum(r['label']=='positive' and r['evaluated'] and r['matched'] for r in rows),
                evaluatedKnownPositives=sum(r['label']=='positive' and r['evaluated'] for r in rows),
                knownNegativeHits=sum(r['label']=='negative' and r['evaluated'] and r['matched'] for r in rows),
                missingSourceCases=[r['id'] for r in rows if not r['evaluated']],
                limitations='Sparse reference coverage is not full recall or precision. All policy/support/scope exclusions remain visible. Window overlap is observation coverage, not ANN candidate generation or acceptance. Scores are not probabilities.')


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--reference',type=Path,default=Path(__file__).with_name('matcher-quality-v1.json'))
    parser.add_argument('--dataset-root',type=Path,default=Path('D:/for_tests_pf'))
    parser.add_argument('--source',type=Path,action='append',required=True)
    parser.add_argument('--report',type=Path,required=True)
    parser.add_argument('--windows',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    # Windows console status can use the local code page while the explicit
    # QJson payload is UTF-8. Decode only the structured payload in that case.
    content=args.report.read_bytes()
    try:
        report=json.loads(content)
        results=report['results']
    except (json.JSONDecodeError, UnicodeDecodeError):
        match=re.search(rb'^\s*results_json\s*:\s*(.*)$',content,re.M)
        if match is None: raise ValueError('No structured matcher results')
        results=json.loads(match[1])
    output=evaluate(json.loads(args.reference.read_text(encoding='utf-8')),
                    args.source,results,json.loads(args.windows.read_text(encoding='utf-8')),
                    args.dataset_root.resolve())
    args.output.write_text(json.dumps(output,indent=2,ensure_ascii=False),encoding='utf-8')
    print(json.dumps({key:value for key,value in output.items() if key!='caseResults'}),flush=True)


if __name__=='__main__': main()
