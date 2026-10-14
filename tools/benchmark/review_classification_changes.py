"""Local contact sheets for changed labels; never modify source or cache."""
import argparse
import json
import subprocess
from pathlib import Path
from PIL import Image, ImageDraw


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--comparison', type=Path, required=True)
    parser.add_argument('--video', type=Path, required=True)
    parser.add_argument('--video-index', type=int, required=True)
    parser.add_argument('--ffmpeg', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(exist_ok=False)
    report = json.loads(args.comparison.read_text(encoding='utf-8-sig'))
    rows = next(row['changes'] for row in report['videos'] if row['video'] == args.video_index)
    sheets = []
    for batch in range(0, len(rows), 4):
        canvas = Image.new('RGB', (6*320, 4*218), '#141414')
        draw = ImageDraw.Draw(canvas)
        for offset, row in enumerate(rows[batch:batch+4]):
            start, end = row['start'], row['end']
            interval = (end-start)/6
            decoded = subprocess.run([
                str(args.ffmpeg), '-v', 'error', '-ss', str(start), '-i', str(args.video),
                '-t', str(end-start), '-vf', f'fps={1/interval},scale=320:180:force_original_aspect_ratio=decrease,pad=320:180:(ow-iw)/2:(oh-ih)/2',
                '-frames:v', '6', '-f', 'rawvideo', '-pix_fmt', 'rgb24', '-an', 'pipe:1'],
                check=True, capture_output=True).stdout
            count = len(decoded)//(320*180*3)
            if count != 6 or len(decoded) != count*320*180*3:
                raise ValueError(f'Expected six source observations, got {count}')
            for index in range(count):
                frame = Image.frombytes('RGB', (320,180), decoded[index*320*180*3:(index+1)*320*180*3])
                canvas.paste(frame, (index*320, offset*218+38))
            draw.text((5,offset*218+3), f"track {row['track']}  {start:.2f}-{end:.2f}  {row['before']} -> {row['after']}", fill='white')
        path = args.output/f'changes-{batch//4+1}.jpg'
        canvas.save(path, quality=92)
        sheets.append(str(path.resolve()))
    with (args.output/'manifest.json').open('x', encoding='utf-8') as out:
        json.dump(dict(source=str(args.video.resolve()), scope='Visual development review; six sampled frames per changed interval', changes=rows, sheets=sheets), out, indent=2)


if __name__ == '__main__':
    main()
