"""Compare real captures: exact observations, reviewed identity and shot reuse.

Uses source timestamp + actual box, never old track IDs, to remap identity
labels after reassociation. Counts are sparse checks, not overall accuracy.
"""
import argparse
from collections import Counter
import json
from pathlib import Path


def load(path):
    return json.loads(path.read_text(encoding='utf-8-sig'))


def flatten(snapshot):
    observations = {}
    raw = Counter()
    for track in snapshot['tracks']:
        for i, observation in enumerate(track['observations']):
            key = (observation['time'], tuple(observation['box']))
            if key in observations:
                raise ValueError('Duplicate timestamp/box cannot be remapped uniquely')
            runs = track.get('allowedRuns', [[0,len(track['observations'])]])
            admitted = track['sourceLead'] and any(a <= i < b for a,b in runs)
            observations[key] = dict(observation=observation, admitted=bool(admitted),track=track['id'])
            raw[json.dumps(observation, sort_keys=True)] += 1
    return observations,raw


def pair_metrics(results):
    uses = Counter()
    bodyShots = set()
    for pair in results:
        for side in ('left','right'):
            shot = (pair[side+'Source'], pair[side+'SceneStart'])
            uses[shot] += 1
            if not pair['headOnlyComparison']: bodyShots.add(shot)
    return dict(results=len(results), headOnly=sum(p['headOnlyComparison'] for p in results),
        bodyOrMotion=sum(not p['headOnlyComparison'] for p in results),
        uniqueShots=len(uses), uniqueBodyOrMotionShots=len(bodyShots),
        maximumShotUses=max(uses.values(),default=0),
        reusedShots=sum(n>1 for n in uses.values()))


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--before',type=Path,required=True)
    p.add_argument('--after',type=Path,required=True)
    p.add_argument('--reference',type=Path,default=Path('tools/benchmark/dominant-quality-rc16-individual.json'))
    p.add_argument('--after-results',type=Path,help='Final matcher replay on the identical cold window cache')
    p.add_argument('--output',type=Path,required=True)
    args=p.parse_args()
    old,new=load(args.before/'tracks.json'),load(args.after/'tracks.json')
    if Path(old['source']).resolve()!=Path(new['source']).resolve():
        raise ValueError('Captures use different sources')
    before,rawBefore=flatten(old);after,rawAfter=flatten(new)
    if rawBefore!=rawAfter:
        raise ValueError('Detector observations changed; cannot isolate selection')
    details=[]
    for case in load(args.reference)['cases']:
        if Path(case['source']).resolve()!=Path(old['source']).resolve():continue
        if 'box' not in case:
            raise ValueError('Reference needs independently reviewed actor box')
        key=(case['timestamp'],tuple(case['box']))
        if key not in before or key not in after:
            raise ValueError(f'Reviewed observation absent: {case["id"]}')
        details.append(dict(id=case['id'],label=case['label'],timestamp=case['timestamp'],
            before=before[key]['admitted'],after=after[key]['admitted']))
    reviewed={label:dict(points=sum(d['label']==label for d in details),
        before=sum(d['label']==label and d['before'] for d in details),
        after=sum(d['label']==label and d['after'] for d in details)) for label in ('lead','other','ambiguous')}
    lost=[d for d in details if d['label']=='lead' and d['before'] and not d['after']]
    report=dict(scope='Sparse reviewed identity points, exact observations and result diversity; not overall precision/recall',
        rawObservationsIdentical=True,observationCount=sum(rawBefore.values()),reviewed=reviewed,
        lostReviewedLeads=lost,details=details,
        before=pair_metrics(load(args.before/'results.json')),
        after=pair_metrics(load(args.after_results or args.after/'results.json')))
    args.output.write_text(json.dumps(report,indent=2),encoding='utf-8')
    print(json.dumps({k:v for k,v in report.items() if k not in ('details','scope')}))
    if lost:raise SystemExit('Reviewed lead regression')


if __name__=='__main__':main()
