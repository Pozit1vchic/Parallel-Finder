"""Inspect independent-shot identity evidence without changing admission.

Legacy face prototypes and separately aligned direct observations are never
mixed. Scores identify candidates for visual review, not identity ground truth.
Only the first capture of each source is used; repeated captures cannot vote.
"""
import argparse
import json
from pathlib import Path
import re
import subprocess
import numpy as np


def normalized(values, dimension=None):
    vector = np.asarray(values, dtype=np.float64)
    if vector.ndim != 1 or not len(vector) or (dimension and len(vector) != dimension):
        return None
    norm = np.linalg.norm(vector)
    if not np.all(np.isfinite(vector)) or not np.isfinite(norm) or norm < 1e-12:
        return None
    return vector / norm


def face_modes(anchors):
    """Complete-link diagnostic modes; single-shot modes cannot establish a link."""
    if not anchors:
        return []
    matrix = np.stack([a['face'] for a in anchors])
    scores = matrix @ matrix.T
    groups = [[i] for i in range(len(anchors))]
    owners = list(range(len(anchors)))
    edges = [(float(scores[i,j]),i,j) for i in range(len(anchors))
             for j in range(i+1,len(anchors)) if scores[i,j]>=.60]
    for _, i, j in sorted(edges, key=lambda e:(-e[0],e[1],e[2])):
        a, b = owners[i], owners[j]
        if a==b or any(scores[x,y]<.60 for x in groups[a] for y in groups[b]):
            continue
        for index in groups[b]:
            owners[index]=a
        groups[a].extend(groups[b]); groups[b]=[]
    return [dict(indices=g, prototype=normalized(matrix[g].sum(axis=0))) for g in groups if len(g)>=2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--windows', type=Path, nargs='+', required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--review-dir', type=Path)
    args = parser.parse_args()
    sources = {}
    for capture in args.windows:
        rows = json.loads(capture.read_text(encoding='utf-8-sig'))
        grouped = {}
        for row in rows:
            name = str(Path(row['source']).resolve())
            grouped.setdefault(name, []).append(row)
        for name, windows in grouped.items():
            if name in sources:
                continue
            shots = {}
            for row in windows:
                if not row['frames']:
                    continue
                face = normalized(row.get('face', []))
                strength = row.get('faceConfidence', 0) if face is not None and row.get('faceConfidence', 0) >= .45 else -1
                previous = shots.get(row['scene'])
                if previous is None or strength > previous[0]:
                    shots[row['scene']] = (strength, row, face)
            anchors = []
            for scene, (strength, row, face) in sorted(shots.items()):
                if strength < .45:
                    continue
                anchors.append(dict(scene=scene, start=row['sceneStart'], end=row['sceneEnd'],
                    face=face, body=normalized(row.get('body', [])), bodyConfidence=row.get('bodyConfidence', 0)))
            sources[name] = dict(capture=str(capture.resolve()), shots=len(shots), anchors=anchors, modes=face_modes(anchors))
    result = dict(scope='diagnostic scores only; no verified identity labels or admission changes', sources=[], pairs=[])
    names = sorted(sources)
    for name in names:
        source = sources[name]
        result['sources'].append(dict(source=name, capture=source['capture'], shots=source['shots'], faceAnchors=len(source['anchors']),
            diagnosticModeShots=[[source['anchors'][i]['scene'] for i in mode['indices']] for mode in source['modes']]))
    for i, left_name in enumerate(names):
        for right_name in names[i+1:]:
            left, right = sources[left_name]['anchors'], sources[right_name]['anchors']
            if not left or not right or len(left[0]['face']) != len(right[0]['face']):
                continue
            a, b = np.stack([x['face'] for x in left]), np.stack([x['face'] for x in right])
            mean_a, mean_b = normalized(a.sum(axis=0)), normalized(b.sum(axis=0))
            matrix = a @ b.T
            mean_score = float(mean_a @ mean_b)
            majority_a = float(np.mean(a @ mean_b >= .363))
            majority_b = float(np.mean(b @ mean_a >= .363))
            modes=[]
            for x in sources[left_name]['modes']:
                for y in sources[right_name]['modes']:
                    score=float(x['prototype'] @ y['prototype'])
                    modes.append(dict(score=score,
                        leftScenes=[left[i]['scene'] for i in x['indices']],
                        rightScenes=[right[i]['scene'] for i in y['indices']],
                        majority=[float(np.mean(a[x['indices']] @ y['prototype']>=.363)),
                                  float(np.mean(b[y['indices']] @ x['prototype']>=.363))]))
            top = []
            used_a, used_b = set(), set()
            for index in np.argsort(matrix, axis=None)[::-1]:
                x, y = np.unravel_index(index, matrix.shape)
                if x in used_a or y in used_b:
                    continue
                used_a.add(x); used_b.add(y)
                body_a, body_b = left[x]['body'], right[y]['body']
                body = float(body_a @ body_b) if body_a is not None and body_b is not None and len(body_a) == len(body_b) else None
                top.append(dict(leftScene=left[x]['scene'], rightScene=right[y]['scene'],
                    leftRange=[left[x]['start'], left[x]['end']], rightRange=[right[y]['start'], right[y]['end']],
                    face=float(matrix[x,y]), body=body))
                if len(top) == 12:
                    break
            result['pairs'].append(dict(left=left_name, right=right_name, aggregate=mean_score,
                existingAdmission=len(left)>=2 and len(right)>=2 and mean_score>=.60 and majority_a>=.75 and majority_b>=.75,
                majority=[majority_a, majority_b], bestIndependentShotPairs=top,
                bestDiagnosticFaceModes=sorted(modes,key=lambda m:-m['score'])[:5]))
    args.output.write_text(json.dumps(result, indent=2, ensure_ascii=False)+'\n', encoding='utf-8')
    if args.review_dir:
        from PIL import Image, ImageDraw
        args.review_dir.mkdir(parents=True, exist_ok=False)
        frames = []
        for pi, pair in enumerate(result['pairs']):
            if 'Interview' not in pair['right'] and 'Interview' not in pair['left']:
                continue
            sheet = Image.new('RGB', (1920, 5*225), (18,18,22))
            draw = ImageDraw.Draw(sheet)
            for row, candidate in enumerate(pair['bestIndependentShotPairs'][:5]):
                for side_index, side in enumerate(('left','right')):
                    start, end = candidate[side+'Range']
                    for phase, fraction in enumerate((.2,.5,.8)):
                        time = start+(end-start)*fraction
                        path = args.review_dir/f'{pi}-{row}-{side}-{phase}.jpg'
                        decoded = subprocess.run(['D:/msys2/ucrt64/bin/ffmpeg.exe','-hide_banner','-loglevel','info',
                            '-ss',str(time),'-copyts','-threads','2','-i',pair[side],'-vf','showinfo,scale=640:-2',
                            '-frames:v','1','-n',str(path)], capture_output=True, check=True)
                        pts = re.search(r'pts_time:([\d.eE+-]+)', decoded.stderr.decode('utf-8',errors='replace'))
                        if not pts or abs(float(pts[1])-time)>.1:
                            raise RuntimeError('Unexpected review-frame timestamp')
                        image = Image.open(path).convert('RGB'); image.thumbnail((320,190))
                        x=(side_index*3+phase)*320; y=row*225
                        sheet.paste(image,(x,y+25))
                        draw.text((x+4,y+3),f'{side} scene {candidate[side+"Scene"]} @ {float(pts[1]):.3f}',fill='white')
                        if phase==0:
                            draw.text((x+4,y+210),f'face {candidate["face"]:.3f}; not identity ground truth',fill='white')
                        frames.append(dict(source=pair[side],requested=time,actual=float(pts[1]),image=str(path.resolve())))
            sheet.save(args.review_dir/f'pair-{pi}.jpg',quality=94)
        (args.review_dir/'frames.json').write_text(json.dumps(frames,indent=2)+'\n',encoding='utf-8')
    print(json.dumps([dict(left=Path(p['left']).name, right=Path(p['right']).name, aggregate=p['aggregate'],
        admitted=p['existingAdmission'], strongest=p['bestIndependentShotPairs'][0]['face'],
        bestMode=p['bestDiagnosticFaceModes'][0] if p['bestDiagnosticFaceModes'] else None) for p in result['pairs']], indent=2))


if __name__ == '__main__':
    main()
