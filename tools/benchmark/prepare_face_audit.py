"""Extract exact observed frames/crops for the application's face probe.

Only already measured faces are replayed; this neither labels identity nor
fabricates face observations. Output is private development evidence.
"""
import argparse
import json
from pathlib import Path
import re
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--tracks', type=Path, required=True)
    parser.add_argument('--track-ids', type=int, nargs='+', required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    snapshot = json.loads(args.tracks.read_text(encoding='utf-8-sig'))
    source = snapshot['source']
    observations = [(t['id'], o) for t in snapshot['tracks']
                    if t['id'] in args.track_ids for o in t['observations'] if o['face']]
    if not observations or len(observations) > 64:
        raise ValueError('Audit requires 1–64 measured face observations')
    info = json.loads(subprocess.run(['D:/msys2/ucrt64/bin/ffprobe.exe', '-v', 'error',
                     '-select_streams', 'v:0', '-show_entries', 'stream=width,height',
                     '-of', 'json', source], capture_output=True, check=True).stdout)['streams'][0]
    # This replay uses unchanged inference pixels for small sources only.
    # Larger sources require the exact application's resize, not a substitute.
    if info['width'] > 1280 or info['height'] > 720:
        raise ValueError('Exact native-pixel replay is limited to small sources')
    args.output.mkdir(parents=True, exist_ok=False)
    samples = []
    for index, (track, observation) in enumerate(observations):
        path = args.output / f'frame-{index:03d}.png'
        result = subprocess.run(['D:/msys2/ucrt64/bin/ffmpeg.exe', '-hide_banner',
                     '-loglevel', 'info', '-ss', str(observation['time']), '-copyts',
                     '-threads', '2', '-i', source, '-vf', 'showinfo', '-frames:v', '1',
                     '-n', str(path)], capture_output=True, check=True)
        actual = re.search(r'pts_time:([\d.eE+-]+)', result.stderr.decode(errors='replace'))
        if not actual or abs(float(actual[1])-observation['time']) > 1e-5:
            raise ValueError('Decoded PTS differs from the measured observation')
        samples.append(dict(id=index, track=track, time=observation['time'],
                            image=str(path.resolve()), box=observation['box'],
                            observedFace=observation['face']))
    (args.output / 'samples.json').write_text(json.dumps(samples), encoding='utf-8')


if __name__ == '__main__':
    main()
