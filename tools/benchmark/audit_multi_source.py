"""Verify desktop captures, preserved strong pairs and measured footage controls.

This checks a finite recorded dataset; it does not establish semantic recall.
"""
import argparse
from collections import Counter
import json
from pathlib import Path
import subprocess
import math
from audit_camera_repeats import distance

def load(path): return json.loads(path.read_text(encoding='utf-8-sig'))
def key(row):
    return tuple((row[s+'Source'],round(row[s+'Start'],6),round(row[s+'End'],6)) for s in ('left','right'))
def strong(row): return row['matchType']=='motion' or not row['headOnlyComparison']

def camera_check(rows,windows):
    sides=[]
    for r in rows:
        pair=[]
        for s in ('left','right'):
            pool=[w for w in windows if w['source']==r[s+'Source'] and w['track']==r[s+'TrackId']
                and w['static']==(r['matchType']=='pose') and abs(w['sceneStart']-r[s+'SceneStart'])<1e-5]
            pair.append(min(pool,key=lambda w:abs(w['frames'][0]['time']-r[s+'Start'])+abs(w['frames'][-1]['time']-r[s+'End'])))
        sides.append(pair)
    def equal(a,b):
        d=distance(a,b)
        return d is not None and d<=.035 and sum(math.sqrt(max(x,0)*max(y,0)) for x,y in zip(a['context'],b['context']))>=.90
    portrait=lambda r:r['matchType']=='pose' and r['headOnlyComparison']
    for i,r in enumerate(rows):
        if portrait(r):assert not equal(*sides[i]),('portrait compared to its own camera',i)
        for j in range(i):
            if portrait(r) or portrait(rows[j]):
                assert not any(equal(a,b) for a in sides[i] for b in sides[j]),('camera reused',i,j)
    return True

def verify(args):
    root=args.audit; report={'single':{},'groups':{}}
    for name in ('jack','steve','soldier-long','soldier-short','tobey','dean'):
        baseline=load(root/f'{name}-baseline/results.json'); current=load(root/f'{name}-{args.phase}/results.json')
        prior={key(r):r for r in baseline if strong(r)}; now={key(r):r for r in current}
        missing=set(prior)-set(now)
        reviewed={'jack':{key(baseline[i]):i for i in (7,21,27,28,34,37,42)}}.get(name,{})
        assert not (missing-set(reviewed)),(name,'unreviewed old pair lost',missing-set(reviewed))
        assert all(r['matchType']!='motion' for k,r in prior.items() if k in missing),'old motion lost'
        for k,r in prior.items():
            if k in missing:continue
            assert abs(r['similarity']-now[k]['similarity'])<1e-6,(name,'old score changed',k)
        usage=Counter((r[s+'Source'],round(r[s+'SceneStart'],6),round(r[s+'SceneEnd'],6)) for r in current for s in ('left','right'))
        assert max(usage.values(),default=0)<=1,(name,'shot reused')
        assert all(r['identityVerified'] for r in current),(name,'unverified identity')
        old_windows=load(root/f'{name}-baseline/windows.json'); new_windows=load(root/f'{name}-{args.phase}/windows.json')
        strip=lambda rows:[{k:v for k,v in r.items() if k not in ('sequence','sequencePts','measuredFaces')} for r in rows]
        assert strip(old_windows)==strip(new_windows),(name,'admitted observations changed')
        camera_check(current,new_windows)
        count=lambda rows:dict(total=len(rows),motion=sum(r['matchType']=='motion' for r in rows),body=sum(r['matchType']=='pose' and not r['headOnlyComparison'] for r in rows),head=sum(r['matchType']=='pose' and r['headOnlyComparison'] for r in rows))
        report['single'][name]={'before':count(baseline),'after':count(current),'oldStrongRetained':len(prior)-len(missing),'reviewedLegacyExclusions':[reviewed[k] for k in sorted(missing)],'newStrong':sum(strong(r) and key(r) not in prior for r in current),'observationsPreserved':True,'shotUseMaximum':max(usage.values(),default=0),'portraitCameraConflicts':0}
    for name,selected,supplied in [('soldier-two',2,2),('foreign-three',2,3),('foreign-reversed',2,3),('same-actor-three',3,3),('mixed-two',1,2)]:
        cap=root/f'{name}-{args.phase}'; rows=load(cap/'results.json'); windows=load(cap/'windows.json')
        sources=sorted({w['source'] for w in windows})
        assert len(sources)==selected,(name,sources)
        assert all(r[s+'Source'] in sources for r in rows for s in ('left','right'))
        log=(cap/'stderr.log').read_text()
        assert f'supplied={supplied} selected={selected}' in log
        report['groups'][name]={'supplied':supplied,'selected':sources,'results':len(rows),'crossFile':sum(r['leftSource']!=r['rightSource'] for r in rows),'identityVerified':all(r['identityVerified'] for r in rows)}
    assert {key(r) for r in load(root/f'foreign-three-{args.phase}/results.json')}=={key(r) for r in load(root/f'foreign-reversed-{args.phase}/results.json')},'input order changed pairs'
    for name in ('jack','steve','soldier-two'):
        rows=load(root/f'{name}-repeat-{args.phase}/results.json')
        assert all(r['identityVerified'] for r in rows)
        camera_check(rows,load(root/f'{name}-repeat-{args.phase}/windows.json'))
        log=(root/f'{name}-repeat-{args.phase}/stderr.log').read_text()
        assert 'expanded=1 pose_neighbours=384 identity_neighbours=256 neighbours_per_shot=12' in log
        report.setdefault('repeat',{})[name]=len(rows)
    (root/'verification.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    print(json.dumps(report,indent=2))

def controls(args):
    root=args.audit; windows=load(root/f'foreign-three-{args.phase}/windows.json')
    previous=load(root/'soldier-two-candidate-v3/results.json')
    reps={(w['source'],w['scene']):w for w in windows if w.get('sequence')}
    selected=[]; pairs=[]; known=[]
    for index in (0,2,3,4,5,7):
        row=previous[index]; known.append(row)
        for side in ('left','right'):
            eligible=[w for w in windows if w['source']==row[side+'Source'] and w['track']==row[side+'TrackId'] and w['static']==(row['matchType']=='pose')]
            best=min(eligible,key=lambda w:abs(w['frames'][0]['time']-row[side+'Start'])+abs(w['frames'][-1]['time']-row[side+'End']))
            assert abs(best['frames'][0]['time']-row[side+'Start'])<.01
            best=dict(best); rep=reps[(best['source'],best['scene'])]
            best['sequence']=rep['sequence'];best['sequencePts']=rep['sequencePts'];selected.append(best)
        pairs.append({'id':f'ffmpeg-reviewed-copy-{index}','a':len(selected)-2,'b':len(selected)-1})
    output=root/'footage-controls';output.mkdir(exist_ok=True)
    params=output/'params.json';params.write_text(json.dumps({'individualPairs':True}))
    cases=output/'pairs.json';cases.write_text(json.dumps(pairs))
    raw=output/'windows.json';raw.write_text(json.dumps(selected))
    run=lambda path:json.loads(subprocess.run([str(args.probe),str(path),'--batch',str(cases),str(params)],capture_output=True,check=True).stdout)
    filtered=run(raw)
    bare=output/'without-footage.json';bare.write_text(json.dumps([{k:v for k,v in w.items() if k not in ('sequence','sequencePts','measuredFaces')} for w in selected]))
    original=run(bare)
    report=[]
    for case,old,new,row in zip(pairs,original,filtered,known):
        assert old['similarity']>0,(case,'control was already rejected')
        assert new['similarity']==0,(case,'copied footage survived',new['similarity'])
        for current in load(root/f'foreign-three-{args.phase}/results.json'):
            same=lambda x,y:abs(x-y)<.01
            assert not (all(current[s+'Source']==row[s+'Source'] and same(current[s+'SceneStart'],row[s+'SceneStart']) for s in ('left','right'))),'known copy remained'
        report.append({'id':case['id'],'before':old['similarity'],'after':new['similarity'],'sources':[row[s+'Source'] for s in ('left','right')],'starts':[row[s+'Start'] for s in ('left','right')]})
    (output/'verified.json').write_text(json.dumps(report,indent=2))
    print(json.dumps(report,indent=2))

def replay(args):
    report={}
    for name in ('jack','steve','soldier-two','jack-repeat','steve-repeat','soldier-two-repeat'):
        cap=args.audit/f'{name}-{args.phase}'; output=cap/'core-replay';output.mkdir(exist_ok=True)
        params=output/'params.json'
        params.write_text(json.dumps({'individualPairs':True,'recoverUnusedShots':True,'coverageSeedLimit':50,
            'maxUniqueResults':500,'expandedSearch':name.endswith('-repeat'),'maxComparisonThreads':4}))
        proc=subprocess.run([str(args.probe.resolve()),str((cap/'windows.json').resolve()),'--all',str(params.resolve())],capture_output=True,check=True)
        (output/'results.json').write_bytes(proc.stdout); (output/'stderr.log').write_bytes(proc.stderr)
        native=json.loads(proc.stdout)['results'];desktop={key(r):r for r in load(cap/'results.json')}
        assert {key(r) for r in native}==set(desktop),(name,'native and desktop intervals differ')
        for r in native:
            for field in ('similarity','identityVerified','faceVerified','headOnlyComparison'):
                assert r[field]==desktop[key(r)][field],(name,field)
        report[name]={'results':len(native),'productionReplayIdentical':True}
        print(name,len(native),flush=True)
    (args.audit/'core-replay-verification.json').write_text(json.dumps(report,indent=2))

def identity(args):
    output=args.audit/'shared-identity';output.mkdir(exist_ok=True)
    measured={}
    for name in ('jack','steve','soldier-short','soldier-long','dean'):
        rows=load(args.audit/f'{name}-{args.phase}/windows.json');shots={}
        def strength(w):return w['faceConfidence'] if w['faceConfidence']>=.45 and w['face'] and all(math.isfinite(v) for v in w['face']) else -1
        for w in rows:
            if w['scene'] not in shots or strength(w)>strength(shots[w['scene']]):shots[w['scene']]=w
        measured[name]=[{**{k:w[k] for k in ('source','scene','sceneStart','sceneEnd','face','faceConfidence')},
                         'frames':[w['frames'][0]]} for w in shots.values()]
    report={}
    for name,inputs,wanted in [('soldier-two',['soldier-short','soldier-long'],['soldier-short','soldier-long']),
        ('foreign-three',['soldier-short','soldier-long','steve'],['soldier-short','soldier-long']),
        ('same-actor-three',['soldier-short','soldier-long','dean'],['soldier-short','soldier-long','dean']),
        ('mixed-two',['jack','steve'],['jack'])]:
        rows=[w for src in inputs for w in measured[src]];path=output/f'{name}-windows.json';path.write_text(json.dumps(rows,separators=(',',':')))
        selected=json.loads(subprocess.run([str(args.probe.resolve()),str(path.resolve()),'--identity'],capture_output=True,check=True).stdout)
        assert set(selected['sources'])=={measured[src][0]['source'] for src in wanted}
        assert all(flag==(w['source'] in selected['sources']) for flag,w in zip(selected['windows'],rows))
        (output/f'{name}-selection.json').write_text(json.dumps(selected,indent=2))
        report[name]={'supplied':selected['supplied'],'selected':selected['sources'],'measuredPrototypes':len(rows)}
    (output/'verified.json').write_text(json.dumps(report,indent=2));print(json.dumps(report,indent=2))

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('mode',choices=('verify','controls','replay','identity'));p.add_argument('--phase',default='complete');p.add_argument('--audit',type=Path,required=True);p.add_argument('--probe',type=Path,default=Path('build/ucrt64-release/pf_matcher_probe.exe'))
    a=p.parse_args(); {'verify':verify,'controls':controls,'replay':replay,'identity':identity}[a.mode](a)



