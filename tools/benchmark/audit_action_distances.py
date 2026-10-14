"""Compare observed limb-distance trajectories; development diagnostics only."""
import argparse
import json
import math
from pathlib import Path

CHAINS = ((5, 9), (6, 10), (11, 15), (12, 16))
JOINTS = ((5, 7, 9), (6, 8, 10), (11, 13, 15), (12, 14, 16))
FEATURES = ('left_arm_reach', 'right_arm_reach', 'left_leg_reach', 'right_leg_reach',
            'left_elbow_bend', 'right_elbow_bend', 'left_knee_bend', 'right_knee_bend')


def joint_bend(points, chain):
    if any(len(points[i]) < 3 or not all(math.isfinite(float(v)) for v in points[i][:3])
           or points[i][2] < .5 for i in chain):
        return None
    a, center, b = (points[i] for i in chain)
    u = (a[0]-center[0], a[1]-center[1])
    v = (b[0]-center[0], b[1]-center[1])
    denominator = math.hypot(*u)*math.hypot(*v)
    if denominator < 1e-8:
        return None
    # Straight limbs approach zero; flexion approaches one. This remains
    # projected evidence, not a claim of recovered three-dimensional motion.
    return (1+max(-1., min(1., sum(x*y for x,y in zip(u,v))/denominator)))/2


def describe(case):
    rows = []
    times = []
    for frame in case['frames']:
        time = float(frame['time'])
        if not math.isfinite(time) or (times and time <= times[-1]):
            raise ValueError('Observations must have finite, strictly increasing timestamps')
        times.append(time)
        p = frame['points']
        if len(p) != 17 or any(p[i][2] < .5 for i in (5, 6, 11, 12)):
            rows.append([None]*len(FEATURES))
            continue
        scale = math.hypot((p[5][0]+p[6][0]-p[11][0]-p[12][0])/2,
                           (p[5][1]+p[6][1]-p[11][1]-p[12][1])/2)
        if scale < 1e-8:
            rows.append([None]*len(FEATURES))
            continue
        rows.append([math.dist(p[a][:2], p[b][:2])/scale
                     if min(p[a][2], p[b][2]) >= .5 else None for a, b in CHAINS]
                    + [joint_bend(p, chain) for chain in JOINTS])
    return rows, times


def compare(left, right):
    a, ta = left
    b, tb = right
    channels = []
    for channel in range(len(FEATURES)):
        # Select nearest actual timestamps; missing observations retain their slots.
        def nearest(times, fraction):
            target = times[0]+fraction*(times[-1]-times[0])
            return min(range(len(times)), key=lambda index:abs(times[index]-target))
        pairs = [(a[nearest(ta,i/15)][channel],
                  b[nearest(tb,i/15)][channel]) for i in range(16)] if a and b else []
        pairs = [(x, y) for x, y in pairs if x is not None and y is not None]
        if len(pairs) < 8:
            channels.append({'supported': False})
            continue
        x, y = zip(*pairs)
        mx, my = sum(x)/len(x), sum(y)/len(y)
        dx, dy = [v-mx for v in x], [v-my for v in y]
        denominator = math.sqrt(sum(v*v for v in dx)*sum(v*v for v in dy))
        channels.append(dict(supported=True, samples=len(pairs),
                             ranges=[max(x)-min(x), max(y)-min(y)],
                             correlation=sum(u*v for u,v in zip(dx,dy))/denominator
                             if denominator > 1e-9 else None))
    return channels


def align(left, right):
    """Monotone bounded-warp alignment, requiring all observed active channels."""
    a, ta = left
    b, tb = right
    if len(a) < 8 or len(b) < 8:
        return {'supported': False}
    if any(y-x > .25 for times in (ta,tb) for x,y in zip(times,times[1:])):
        return {'supported': False, 'reason': 'observation_gap'}
    active = []
    scaled = [[], []]
    for c in range(len(FEATURES)):
        values = [[row[c] for row in seq if row[c] is not None] for seq in (a, b)]
        if any(len(v) < .8*len(seq) for v, seq in zip(values, (a, b))):
            continue
        ranges = [max(v)-min(v) for v in values]
        if min(ranges) < .12:
            continue
        active.append(c)
        for side, seq in enumerate((a, b)):
            center = sum(values[side])/len(values[side])
            scaled[side].append([(row[c]-center)/ranges[side] if row[c] is not None else None
                                 for row in seq])
    if not active:
        return {'supported': False}
    n, m = len(a), len(b)
    dp = {(0, 0): (0., 0)}
    for i in range(1, n+1):
        for j in range(1, m+1):
            if abs((ta[i-1]-ta[0])/(ta[-1]-ta[0])-(tb[j-1]-tb[0])/(tb[-1]-tb[0])) > .25:
                continue
            pairs = [(x[i-1], y[j-1]) for x,y in zip(*scaled)]
            if any(x is None or y is None for x,y in pairs):
                continue
            cost = sum(abs(x-y) for x,y in pairs)/len(pairs)
            previous = [dp[p] for p in ((i-1,j-1),(i-1,j),(i,j-1)) if p in dp]
            if previous:
                total, count = min(previous, key=lambda v:v[0])
                dp[i,j] = (total+cost,count+1)
    if (n,m) not in dp:
        return {'supported': False, 'channels': active}
    total, count = dp[n,m]
    return dict(supported=True, channels=active, meanDistance=total/count, pathLength=count)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--fixtures', nargs='+', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    cases = []
    for path in args.fixtures:
        data = json.loads(path.read_text(encoding='utf-8-sig'))
        cases.extend(data['cases'])
    descriptors = [describe(case) for case in cases]
    rows = [dict(left=cases[i]['label'], right=cases[j]['label'],
                 channels=compare(descriptors[i], descriptors[j]),
                 phaseAlignment=align(descriptors[i], descriptors[j]))
            for i in range(len(cases)) for j in range(i, len(cases))]
    with args.output.open('x', encoding='utf-8') as output:
        json.dump(dict(scope='Uncalibrated projected limb distances; no identity or acceptance decision',
                       features=FEATURES,
                       comparisons=rows), output, indent=2)


if __name__ == '__main__':
    main()
