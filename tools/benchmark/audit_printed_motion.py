"""Measure relative optical-flow residuals for reviewed person/image controls.

Research diagnostic only: a low residual does not identify a printed image.
Requires local OpenCV, NumPy, FFmpeg, the supplied video and captured tracks.
"""
import argparse
import json
from pathlib import Path
import subprocess

import cv2
import numpy as np


def decode(ffmpeg, video, time):
    result = subprocess.run([str(ffmpeg), '-loglevel', 'error', '-ss', str(time),
        '-i', str(video), '-frames:v', '1', '-vf', 'scale=640:272',
        '-f', 'image2pipe', '-vcodec', 'png', 'pipe:1'],
        check=True, capture_output=True)
    image = cv2.imdecode(np.frombuffer(result.stdout, np.uint8), cv2.IMREAD_GRAYSCALE)
    if image is None or image.shape != (272, 640):
        raise ValueError('Failed to decode the requested frame')
    return image


def measure(first, last, box):
    x1, y1, x2, y2 = np.asarray(box, dtype=float) / 2
    x1, x2 = int(max(0, x1)), int(min(640, x2))
    y1, y2 = int(max(0, y1)), int(min(272, y2))
    if x2 <= x1 or y2 <= y1:
        return {'status': 'invalid_box'}
    points = cv2.goodFeaturesToTrack(first, maxCorners=1200, qualityLevel=.01,
                                    minDistance=4, blockSize=5)
    if points is None:
        return {'status': 'insufficient_features'}
    moved, status, error = cv2.calcOpticalFlowPyrLK(first, last, points, None,
                                                   winSize=(21, 21), maxLevel=3)
    if moved is None or status is None or error is None:
        return {'status': 'optical_flow_failed'}
    valid = (status[:, 0] > 0) & (error[:, 0] < 20)
    original, moved = points[:, 0, :][valid], moved[:, 0, :][valid]
    inside = ((original[:, 0] >= x1) & (original[:, 0] < x2)
              & (original[:, 1] >= y1) & (original[:, 1] < y2))
    inner_count, outer_count = int(inside.sum()), int((~inside).sum())
    if inner_count < 5 or outer_count < 12:
        return {'status': 'insufficient_features', 'inside': inner_count, 'outside': outer_count}
    transform, inliers = cv2.estimateAffinePartial2D(original[~inside], moved[~inside],
        method=cv2.RANSAC, ransacReprojThreshold=2., maxIters=3000)
    if transform is None or inliers is None:
        return {'status': 'transform_failed'}
    predicted = original[inside] @ transform[:, :2].T + transform[:, 2]
    residual = np.linalg.norm(moved[inside] - predicted, axis=1)
    return {'status': 'measured', 'inside': inner_count, 'outside': outer_count,
            'backgroundInlierFraction': float(inliers.mean()),
            'medianResidualPerBoxDiagonal': float(np.median(residual) / np.hypot(x2-x1, y2-y1))}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('video', 'tracks', 'controls', 'ffmpeg', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    if args.output.exists():
        parser.error('Output must not already exist')
    tracks = {str(t['id']): t for t in json.loads(args.tracks.read_text(encoding='utf-8-sig'))['tracks']}
    controls = json.loads(args.controls.read_text(encoding='utf-8-sig'))['controls']
    rows = []
    for control in controls:
        track = tracks.get(control['track'])
        if track is None:
            raise ValueError('Control track missing: ' + control['id'])
        samples = [o for o in track['observations'] if abs(o['time'] - control['time']) <= 1]
        row = {'id': control['id'], 'expected': control['expected']}
        if len(samples) < 2:
            row['status'] = 'insufficient_frames'
        else:
            a, b = samples[0], samples[-1]
            row.update(firstTime=a['time'], lastTime=b['time'])
            row.update(measure(decode(args.ffmpeg, args.video, a['time']),
                               decode(args.ffmpeg, args.video, b['time']), a['box']))
        rows.append(row)
    args.output.write_text(json.dumps({'scope': __doc__, 'controls': rows}, indent=2), encoding='utf-8')
    print(json.dumps({'controls': len(rows), 'measured': sum(r['status'] == 'measured' for r in rows)}))


if __name__ == '__main__':
    main()
