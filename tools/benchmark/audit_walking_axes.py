"""Read-only diagnostics of measured foot axes; not an action/identity label."""
import argparse
import json
import math
from pathlib import Path


def trace(values, prominence):
    filtered = list(values)
    for i in range(1, len(values)-1):
        filtered[i] = sorted(values[i-1:i+2])[1]
    n = min(3, max(1, len(values)//4))
    first = sorted(filtered[:n])[n//2]
    extreme = first
    sign = reversals = 0
    for value in filtered:
        if sign == 0:
            if abs(value-first) >= prominence:
                sign = 1 if value > first else -1
                extreme = value
        elif (value-extreme)*sign >= 0:
            extreme = value
        elif (extreme-value)*sign >= prominence:
            reversals += 1
            sign = -sign
            extreme = value
    return dict(smoothedRange=max(filtered)-min(filtered), reversals=reversals)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--windows', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    windows = json.loads(args.windows.read_text(encoding='utf-8-sig'))
    rows = []
    for window in windows:
        values = []
        for frame in window['frames']:
            points = frame['points']
            if len(points) != 17 or any(points[i][2] < .35 for i in (5, 6, 11, 12, 13, 14, 15, 16)):
                continue
            shoulder = [(points[5][i] + points[6][i]) / 2 for i in (0, 1)]
            hip = [(points[11][i] + points[12][i]) / 2 for i in (0, 1)]
            down = [hip[i] - shoulder[i] for i in (0, 1)]
            scale = math.hypot(*down)
            if scale <= 1e-8:
                continue
            down = [v / scale for v in down]
            local = {}
            for joint in (13, 14, 15, 16):
                x, y = [points[joint][i] - shoulder[i] for i in (0, 1)]
                local[joint] = ((x * down[1] - y * down[0]) / scale,
                                (x * down[0] + y * down[1]) / scale)
            values.append(local)
        axes = {}
        for axis, name in enumerate(('horizontal', 'vertical')):
            left = [v[15][axis] for v in values]
            right = [v[16][axis] for v in values]
            if not left:
                continue
            mean_left, mean_right = sum(left) / len(left), sum(right) / len(right)
            a, b = [v - mean_left for v in left], [v - mean_right for v in right]
            denominator = math.sqrt(sum(v*v for v in a) * sum(v*v for v in b))
            axes[name] = dict(leftRange=max(left)-min(left), rightRange=max(right)-min(right),
                              correlation=sum(x*y for x, y in zip(a, b))/denominator if denominator > 1e-12 else None,
                              leftTrace=trace(left, .075 if axis else .15),
                              rightTrace=trace(right, .075 if axis else .15))
        knees = {str(joint): trace([v[joint][1] for v in values], .15) for joint in (13, 14)} if values else {}
        rows.append(dict(source=window['source'], samples=len(values), supplied=len(window['frames']), axes=axes, knees=knees))
    with args.output.open('x', encoding='utf-8') as output:
        json.dump(dict(scope='raw measured axes, no smoothing or production classification replay', windows=rows), output, indent=2)


if __name__ == '__main__':
    main()
