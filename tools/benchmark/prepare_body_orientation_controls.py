"""Recreate private body-view frames from exact source hashes and frame indices.

Requires OpenCV (see the fixture's version). This never downloads media/models,
changes originals, or overwrites an existing experiment. Pixel hashes distinguish
actual input changes from irrelevant PNG encoder differences.
"""
import argparse
import hashlib
import json
from pathlib import Path


def digest(path):
    hasher = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            hasher.update(block)
    return hasher.hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--kyle', type=Path, required=True)
    parser.add_argument('--butcher', type=Path, required=True)
    parser.add_argument('--fixture', type=Path, default=Path(__file__).resolve().parents[2] / 'tests/fixtures/body-yaw-observations.json')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    import cv2
    fixture = json.loads(args.fixture.read_text(encoding='utf-8'))
    sources = {'KyleCrane_Parallel_NikolayOM.mp4': args.kyle,
               'butcher_parallel_notsorted.mov': args.butcher}
    if args.output.exists():
        parser.error('Output must be a new directory; preserve existing evidence')
    for row in fixture['controls']:
        name = row['image']
        if not name or '/' in name or '\\' in name or Path(name).suffix.lower() != '.png':
            parser.error('Control image must be a PNG basename')
    for item in fixture['sources']:
        source = sources[item['source']]
        if not source.is_file() or digest(source) != item['sha256']:
            parser.error('Source missing or hash differs: ' + str(source))
    args.output.mkdir(parents=True)
    prepared = set()
    for row in fixture['controls']:
        if row['image'] in prepared:
            continue
        video = cv2.VideoCapture(str(sources[row['source']].resolve()))
        try:
            video.set(cv2.CAP_PROP_POS_FRAMES, row['frameIndex'])
            ok, frame = video.read()
            if not ok or round(video.get(cv2.CAP_PROP_POS_FRAMES)) - 1 != row['frameIndex']:
                raise RuntimeError('Cannot recover exact source frame: ' + row['id'])
        finally:
            video.release()
        frame = cv2.resize(frame, (row['width'], row['height']), interpolation=cv2.INTER_LINEAR)
        pixels = cv2.cvtColor(frame, cv2.COLOR_BGR2RGBA)
        if hashlib.sha256(pixels.tobytes()).hexdigest() != row['rgbaSha256']:
            raise RuntimeError('Source-frame pixels differ: ' + row['id'] + '; check decoder/OpenCV version')
        if not cv2.imwrite(str(args.output / row['image']), frame):
            raise RuntimeError('Cannot save source control: ' + row['id'])
        prepared.add(row['image'])
    labels = {key: fixture[key] for key in ('schema', 'annotationMethod', 'yawConvention', 'controls')}
    (args.output / 'labels.json').write_text(json.dumps(labels, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    print(f'Prepared {len(prepared)} exact frame images for {len(fixture["controls"])} controls')


if __name__ == '__main__':
    main()
