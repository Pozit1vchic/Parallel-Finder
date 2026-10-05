"""Capture real workloads and original-PTS contact sheets for rc16 A/B review.

Requires Pillow for visual review. Reports are diagnostic, not precision/recall.
Never overwrites an earlier capture. No synthetic source frames or embeddings.
"""
import argparse
import concurrent.futures
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import time

ROOT = Path(__file__).resolve().parents[2]
DATA = Path('D:/for_tests_pf')
BIN = ROOT / 'build/ucrt64-release'
FFMPEG = 'D:/msys2/ucrt64/bin/ffmpeg.exe'


def capture(args):
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    videos=[]
    for prefix in args.source:
        candidates=[p for p in DATA.iterdir() if p.name==prefix]
        if not candidates:candidates=[p for p in DATA.iterdir() if p.name.startswith(prefix)]
        if len(candidates)!=1:raise ValueError(f'Source prefix is not unique: {prefix}')
        videos.append(candidates[0])
    video=videos[0]
    exe = args.exe.resolve()
    env = {k: v for k, v in os.environ.items() if not k.startswith('PF_')}
    runtime = Path(os.environ['LOCALAPPDATA']) / 'ParallelFinder/ParallelFinder/providers/cuda/onnxruntime.dll'
    model_dir=Path(args.model_directory or 'D:/PF_CUDA/models')
    provider='cuda'
    search_path='D:/msys2/ucrt64/bin;' + env.get('PATH', '')
    pose_model=model_dir/(args.pose_model or 'yolo26m-pose-640-b1.onnx')
    if args.bundled:
        runtime=exe.parent/'onnxruntime.dll'
        model_dir=exe.parent/'models'
        pose_model=model_dir/'yolo26m-pose.onnx'
        provider='cpu'
        search_path=f'{exe.parent};{os.environ["SystemRoot"]}/System32;{os.environ["SystemRoot"]}'
        for name in ('QML_IMPORT_PATH','QML2_IMPORT_PATH','QT_PLUGIN_PATH'):
            env.pop(name,None)
    env.update(PATH=search_path, QT_QPA_PLATFORM='windows',
        PF_MODEL_PATH=pose_model.as_posix(), PF_REID_MODEL_PATH=(model_dir/'person-reid-osnet.onnx').as_posix(),
        PF_ORT_DLL=str(runtime), PF_PROVIDER_ROOT=str(runtime.parent), PF_PROVIDER=provider,
        PF_ANALYSIS_MODE='combined', PF_ANALYSIS_JSON='1', PF_ANALYSIS_MIN_PAIRS='0',
        PF_ANALYSIS_TIMEOUT_SEC='3600', PF_QUALITY_PROFILE='fast', PF_EXPANDED_SEARCH='1' if args.expanded else '0',
        PF_DEBUG_ANALYSIS='1', PF_DEBUG_POSES_JSON=str(out/'windows.json'),
        PF_DEBUG_TRACKS_JSON=str(out/'tracks.json'), PF_BENCHMARK_CACHE_ROOT=str(args.cache or out/'cache'))
    if args.max_results is not None:env['PF_MAX_UNIQUE_RESULTS']=str(args.max_results)
    if args.frozen_keys:env['PF_AUDIT_FROZEN_WINDOW_KEYS']=str(args.frozen_keys.resolve())
    if args.model_root:
        if args.bundled:raise ValueError('Bundled isolation cannot use an external model root')
        if not Path(args.model_root).is_dir():raise ValueError('Model root must exist')
        # Preserve caller spelling: model paths are part of historical cache
        # fingerprints, including Qt's mixed separators on Windows.
        env['PF_MODEL_ROOT']=args.model_root
    manifest = dict(source=str(video), exe=str(exe),
        executableSha256=hashlib.sha256(exe.read_bytes()).hexdigest(),
        sourceSize=video.stat().st_size, sourceMtimeNs=video.stat().st_mtime_ns,
        configuration={k:v for k,v in env.items() if k.startswith('PF_')},
        performanceBenchmark=False,bundledCpuIsolation=args.bundled,
        sources=[dict(source=str(p),sourceSize=p.stat().st_size,sourceMtimeNs=p.stat().st_mtime_ns) for p in videos])
    (out/'manifest.json').write_text(json.dumps(manifest, indent=2), encoding='utf-8')
    start = time.perf_counter()
    with (out/'stdout.log').open('wb') as stdout, (out/'stderr.log').open('wb') as stderr:
        proc = subprocess.run([str(exe), '--pf-analysis-smoke', *map(str,videos)], env=env,
            stdout=stdout, stderr=stderr, timeout=3700, creationflags=subprocess.CREATE_NO_WINDOW)
    if proc.returncode:
        raise RuntimeError(f'Analysis failed: {proc.returncode}; inspect {out}')
    log = (out/'stdout.log').read_text(encoding='utf-8', errors='replace')
    match = re.search(r'^\s*results_json\s*:\s*(.+)$', log, re.M)
    if not match or not (out/'windows.json').is_file():
        raise RuntimeError('Incomplete diagnostic capture')
    results = json.loads(match[1])
    (out/'results.json').write_text(json.dumps(results, indent=2), encoding='utf-8')
    summary = dict(exitCode=proc.returncode, results=len(results), diagnosticSeconds=time.perf_counter()-start)
    (out/'complete.json').write_text(json.dumps(summary, indent=2))
    print(json.dumps(summary), flush=True)


def review(args):
    from PIL import Image, ImageDraw
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    manifest = json.loads((args.capture/'manifest.json').read_text())
    video = Path(manifest['source'])
    tasks = []
    if args.overview:
        probe = subprocess.run(['D:/msys2/ucrt64/bin/ffprobe.exe', '-v', 'error',
            '-show_entries', 'format=duration', '-of', 'json', str(video)], capture_output=True, check=True)
        duration = float(json.loads(probe.stdout)['format']['duration'])
        tasks = [dict(label=f'overview {i}', time=(duration-.25)*i/47) for i in range(48)]
    else:
        results = json.loads((args.capture/'results.json').read_text())
        def pair_key(r):
            return tuple(sorted((r[s+'Source'],round(r[s+'Start'],6),round(r[s+'End'],6)) for s in ('left','right')))
        old={pair_key(r) for r in json.loads(args.baseline.read_text())} if args.baseline else set()
        for i, result in enumerate(results):
            if args.pair is not None and i not in args.pair:continue
            if pair_key(result) in old:continue
            for side in ('left', 'right'):
                for fraction in (0, .5, 1):
                    tasks.append(dict(label=f"pair {i} {side} {fraction} score {result['similarity']:.3f}",
                        source=result[side+'Source'],
                        time=result[side+'Start']+fraction*(result[side+'End']-result[side+'Start'])))
    def extract(row):
        i, task = row
        path = out/f'{i:04}.png'
        proc = subprocess.run([FFMPEG, '-hide_banner', '-loglevel', 'info', '-ss', str(task['time']),
            '-copyts', '-threads', '2', '-i', task.get('source',str(video)), '-vf', 'showinfo,scale=480:-2',
            '-frames:v', '1', '-n', str(path)], capture_output=True, creationflags=subprocess.CREATE_NO_WINDOW)
        (out/f'{i:04}.log').write_bytes(proc.stderr)
        pts = re.findall(rb'\bn:\s*0\s+pts:\s*-?\d+\s+pts_time:([-\d.]+)', proc.stderr)
        if proc.returncode or not path.is_file() or not pts:
            raise RuntimeError('Failed source-frame extraction')
        actual = float(pts[0])
        if abs(actual-task['time']) > .1:
            raise RuntimeError('Source frame outside requested time tolerance')
        return dict(task, pts=actual, image=str(path))
    with concurrent.futures.ThreadPoolExecutor(max_workers=3) as pool:
        frames = list(pool.map(extract, enumerate(tasks)))
    for page, first in enumerate(range(0, len(frames), 12)):
        sheet = Image.new('RGB', (1440, 4*300), (18,18,22))
        draw = ImageDraw.Draw(sheet)
        for j, frame in enumerate(frames[first:first+12]):
            x,y = j%3*480,j//3*300
            sheet.paste(Image.open(frame['image']), (x,y+38))
            draw.text((x+4,y+3), frame['label'], fill='white')
            draw.text((x+4,y+19), f"PTS {frame['pts']:.6f}s", fill='white')
        sheet.save(out/f'page-{page:02}.jpg', quality=92)
    (out/'manifest.json').write_text(json.dumps(dict(source=str(video),frames=frames), indent=2))
    print(f'Decoded {len(frames)} original frames', flush=True)


def annotate(args):
    from PIL import Image, ImageDraw
    out=args.output.resolve()
    out.mkdir(parents=True,exist_ok=False)
    results=json.loads((args.capture/'results.json').read_text())
    tracks={}
    for path in args.tracks or [args.capture/'tracks.json']:
        snapshot=json.loads(path.read_text())
        source=snapshot['source'].replace('\\','/')
        for t in snapshot['tracks']:tracks[(source,t['id'])]=t
    frames=json.loads((args.review/'manifest.json').read_text())['frames']
    widths={}
    for source in {r[s+'Source'] for r in results for s in ('left','right')}:
        probe=subprocess.run(['D:/msys2/ucrt64/bin/ffprobe.exe','-v','error','-select_streams','v:0',
            '-show_entries','stream=width,height','-of','json',source],capture_output=True,check=True)
        stream=json.loads(probe.stdout)['streams'][0]
        inferenceScale=min(1.,1280/stream['width'],720/stream['height'])
        widths[source.replace('\\','/')]=round(stream['width']*inferenceScale)
    for page,first in enumerate(range(0,len(frames),12)):
        sheet=Image.new('RGB',(1440,1200),(18,18,22)); draw=ImageDraw.Draw(sheet)
        for j,frame in enumerate(frames[first:first+12]):
            match=re.match(r'pair (\d+) (left|right)',frame['label'])
            if not match:raise ValueError('Expected pair review, not inventory')
            pair=results[int(match[1])];side=match[2]
            source=pair[side+'Source'].replace('\\','/')
            inferenceWidth=widths[source]
            observations=tracks[(source,pair[side+'TrackId'])]['observations']
            observation=min(observations,key=lambda o:abs(o['time']-frame['time']))
            if abs(observation['time']-frame['time'])>.25:
                raise ValueError('No nearby original actor observation')
            picture=Image.open(frame['image']).convert('RGB')
            # Use the pipeline's <=1280x720 working scale, not a fixed
            # assumption that would misplace boxes on portrait source videos.
            overlay=ImageDraw.Draw(picture)
            overlay.rectangle([p*picture.width/inferenceWidth for p in observation['box']],outline=(80,255,110),width=2)
            x,y=j%3*480,j//3*300
            sheet.paste(picture,(x,y+38))
            draw.text((x+4,y+3),frame['label'],fill='white')
            draw.text((x+4,y+19),f"PTS {frame['pts']:.6f} / track {pair[side+'TrackId']}",fill='white')
        sheet.save(out/f'page-{page:02}.jpg',quality=92)
    (out/'complete.json').write_text(json.dumps(dict(frames=len(frames))))
    print(f'Annotated {len(frames)} original frames',flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='mode', required=True)
    c = sub.add_parser('capture')
    c.add_argument('--source', required=True, action='append', help='Unique filename prefix in D:/for_tests_pf; repeat for multiple inputs')
    c.add_argument('--exe', type=Path, default=BIN/'ParallelFinder.exe')
    c.add_argument('--output', type=Path, required=True)
    c.add_argument('--cache', type=Path)
    c.add_argument('--bundled',action='store_true',help='Use only app-local models/CPU DLLs with developer paths removed')
    c.add_argument('--expanded',action='store_true',help='Exercise the desktop repeat-search option')
    c.add_argument('--max-results',type=int,choices=range(10,501),metavar='10..500',help='Explicit limit for a controlled A/B run')
    c.add_argument('--model-root',help='Explicit shared model root for cache-preserving executable comparisons')
    c.add_argument('--model-directory',help='Explicit pose/ReID model directory; recorded in manifest')
    c.add_argument('--pose-model',help='Pose model filename within the explicit directory')
    c.add_argument('--frozen-keys',type=Path,help='Smoke-only replay of explicitly recorded legacy window-cache keys; never a new-model inference benchmark')
    c.set_defaults(function=capture)
    r = sub.add_parser('review')
    r.add_argument('--capture', type=Path, required=True)
    r.add_argument('--output', type=Path, required=True)
    r.add_argument('--overview', action='store_true')
    r.add_argument('--baseline',type=Path,help='Review only newly selected pair intervals; preserve original pair labels')
    r.add_argument('--pair',type=int,action='append',help='Inspect particular result indices')
    r.set_defaults(function=review)
    a=sub.add_parser('annotate')
    a.add_argument('--capture',type=Path,required=True)
    a.add_argument('--review',type=Path,required=True)
    a.add_argument('--output',type=Path,required=True)
    a.add_argument('--tracks',type=Path,action='append',help='Cold tracker snapshots; repeat for multiple sources')
    a.set_defaults(function=annotate)
    args = parser.parse_args()
    args.function(args)


if __name__ == '__main__':
    main()
