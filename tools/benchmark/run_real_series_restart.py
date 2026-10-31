"""Run five fresh processes against a NEW real series fixture cache.

Use prepare_real_series_fixture.py first. Each process gets an isolated Qt test
profile; only the explicit observation/selection cache persists across processes.
All reports remain local beside the fixture. Existing evidence is not overwritten.
"""
import argparse
import json
import os
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', type=Path, required=True)
    parser.add_argument('--manifest', type=Path, required=True)
    parser.add_argument('--model', type=Path, required=True)
    parser.add_argument('--runtime', type=Path, required=True)
    parser.add_argument('--models', type=Path, required=True)
    parser.add_argument('--qt-plugins', type=Path, required=True)
    parser.add_argument('--qt-bin', type=Path, required=True)
    args = parser.parse_args()
    for path in (args.exe, args.manifest, args.model, args.runtime):
        if not path.is_file():
            parser.error(f'Missing input: {path}')
    root = args.manifest.resolve().parent
    manifest = json.loads(args.manifest.read_text(encoding='utf-8'))
    if Path(manifest['cacheRoot']).exists():
        parser.error('Fixture cache already exists; create a fresh fixture to preserve cold-run evidence')
    env = os.environ.copy()
    # Do not inherit opt-in overrides from unrelated diagnostics.
    for key in ('PF_SMOKE_TARGET_ID', 'PF_ALLOW_UNVERIFIED_TARGETS', 'PF_TEST_REAL_CHARACTER_VIDEO',
                'PF_BENCHMARK_CACHE_ROOT', 'PF_DEBUG_TRACKS_JSON'):
        env.pop(key, None)
    env.update(PF_TEST_REAL_SERIES_MANIFEST=str(args.manifest.resolve()),
               PF_MODEL_PATH=str(args.model.resolve()), PF_MODEL_ROOT=str(args.models.resolve()),
               PF_ORT_DLL=str(args.runtime.resolve()), PF_PROVIDER_ROOT=str(args.runtime.resolve().parent),
               PF_DEBUG_ANALYSIS='1', QT_QPA_PLATFORM='offscreen',
               QT_QPA_PLATFORM_PLUGIN_PATH=str(args.qt_plugins.resolve()))
    env['PATH'] = str(args.runtime.resolve().parent) + os.pathsep + str(args.qt_bin.resolve()) + os.pathsep + env.get('PATH', '')
    reports = []
    for phase in ('seed', 'incremental', 'warm', 'manual', 'manual-warm'):
        prefix = root / phase
        outputs = [prefix.with_suffix(suffix) for suffix in ('.json', '.txt', '.diagnostics.log')]
        if any(path.exists() for path in outputs):
            parser.error(f'Existing evidence for {phase}; create a new fixture')
        env.update(PF_TEST_REAL_SERIES_PHASE=phase, PF_TEST_REAL_SERIES_REPORT=str(outputs[0]))
        with outputs[2].open('wb') as diagnostics:
            result = subprocess.run([str(args.exe.resolve()), 'realSeriesIncrementalRestart',
                                     '-o', str(outputs[1]) + ',txt'], env=env, stdout=diagnostics,
                                    stderr=subprocess.STDOUT, timeout=240)
        if result.returncode:
            raise SystemExit(f'{phase} failed ({result.returncode}); inspect {outputs[1]} and {outputs[2]}')
        report = json.loads(outputs[0].read_text(encoding='utf-8'))
        reports.append({'phase': phase, 'sourceCount': len(report['sources']),
                        'analyzedFrames': report['analyzedFrames'], 'poseDetections': report['poseDetections'],
                        'cacheHits': sum(bool(row['observationCacheHit']) for row in report['sources']),
                        'automaticSelections': sum(bool(row['targetAutomatic']) for row in report['sources'])})
        print(reports[-1], flush=True)
    (root / 'summary.json').write_text(json.dumps(reports, indent=2), encoding='utf-8')


if __name__ == '__main__':
    main()
