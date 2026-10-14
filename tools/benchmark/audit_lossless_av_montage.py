"""Audit joint exact video/audio trims and two distinct lossless montage clips."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess


def run(program, args):
    result = subprocess.run([program, '-hide_banner', '-nostdin', '-loglevel', 'error',
                             *args], capture_output=True, timeout=90)
    if result.returncode:
        raise RuntimeError(result.stderr.decode(errors='replace')[-4000:])
    return result.stdout


def frame_hashes(ffmpeg, source, pixel_format, options=()):
    data = run(ffmpeg, ['-i', str(source), *options, '-an', '-pix_fmt', pixel_format,
                       '-fps_mode', 'passthrough', '-f', 'framemd5', '-'])
    return [line.rsplit(',', 1)[-1].strip() for line in data.decode().splitlines()
            if line and not line.startswith('#')]


def audio(ffmpeg, source, options=()):
    return run(ffmpeg, ['-i', str(source), *options, '-vn', '-c:a', 'pcm_f64le',
                       '-f', 'f64le', '-'])


def frame_times(ffmpeg, source):
    probe = str(Path(ffmpeg).with_name('ffprobe.exe' if Path(ffmpeg).suffix == '.exe' else 'ffprobe'))
    result = subprocess.run([probe, '-v', 'error', '-select_streams', 'v:0',
        '-show_frames', '-show_entries', 'frame=best_effort_timestamp_time', '-of', 'json',
        str(source)], capture_output=True, check=True, timeout=60)
    return [float(frame['best_effort_timestamp_time']) for frame in json.loads(result.stdout)['frames']]


def same_times(actual, expected):
    return len(actual) == len(expected) and all(abs(a-b) <= 2e-6 for a, b in zip(actual, expected))


def audio_timeline(ffmpeg, source, duration):
    probe = str(Path(ffmpeg).with_name('ffprobe.exe' if Path(ffmpeg).suffix == '.exe' else 'ffprobe'))
    result = subprocess.run([probe, '-v', 'error', '-select_streams', 'a:0',
        '-show_packets', '-show_entries', 'packet=pts_time,duration_time', '-of', 'json',
        str(source)], capture_output=True, check=True, timeout=60)
    packets = json.loads(result.stdout)['packets']
    spans = [(float(p['pts_time']), float(p['duration_time'])) for p in packets]
    max_gap = max((abs(current[0] - (previous[0] + previous[1]))
                   for previous, current in zip(spans, spans[1:])), default=0)
    end = spans[-1][0]+spans[-1][1] if spans else None
    return {'start': spans[0][0] if spans else None, 'end': end, 'maxGapOrOverlap': max_gap,
            'continuousExpectedRange': bool(spans) and abs(spans[0][0])<=2e-6 and
            abs(end-duration)<=2e-6 and max_gap<=2e-6}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--ffmpeg', required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=False)
    report = {'scope': 'Synthetic joint AV; two different fractional intervals, exact pixels/samples',
              'afterEffectsVerified': False, 'cases': []}
    for pixel_format in ['yuv420p', 'yuv420p10le']:
        source = args.output / (pixel_format + '.mkv')
        run(args.ffmpeg, ['-f', 'lavfi', '-i',
            'testsrc2=size=320x180:rate=30:duration=3,format=' + pixel_format,
            '-f', 'lavfi', '-i', 'aevalsrc=0.3*sin(2*PI*(997*t+31*t*t)):s=48000:d=3',
            '-c:v', 'ffv1', '-c:a', 'pcm_f64le', str(source)])
        for encoder in ['libx264', 'hevc_nvenc']:
            case = {'pixelFormat': pixel_format, 'encoder': encoder, 'clips': []}
            expected_video, expected_audio, outputs, expected_times = [], b'', [], []
            source_times = frame_times(args.ffmpeg, source)
            try:
                for index, start in enumerate([0.715, 1.715]):
                    end = start + 1
                    vf = f'trim=start={start}:end={end},setpts=PTS-{start}/TB'
                    af = f'atrim=start={start}:end={end},asetpts=PTS-{start}/TB'
                    expected_frames = frame_hashes(args.ffmpeg, source, pixel_format, ['-vf', vf])
                    expected_samples = audio(args.ffmpeg, source, ['-af', af])
                    expected_video.extend(expected_frames)
                    expected_audio += expected_samples
                    clip_times = [t-start for t in source_times if start <= t < end]
                    expected_times.extend(t+index for t in clip_times)
                    output = args.output / f'{pixel_format}-{encoder}-{index}.mov'
                    codec_options = ['-preset', 'veryfast', '-qp', '0'] if encoder == 'libx264' else [
                        '-preset', 'p5', '-tune', 'lossless']
                    run(args.ffmpeg, ['-copyts', '-ss', str(max(0, start-.5)), '-t', '1.75',
                        '-i', str(source), '-vf', vf, '-af', af, '-c:v', encoder, *codec_options,
                        '-pix_fmt', pixel_format, '-fps_mode', 'passthrough', '-enc_time_base:v',
                        'demux', '-c:a', 'pcm_f64le', '-t', '1', str(output)])
                    frames = frame_hashes(args.ffmpeg, output, pixel_format)
                    samples = audio(args.ffmpeg, output)
                    case['clips'].append({'expectedFrames': len(expected_frames),
                        'actualFrames': len(frames), 'exactVideo': frames == expected_frames,
                        'sourceVideoOffsetPreserved': same_times(frame_times(args.ffmpeg, output), clip_times),
                        'audioTimeline': audio_timeline(args.ffmpeg, output, 1),
                        'expectedSamples': len(expected_samples)//8, 'actualSamples': len(samples)//8,
                        'exactAudio': samples == expected_samples})
                    outputs.append(output)
                listing = args.output / f'{pixel_format}-{encoder}.concat.txt'
                listing.write_text(''.join("file '" + path.resolve().as_posix().replace("'", "'\\''") +
                    "'\nduration 1\n" for path in outputs), encoding='utf-8')
                montage = args.output / f'{pixel_format}-{encoder}-montage.mov'
                run(args.ffmpeg, ['-f', 'concat', '-safe', '0', '-i', str(listing),
                                 '-map', '0:v:0', '-map', '0:a:0', '-c', 'copy', str(montage)])
                frames = frame_hashes(args.ffmpeg, montage, pixel_format)
                samples = audio(args.ffmpeg, montage)
                case.update(montageFrames=len(frames), montageSamples=len(samples)//8,
                    exactMontageVideo=frames == expected_video, exactMontageAudio=samples == expected_audio,
                    montageVideoTimesPreserved= same_times(frame_times(args.ffmpeg, montage), expected_times),
                    montageAudioTimeline=audio_timeline(args.ffmpeg, montage, 2),
                    expectedAudioSha256=hashlib.sha256(expected_audio).hexdigest(),
                    actualAudioSha256=hashlib.sha256(samples).hexdigest())
            except (RuntimeError, subprocess.TimeoutExpired) as error:
                case['error'] = str(error)
            report['cases'].append(case)
    (args.output / 'report.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
