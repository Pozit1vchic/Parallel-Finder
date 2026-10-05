"""Compare measured camera-view enrichment with a frozen rc16.1 capture.

The equality assertion covers every old pose/identity/boundary field. Camera
distances are diagnostics for reviewing original frames, not semantic labels.
"""
import argparse
import json
import math
from pathlib import Path


def key(r):
    return tuple(r[k] for k in ('leftSource', 'rightSource', 'leftStart', 'leftEnd', 'rightStart', 'rightEnd'))


def portrait(r):
    return r['matchType']=='pose' and r['headOnlyComparison']


def distance(a, b):
    if len(a.get('view', [])) != 432 or len(b.get('view', [])) != 432:
        return None
    differences=[]
    for shift in (-1,0,1):
        total=weight=0
        for row in range(9):
            for col in range(max(0,-shift),min(16,16-shift)):
                w=((2 if col<5 or col>=11 else 1)+(2 if col+shift<5 or col+shift>=11 else 1))/2
                for channel in range(3):
                    total+=w*abs(a['view'][(row*16+col)*3+channel]-b['view'][(row*16+col+shift)*3+channel])
                    weight+=w
        differences.append(total/weight)
    return min(differences)


def audit(before, after):
    old_windows = json.loads((before/'windows.json').read_text())
    windows = json.loads((after/'windows.json').read_text())
    stripped = [{k:v for k,v in w.items() if k not in ('view', 'viewSampled')} for w in windows]
    assert stripped == old_windows, 'Pose, identity or scene data changed during camera enrichment'
    old = json.loads((before/'results.json').read_text())
    new = json.loads((after/'results.json').read_text())
    by_scene = {}
    for w in windows:
        if w['static']:
            by_scene.setdefault((w['source'],w['scene']),w)
    def side(r, s):
        candidates=[w for w in windows if w['source']==r[s+'Source']
            and w['track']==r[s+'TrackId'] and w['static']==(r['matchType']=='pose')
            and abs(w['sceneStart']-r[s+'SceneStart'])<1e-5
            and abs(w['sceneEnd']-r[s+'SceneEnd'])<1e-5]
        return min(candidates,key=lambda w:abs(w['frames'][0]['time']-r[s+'Start'])
            + abs(w['frames'][-1]['time']-r[s+'End']))
    def describe(r):
        a,b=side(r,'left'),side(r,'right')
        context=sum(math.sqrt(max(x,0)*max(y,0)) for x,y in zip(a['context'],b['context']))
        return dict(id=r['id'],head=portrait(r),matchType=r['matchType'],left=r['leftStart'],right=r['rightStart'],
            viewDistance=distance(a,b),contextSimilarity=context,similarity=r['similarity'])
    def stats(rows):
        return dict(results=len(rows),heads=sum(portrait(r) for r in rows),
            bodyOrMotion=sum(not portrait(r) for r in rows),motion=sum(r['matchType']=='motion' for r in rows))
    retained = {key(r):r for r in new}
    oldkeys={key(r) for r in old}
    unchanged = [r for r in old if key(r) in retained]
    def same_view(a,b):
        d=distance(a,b)
        context=sum(math.sqrt(max(x,0)*max(y,0)) for x,y in zip(a['context'],b['context']))
        return a['source']==b['source'] and d is not None and d<=.035 and context>=.90
    for r in unchanged:
        assert r['similarity']==retained[key(r)]['similarity']
        assert r['identityVerified']==retained[key(r)]['identityVerified']
        assert r['faceVerified']==retained[key(r)]['faceVerified']
    original_bodies=[r for r in old if not portrait(r)]
    lost_bodies=[r['id'] for r in original_bodies if key(r) not in retained]
    assert not lost_bodies, f'Original body/motion parallels lost: {lost_bodies}'
    conflicts=[]
    used_shots={}
    for i,r in enumerate(new):
        a,b=side(r,'left'),side(r,'right')
        for w in (a,b):
            shot=(w['source'],w['scene'])
            used_shots[shot]=used_shots.get(shot,0)+1
        if portrait(r) and same_view(a,b):
            conflicts.append(dict(kind='self-view',pair=r['id']))
        for previous in new[:i]:
            if not (portrait(r) or portrait(previous)):continue
            if any(same_view(x,y) for x in (a,b) for y in (side(previous,'left'),side(previous,'right'))):
                conflicts.append(dict(kind='recurring-view',pairs=[r['id'],previous['id']]))
    assert not conflicts, f'Recurring portrait camera survived: {conflicts}'
    assert max(used_shots.values(),default=0)<=1
    report=dict(before=stats(old),after=stats(new),originalWindowFieldsIdentical=True,
        unchangedPairScores=len(unchanged),originalBodyOrMotionPairsPreserved=len(original_bodies),
        repeatedPortraitCameraConflicts=conflicts,maxShotReuse=max(used_shots.values(),default=0),
        measuredViews=sum(bool(w['view']) for w in by_scene.values()),
        sampledScenes=len(by_scene),removed=[describe(r) for r in old if key(r) not in retained],
        added=[describe(r) for r in new if key(r) not in oldkeys],
        remaining=[describe(r) for r in new])
    (after/'camera-verification.json').write_text(json.dumps(report,indent=2))
    print(json.dumps({k:v for k,v in report.items() if k not in ('removed','added','remaining')},indent=2))


def verify_warm(capture, warm):
    assert json.loads((capture/'windows.json').read_text())==json.loads((warm/'windows.json').read_text())
    def results(path):
        return [{k:v for k,v in r.items() if k not in ('leftPreview','rightPreview')}
            for r in json.loads((path/'results.json').read_text())]
    assert results(capture)==results(warm), 'Non-preview result data changed on cache replay'
    log=(warm/'stderr.log').read_text()
    assert 'complete_cache_hit=1 window_cache_hit=1 decoded=0 view_samples=0 pose_samples=0' in log
    report=dict(windowsIdentical=True,resultsIdenticalExceptTemporaryPreviewUrls=True,
        completeCacheHit=True,decodedFrames=0,viewSamples=0,poseSamples=0)
    (capture/'cache-verification.json').write_text(json.dumps(report,indent=2))
    print(json.dumps(report))


if __name__=='__main__':
    p=argparse.ArgumentParser()
    p.add_argument('--before',type=Path,required=True)
    p.add_argument('--after',type=Path,required=True)
    p.add_argument('--warm',type=Path)
    a=p.parse_args()
    audit(a.before,a.after)
    if a.warm: verify_warm(a.after,a.warm)
