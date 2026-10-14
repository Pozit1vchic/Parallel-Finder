"""Confirm cached continuity brackets with native source frames; no inference or cache writes."""
import argparse
import json
import os
import subprocess
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--candidates', type=Path, required=True)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--probe', type=Path, required=True)
    parser.add_argument('--runtime', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if not args.source.is_file():
        raise FileNotFoundError(args.source)
    args.output.mkdir(exist_ok=False)
    report = json.loads(args.candidates.read_text(encoding='utf-8-sig'))
    intervals = {}
    for kind, entries in (('candidate', report['candidates']), ('tail', report['endChecks'])):
        for row in entries:
            start, end = row['before'], row['after']
            if not 0 <= start < end or end-start > 30:
                raise ValueError('Invalid native confirmation interval')
            intervals.setdefault((start,end), []).append(dict(kind=kind, scene=row['scene']))
    environment = dict(os.environ)
    environment['PATH'] = str(args.runtime)+os.pathsep+environment.get('PATH','')
    rows = []
    unique_cuts = {}
    for index, ((start,end), provenance) in enumerate(sorted(intervals.items())):
        destination = args.output/f'interval-{index:04d}.json'
        subprocess.run([str(args.probe.resolve()),str(args.source.resolve()),str(start),str(end),str(destination.resolve())],
                       env=environment, check=True, capture_output=True, timeout=60)
        measured = json.loads(destination.read_text(encoding='utf-8-sig'))
        for cut in measured['boundaries']:
            if not start < cut['time'] < end:
                raise ValueError('Detector returned an out-of-range cut')
            unique_cuts[cut['time']] = cut
        rows.append(dict(start=start,end=end,provenance=provenance,boundaries=measured['boundaries']))
        if (index+1)%20 == 0 or index+1 == len(intervals):
            print(f'Confirmed {index+1}/{len(intervals)} brackets; {len(unique_cuts)} native cuts',flush=True)
    with (args.output/'confirmed.json').open('x',encoding='utf-8') as file:
        json.dump(dict(source=str(args.source.resolve()),scope='Native detector evidence from cached brackets; not manual ground truth or complete source scan',
                       sourceSize=args.source.stat().st_size,sourceModifiedNs=args.source.stat().st_mtime_ns,
                       intervals=rows,boundaries=[unique_cuts[t] for t in sorted(unique_cuts)]),file,indent=2)


if __name__ == '__main__':
    main()
