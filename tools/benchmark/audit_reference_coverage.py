"""Check retention of manually identified development intervals, not accuracy.

Times are in the diagnostic source's timeline. Overlap reports retained model
observations; it neither proves identity nor accepts a parallel automatically.
"""
import argparse
import json
import math
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--windows', type=Path, required=True)
    parser.add_argument('--reference', type=Path, required=True)
    parser.add_argument('--tracks', type=Path,
                        help='Optional pre-admission track snapshot to locate earlier losses')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    windows = json.loads(args.windows.read_text(encoding='utf-8-sig'))
    reference = json.loads(args.reference.read_text(encoding='utf-8-sig'))
    tracks = (json.loads(args.tracks.read_text(encoding='utf-8-sig'))['tracks']
              if args.tracks else None)
    rows = []
    for interval in reference['intervals']:
        start, end = interval['start'], interval['end']
        if not math.isfinite(start) or not math.isfinite(end) or not 0 <= start < end:
            raise ValueError('Reference intervals must be finite ordered ranges')
        retained = []
        for index, window in enumerate(windows):
            times = [frame['time'] for frame in window['frames']
                     if start <= frame['time'] < end]
            if times:
                retained.append(dict(window=index, scene=window['scene'],
                                     track=window['track'], observations=len(times),
                                     first=min(times), last=max(times)))
        row = dict(**interval, retained=retained,
                   hasRetainedObservations=bool(retained))
        if tracks is not None:
            detected = []
            for track in tracks:
                times = [o['time'] for o in track['observations']
                         if start <= o['time'] < end]
                if times:
                    detected.append(dict(track=track['id'],
                                         admitted=bool(track['sourceLead']),
                                         observations=len(times),
                                         first=min(times), last=max(times)))
            row['tracked'] = detected
            row['hasTrackedObservations'] = bool(detected)
            row['hasAdmittedObservations'] = any(t['admitted'] for t in detected)
            row['firstMissingStage'] = (
                'none_before_pair_comparison' if retained else
                'window_preparation' if row['hasAdmittedObservations'] else
                'source_identity_admission' if detected else 'detection_or_tracking')
        rows.append(row)
    report = dict(scope='development interval retention; not match/class accuracy',
                  windows=str(args.windows.resolve()), reference=reference,
                  intervals=rows)
    # Avoid replacing previous experimental evidence by accident.
    with args.output.open('x', encoding='utf-8') as destination:
        json.dump(report, destination, indent=2, ensure_ascii=False)


if __name__ == '__main__':
    main()
