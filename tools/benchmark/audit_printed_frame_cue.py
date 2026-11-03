"""Research-only rectangular-surface cue on labelled local video frames.

Requires OpenCV and FFmpeg; consumes no application state and does not classify
people in the product. A positive means line segments support both sides and
the bottom of a frame around a detection. One static cue cannot establish that
a person in front of a frame is printed on it.
"""
import argparse
import json
import subprocess
from pathlib import Path

import cv2
import numpy as np


def inspect(image, box):
    x1, y1, x2, y2 = box
    width, height = x2 - x1, y2 - y1
    if min(width, height) <= 0:
        raise ValueError('Detection box must have positive area')
    gray = cv2.cvtColor(image, cv2.COLOR_BGR2GRAY)
    edges = cv2.Canny(gray, 40, 120)
    lines = cv2.HoughLinesP(edges, 1, np.pi / 180, 40, minLineLength=60, maxLineGap=12)
    left = right = bottom = False
    for x, y, other_x, other_y in ([] if lines is None else lines.reshape(-1, 4)):
        dx, dy = abs(int(other_x) - int(x)), abs(int(other_y) - int(y))
        mx, my = (int(x) + int(other_x)) / 2, (int(y) + int(other_y)) / 2
        if dy > dx * 5 and dy >= .55 * height and max(y, other_y) >= y1 and min(y, other_y) <= y2:
            left |= abs(mx - x1) <= .25 * width
            right |= abs(mx - x2) <= .25 * width
        if dx > dy * 5 and dx >= .55 * width and abs(my - y2) <= .25 * height:
            # Video-frame borders are not evidence of a picture frame.
            if 3 < my < image.shape[0] - 4 and max(x, other_x) >= x1 and min(x, other_x) <= x2:
                bottom = True
    return {'left': bool(left), 'right': bool(right), 'bottom': bool(bottom),
            'frameCue': bool(left and right and bottom)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--video', type=Path, required=True)
    parser.add_argument('--controls', type=Path, required=True)
    parser.add_argument('--ffmpeg', type=Path, required=True)
    args = parser.parse_args()
    labels = json.loads(args.controls.read_text(encoding='utf-8-sig'))
    width, height = labels['frameWidth'], labels['frameHeight']
    rows = []
    for control in labels['controls']:
        raw = subprocess.check_output([str(args.ffmpeg), '-loglevel', 'error', '-ss', str(control['time']),
            '-i', str(args.video), '-frames:v', '1', '-vf', f'scale={width}:{height}',
            '-f', 'image2pipe', '-vcodec', 'png', 'pipe:1'])
        image = cv2.imdecode(np.frombuffer(raw, np.uint8), cv2.IMREAD_COLOR)
        if image is None or image.shape[:2] != (height, width):
            raise RuntimeError('Could not decode the labelled frame')
        rows.append({'id': control['id'], 'expected': control['expected'], **inspect(image, control['box'])})
    print(json.dumps({'scope': 'single-frame research cue; not a person or portrait detector', 'controls': rows}, indent=2))


if __name__ == '__main__':
    main()
