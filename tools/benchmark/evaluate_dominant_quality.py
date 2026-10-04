"""Evaluate sparse reviewed identity points against exact tracker snapshots.

This measures annotated observation admission, not overall matcher accuracy.
"""
import argparse
import json
from pathlib import Path

def evaluate(reference, report_name):
    dataset = Path(reference['datasetRoot']).resolve()
    snapshots = {}
    details = []
    for case in reference['cases']:
        snapshot_path = Path(case['snapshot']).resolve()
        if snapshot_path not in snapshots:
            snapshot = json.loads(snapshot_path.read_text(encoding='utf-8'))
            source = Path(snapshot['source']).resolve()
            if not source.is_relative_to(dataset) or not source.is_file():
                raise ValueError('Snapshot source is not an existing acceptance video')
            report = json.loads((snapshot_path.parent / report_name).read_text(encoding='utf-8-sig'))
            snapshots[snapshot_path] = (snapshot, {t['id']: t for t in snapshot['tracks']},
                                       {t['id']: t for t in report})
        snapshot, tracks, report = snapshots[snapshot_path]
        if Path(case['source']).resolve() != Path(snapshot['source']).resolve():
            raise ValueError('Reference and snapshot source disagree')
        track = tracks[case['track']]
        index = case['observation']
        if abs(track['observations'][index]['time'] - case['timestamp']) > 1e-7:
            raise ValueError('Reference observation no longer matches its frozen snapshot')
        prediction = report[case['track']]
        runs = prediction['runs']
        for run in runs:
            if not 0 <= run['begin'] < run['end'] <= len(track['observations']):
                raise ValueError('Invalid observation range in replay')
            if abs(run['start'] - track['observations'][run['begin']]['time']) > 1e-7:
                raise ValueError('Run start/index mismatch')
            if abs(run['stop'] - track['observations'][run['end']-1]['time']) > 1e-7:
                raise ValueError('Run stop/index mismatch')
        admitted = bool(prediction['selected'] and any(r['begin'] <= index < r['end'] for r in runs))
        details.append(dict(id=case['id'], label=case['label'],
                            before=track['sourceLead'], after=admitted,
                            recovered=prediction['recovered'], timestamp=case['timestamp']))
    metrics = {}
    for label in ['lead', 'other', 'ambiguous']:
        cases = [c for c in details if c['label'] == label]
        metrics[label] = dict(annotatedPoints=len(cases), admittedBefore=sum(c['before'] for c in cases),
                              admittedAfter=sum(c['after'] for c in cases))
    return dict(scope='Sparse reviewed identity observations; not overall matcher precision/recall',
                metrics=metrics, details=details)

def evaluate_current_snapshots(reference, current_paths):
    """Remap reviewed observations after retracking; track IDs are not identity."""
    dataset = Path(reference['datasetRoot']).resolve()
    details = []
    represented = []
    for current_path in current_paths:
        current = json.loads(current_path.read_bytes())
        source = Path(current['source']).resolve()
        if not source.is_file() or not source.is_relative_to(dataset):
            raise ValueError('Current snapshot is not from the acceptance dataset')
        if source in represented:
            raise ValueError('Multiple current snapshots for one source are ambiguous')
        represented.append(source)
        cases = [c for c in reference['cases'] if Path(c['source']).resolve() == source]
        if not cases:
            raise ValueError('Current source has no reviewed identity points')
        # Exact source observations survive scene retracking under different IDs.
        # Never use old numeric track IDs to infer identity in a new run.
        observations = {}
        for track in current['tracks']:
            for index, observation in enumerate(track['observations']):
                key = (observation['time'], tuple(observation['box']))
                if key in observations:
                    raise ValueError('Duplicate source observations cannot be remapped uniquely')
                observations[key] = (track, index, observation)
        frozen = {}
        for case in cases:
            path = Path(case['snapshot']).resolve()
            if path not in frozen:
                snapshot = json.loads(path.read_bytes())
                if Path(snapshot['source']).resolve() != source:
                    raise ValueError('Frozen reference source disagrees with current source')
                frozen[path] = {t['id']: t for t in snapshot['tracks']}
            old_track = frozen[path][case['track']]
            old_observation = old_track['observations'][case['observation']]
            if abs(old_observation['time'] - case['timestamp']) > 1e-7:
                raise ValueError('Reviewed point disagrees with frozen observation')
            key = (old_observation['time'], tuple(old_observation['box']))
            if key not in observations:
                raise ValueError(f'Reviewed observation changed or disappeared: {case["id"]}')
            track, index, observation = observations[key]
            if observation != old_observation:
                raise ValueError(f'Inference changed for reviewed observation: {case["id"]}')
            runs = track.get('allowedRuns', [[0, len(track['observations'])]] if track['sourceLead'] else [])
            if any(not 0 <= begin < end <= len(track['observations']) for begin, end in runs):
                raise ValueError('Invalid current observation runs')
            admitted = bool(track['sourceLead'] and any(begin <= index < end for begin, end in runs))
            details.append(dict(id=case['id'],label=case['label'],timestamp=case['timestamp'],
                before=old_track['sourceLead'],after=admitted,recovered=track.get('recovered',False),
                oldTrack=case['track'],currentTrack=track['id'],snapshot=str(current_path)))
    metrics = {}
    for label in ('lead', 'other', 'ambiguous'):
        points = [c for c in details if c['label'] == label]
        metrics[label] = dict(annotatedPoints=len(points),admittedBefore=sum(c['before'] for c in points),
            admittedAfter=sum(c['after'] for c in points))
    return dict(scope='Sparse reviewed identity observations on explicitly listed sources; not overall matcher precision/recall',
        sources=[str(s) for s in represented],metrics=metrics,details=details)

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--reference', type=Path, default=Path(__file__).with_name('dominant-quality-v1.json'))
    parser.add_argument('--report-name', default='verified-runs-replay-v2.json')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--current-snapshots', type=Path, nargs='+',
                        help='Evaluate real analysis snapshots, remapping exact observations after scene retracking')
    args = parser.parse_args()
    reference = json.loads(args.reference.read_text(encoding='utf-8'))
    result = (evaluate_current_snapshots(reference, args.current_snapshots) if args.current_snapshots
              else evaluate(reference, args.report_name))
    args.output.write_text(json.dumps(result, indent=2, ensure_ascii=False), encoding='utf-8')
    print(json.dumps(result['metrics']))

if __name__ == '__main__':
    main()
