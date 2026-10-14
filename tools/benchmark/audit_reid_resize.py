"""Compare current nearest crop sampling with bilinear sampling on fixed frames.

This is a preprocessing diagnostic, not identity ground truth. Bilinear here
uses OpenCV without PIL antialias; it does not claim exact upstream parity.
"""
import argparse
import json
from pathlib import Path
import cv2
import numpy as np
import onnxruntime as ort


def normalized(vector):
    vector = np.asarray(vector, dtype=np.float64).ravel()
    norm = np.linalg.norm(vector)
    if not np.isfinite(vector).all() or norm <= 1e-12:
        raise ValueError('Invalid model descriptor')
    return vector / norm


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--video', type=Path, required=True)
    parser.add_argument('--tracks', type=Path, required=True)
    parser.add_argument('--model', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--different-model', action='store_true',
                        help='Do not compare descriptors across incompatible model spaces')
    args = parser.parse_args()
    if args.output.exists():
        raise FileExistsError(args.output)
    tracks = json.loads(args.tracks.read_text(encoding='utf-8-sig'))['tracks']
    options = ort.SessionOptions()
    options.intra_op_num_threads = 1
    session = ort.InferenceSession(str(args.model), options, providers=['CPUExecutionProvider'])
    input_info = session.get_inputs()[0]
    _, _, target_h, target_w = input_info.shape
    if not isinstance(target_h, int) or not isinstance(target_w, int):
        raise ValueError('Audit requires fixed NCHW input')
    capture = cv2.VideoCapture(str(args.video))
    fps = capture.get(cv2.CAP_PROP_FPS)
    frames = []
    while True:
        ok, frame = capture.read()
        if not ok:
            break
        frames.append(cv2.cvtColor(frame, cv2.COLOR_BGR2RGB))
        if len(frames) > 2000:
            raise ValueError('Audit restricted to a short development clip')
    capture.release()
    if not frames or fps <= 0:
        raise ValueError('Cannot decode source')
    mean = np.array([.485, .456, .406], dtype=np.float32)
    std = np.array([.229, .224, .225], dtype=np.float32)
    rows = []
    prototypes = {}
    for track in tracks:
        samples = {'recorded': [], 'nearest': [], 'bilinear': []}
        for observation in track['observations']:
            if not observation['body']:
                continue
            frame = frames[min(len(frames)-1, round(observation['time'] * fps))]
            h, w = frame.shape[:2]
            box = observation['box']
            left, top, right, bottom = map(np.float32, box)
            left = np.float32(np.clip(left, 0, w-1))
            top = np.float32(np.clip(top, 0, h-1))
            right = np.float32(np.clip(right, left+1, w))
            bottom = np.float32(np.clip(bottom, top+1, h))
            xs = (np.arange(target_w, dtype=np.float32)+np.float32(.5)) * (right-left) / target_w + left
            ys = (np.arange(target_h, dtype=np.float32)+np.float32(.5)) * (bottom-top) / target_h + top
            nearest = frame[np.clip(ys.astype(int), 0, h-1)[:, None],
                            np.clip(xs.astype(int), 0, w-1)[None, :]].astype(np.float32)
            mx, my = np.meshgrid(xs-.5, ys-.5)
            bilinear = cv2.remap(frame.astype(np.float32), mx, my,
                                 cv2.INTER_LINEAR, borderMode=cv2.BORDER_REPLICATE)
            embeddings = {}
            for name, pixels in [('nearest', nearest), ('bilinear', bilinear)]:
                tensor = np.ascontiguousarray(((pixels/255-mean)/std).transpose(2, 0, 1)[None])
                embeddings[name] = normalized(session.run(None, {input_info.name: tensor})[0])
                samples[name].append(embeddings[name])
            recorded = normalized(observation['body'])
            samples['recorded'].append(recorded)
            rows.append(dict(track=track['id'], time=observation['time'],
                             recordedNearest=(None if args.different_model else
                                              float(recorded @ embeddings['nearest'])),
                             nearestBilinear=float(embeddings['nearest'] @ embeddings['bilinear'])))
        if samples['recorded']:
            prototypes[track['id']] = {name: normalized(np.sum(values, axis=0))
                                       for name, values in samples.items()}
    anchors = [t['id'] for t in tracks if t['sourceLead'] and t['id'] in prototypes]
    pairs = [dict(track=track, anchor=anchor,
                  **{name: float(prototypes[track][name] @ prototypes[anchor][name])
                     for name in ('recorded', 'nearest', 'bilinear')})
             for track in prototypes for anchor in anchors if track != anchor]
    with args.output.open('x', encoding='utf-8') as destination:
        json.dump(dict(scope='short-clip resize diagnostic; not accuracy or PIL parity',
                       recordedDescriptorComparable=not args.different_model,
                       video=str(args.video.resolve()), model=str(args.model.resolve()),
                       observations=rows, pairs=pairs), destination, indent=2)


if __name__ == '__main__':
    main()
