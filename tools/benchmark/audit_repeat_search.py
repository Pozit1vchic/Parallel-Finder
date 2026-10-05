"""Verify repeat-search execution on unchanged cached observations.

Also replay only measured motion windows, so wider retrieval cannot be
mistaken for the separate short-static-window expansion.
"""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess
from audit_camera_repeats import distance
import math


def metrics(rows):
    return dict(total=len(rows),motion=sum(r['matchType']=='motion' for r in rows),
        bodyPoses=sum(r['matchType']=='pose' and not r['headOnlyComparison'] for r in rows),
        portraits=sum(r['matchType']=='pose' and r['headOnlyComparison'] for r in rows))


def verify(normal, repeat, probe):
    load=lambda p:json.loads(p.read_text())
    base=load(normal/'windows.json');expanded=load(repeat/'windows.json')
    assert expanded[:len(base)]==base,'Repeat search changed the underlying observations'
    assert len(expanded)>len(base),'Short pose expansion was not exercised'
    ordinary=load(normal/'results.json');results=load(repeat/'results.json')
    for p in (normal,repeat):
        log=(p/'stderr.log').read_text()
        assert 'complete_cache_hit=1 window_cache_hit=1 decoded=0 view_samples=0 pose_samples=0' in log
    assert 'expanded=0 pose_neighbours=192 identity_neighbours=128 neighbours_per_shot=8' in (normal/'stderr.log').read_text()
    assert 'expanded=1 pose_neighbours=384 identity_neighbours=256 neighbours_per_shot=12' in (repeat/'stderr.log').read_text()
    assert 'Повторный поиск завершён' in (repeat/'stdout.log').read_text()
    out=repeat/'motion-only-probe';out.mkdir(exist_ok=False)
    motion=[w for w in base if not w['static']]
    (out/'windows.json').write_text(json.dumps(motion,separators=(',',':')))
    env=dict(os.environ,PF_DEBUG_ANALYSIS='1')
    env['PATH']='D:/msys2/ucrt64/bin;'+env.get('PATH','')
    counts={}
    for enabled in (False,True):
        name='repeat' if enabled else 'normal'
        config=dict(individualPairs=True,expandedSearch=enabled,sameSourceGapFloorSec=12)
        (out/(name+'-params.json')).write_text(json.dumps(config))
        process=subprocess.run([str(probe.resolve()),str((out/'windows.json').resolve()),'--all',
            str((out/(name+'-params.json')).resolve())],env=env,capture_output=True,check=True,
            creationflags=subprocess.CREATE_NO_WINDOW)
        (out/(name+'-results.json')).write_bytes(process.stdout)
        (out/(name+'.log')).write_bytes(process.stderr)
        log=process.stderr.decode()
        candidates=re.search(r'candidates=(\d+) compared=(\d+)',log)
        assert candidates
        counts[name]=dict(candidates=int(candidates[1]),compared=int(candidates[2]),
            results=len(json.loads(process.stdout)['results']))
    assert counts['repeat']['candidates']>counts['normal']['candidates']
    assert counts['repeat']['compared']>counts['normal']['compared']
    def pair_key(r):
        return tuple(r[k] for k in ('leftSource','rightSource','leftStart','leftEnd','rightStart','rightEnd'))
    conflicts=[]
    for enabled,windows,ui_results in ((False,base,ordinary),(True,expanded,results)):
        name='repeat' if enabled else 'normal'
        input_path=normal/'windows.json' if not enabled else repeat/'windows.json'
        process=subprocess.run([str(probe.resolve()),str(input_path.resolve()),'--all',
            str((out/(name+'-params.json')).resolve())],env=env,capture_output=True,check=True,
            creationflags=subprocess.CREATE_NO_WINDOW)
        replay=json.loads(process.stdout)['results']
        (out/('full-'+name+'.json')).write_bytes(process.stdout)
        expected={pair_key(r):r for r in ui_results}
        assert {pair_key(r) for r in replay}==set(expected),'Core replay and UI selected different pairs'
        for r in replay:
            for field in ('similarity','identityVerified','faceVerified','headOnlyComparison'):
                assert r[field]==expected[pair_key(r)][field]
        def camera(a,b):
            d=distance(a,b)
            context=sum(math.sqrt(max(x,0)*max(y,0)) for x,y in zip(a['context'],b['context']))
            return a['source']==b['source'] and d is not None and d<=.035 and context>=.90
        used=set()
        for i,r in enumerate(replay):
            a,b=(windows[r[k]] for k in ('leftIndex','rightIndex'))
            head=a['static'] and r['headOnlyComparison']
            for w in (a,b):
                key=(w['source'],w['scene']);assert key not in used;used.add(key)
            if head and camera(a,b):conflicts.append((name,'self-view',i))
            for prior in replay[:i]:
                c,d=(windows[prior[k]] for k in ('leftIndex','rightIndex'))
                if not (head or (c['static'] and prior['headOnlyComparison'])):continue
                if any(camera(x,y) for x in (a,b) for y in (c,d)):
                    conflicts.append((name,'recurring-view',i))
    assert not conflicts,conflicts
    report=dict(normal=metrics(ordinary),repeat=metrics(results),observationsIdentical=True,
        originalWindows=len(base),extraSupportedPoseWindows=len(expanded)-len(base),
        repeatedDecodeFrames=0,repeatedInferenceSamples=0,statusConfirmsRepeatSearch=True,
        motionOnlyProbe=counts,coreReplaySemanticFieldsIdentical=True,measuredPortraitCameraConflicts=conflicts,
        scope='Measured workloads; additional useful pairs are not guaranteed for every source')
    (repeat/'repeat-verification.json').write_text(json.dumps(report,indent=2))
    print(json.dumps(report,indent=2))


if __name__=='__main__':
    p=argparse.ArgumentParser()
    p.add_argument('--normal',type=Path,required=True)
    p.add_argument('--repeat',type=Path,required=True)
    p.add_argument('--probe',type=Path,default=Path('build/ucrt64-release/pf_matcher_probe.exe'))
    a=p.parse_args();verify(a.normal,a.repeat,a.probe)
