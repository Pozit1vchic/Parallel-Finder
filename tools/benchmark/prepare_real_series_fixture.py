"""Create local, disposable series controls from the supplied Kyle/Butcher videos.

Pixels remain in the requested output folder. Originals are read only. The first
eight episodes deliberately share a strong face; additions sort before the saved
anchor and contain an 8%-duration target respectively. This controls cache/choice
behavior, not general cross-costume recognition accuracy.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--kyle', type=Path, required=True)
    parser.add_argument('--butcher', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--ffmpeg', default='ffmpeg')
    args = parser.parse_args()
    for source in (args.kyle, args.butcher):
        if not source.is_file():
            parser.error(f'Missing source: {source}')
    root = args.output.resolve()
    if root.exists():
        parser.error('Output must be a new directory; existing evidence is never overwritten')
    root.mkdir(parents=True)

    def encode(source, start, duration, target, scale):
        subprocess.run([args.ffmpeg, '-hide_banner', '-loglevel', 'error', '-ss', str(start),
                        '-i', str(source.resolve()), '-t', str(duration), '-vf', scale + ',fps=12',
                        '-an', '-c:v', 'libx264', '-preset', 'fast', '-crf', '18', '-n', str(target)], check=True)

    anchor = root / '05-якорь.mp4'
    background = root / 'background.mp4'
    encode(args.kyle, 0, 3, anchor, 'scale=1280:720')
    encode(args.butcher, 10, 34.5, background, 'scale=1280:-2,pad=1280:720:0:(oh-ih)/2')
    initial = [anchor]
    for index in range(6, 13):
        destination = root / f'{index:02}-эпизод.mp4'
        shutil.copyfile(anchor, destination)
        initial.append(destination)
    earlier = root / '00-追加.mp4'
    shutil.copyfile(anchor, earlier)
    minority = root / '13-меньшинство.mp4'
    # Relative fixed generated names avoid concat escaping of arbitrary paths.
    (root / 'concat.txt').write_text("file 'background.mp4'\nfile '05-якорь.mp4'\n", encoding='utf-8')
    subprocess.run([args.ffmpeg, '-hide_banner', '-loglevel', 'error', '-f', 'concat', '-safe', '0',
                    '-i', 'concat.txt', '-c', 'copy', '-n', minority.name], cwd=root, check=True)

    def digest(path):
        h = hashlib.sha256()
        with path.open('rb') as stream:
            for block in iter(lambda: stream.read(1024 * 1024), b''):
                h.update(block)
        return h.hexdigest()

    manifest = {'schema': 1, 'anchor': anchor.as_posix(), 'anchorCandidateId': '1',
                'initialSources': [p.as_posix() for p in initial],
                'addedSources': [earlier.as_posix(), minority.as_posix()],
                'cacheRoot': (root / 'cache').as_posix(),
                'minorityTargetRange': {'source': minority.as_posix(), 'start': 34.5, 'end': 37.5,
                                        'targetDurationFraction': .08},
                'provenance': [{'source': p.resolve().as_posix(), 'sha256': digest(p)}
                               for p in (args.kyle, args.butcher)],
                'generated': [{'path': p.as_posix(), 'sha256': digest(p)}
                              for p in initial + [earlier, minority]],
                'limits': 'Repeated real clip controls persistence and strong-face assignment; not broad identity accuracy.'}
    (root / 'manifest.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2), encoding='utf-8')
    print(root / 'manifest.json')


if __name__ == '__main__':
    main()
