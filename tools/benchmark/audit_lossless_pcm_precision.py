"""Check exact decoded samples through MOV PCM, including 32-bit integer detail.

Private generated fixtures only; never overwrite a source or existing report.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import struct
import subprocess


def run(executable, arguments):
    result = subprocess.run([executable, '-hide_banner', '-nostdin', '-loglevel',
                             'error', *arguments], capture_output=True, timeout=60)
    if result.returncode:
        raise RuntimeError(result.stderr.decode(errors='replace')[-4000:])
    return result.stdout


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--ffmpeg', required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--audio-source', type=Path)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=False)
    cases = []
    for fmt, codec, packing in [('s32le', 'pcm_s32le', 'i'),
                                ('f32le', 'pcm_f32le', 'f'),
                                ('f64le', 'pcm_f64le', 'd')]:
        # Integer low bits deliberately exercise precision lost by float32.
        values = [((n * 1664525 + 1013904223) % 4294967296) - 2147483648
                  if packing == 'i' else .31 * math.sin(n * .131) + 1e-10
                  for n in range(144000)]
        raw = args.output / (fmt + '.raw')
        raw.write_bytes(struct.pack('<' + packing * len(values), *values))
        source = args.output / (fmt + '.wav')
        run(args.ffmpeg, ['-f', fmt, '-ar', '48000', '-ac', '1', '-i', str(raw),
                         '-c:a', codec, str(source)])
        trim = 'atrim=start=0.715:end=1.715,asetpts=PTS-STARTPTS'
        expected = run(args.ffmpeg, ['-i', str(source), '-af', trim,
                                    '-c:a', 'pcm_f64le', '-f', 'f64le', '-'])
        for destination_codec in ['pcm_f32le', 'pcm_f64le']:
            output = args.output / (fmt + '-' + destination_codec + '.mov')
            case = {'sourceFormat': fmt, 'destinationCodec': destination_codec}
            try:
                run(args.ffmpeg, ['-copyts', '-ss', '0.215', '-t', '1.75',
                    '-i', str(source), '-af', trim, '-c:a', destination_codec, str(output)])
                actual = run(args.ffmpeg, ['-i', str(output), '-c:a', 'pcm_f64le',
                                          '-f', 'f64le', '-'])
                case.update(expectedSamples=len(expected) // 8,
                            actualSamples=len(actual) // 8, exactSamples=actual == expected,
                            expectedSha256=hashlib.sha256(expected).hexdigest(),
                            actualSha256=hashlib.sha256(actual).hexdigest())
            except (RuntimeError, subprocess.TimeoutExpired) as error:
                case.update(exactSamples=False, error=str(error))
            cases.append(case)
    if args.audio_source:
        source = args.audio_source.resolve(strict=True)
        trim = 'atrim=start=0.715:end=1.715,asetpts=PTS-STARTPTS'
        expected = run(args.ffmpeg, ['-t', '2', '-i', str(source), '-map', '0:a:0',
            '-af', trim, '-c:a', 'pcm_f64le', '-f', 'f64le', '-'])
        output = args.output / 'real-source-pcm-f64.mov'
        run(args.ffmpeg, ['-copyts', '-ss', '0.215', '-t', '1.75', '-i', str(source),
            '-map', '0:a:0', '-af', trim, '-c:a', 'pcm_f64le', str(output)])
        actual = run(args.ffmpeg, ['-i', str(output), '-c:a', 'pcm_f64le', '-f', 'f64le', '-'])
        cases.append({'source': str(source), 'destinationCodec': 'pcm_f64le',
                      'expectedScalarSamples': len(expected) // 8,
                      'actualScalarSamples': len(actual) // 8,
                      'exactSamples': bool(expected) and actual == expected,
                      'expectedSha256': hashlib.sha256(expected).hexdigest(),
                      'actualSha256': hashlib.sha256(actual).hexdigest()})
    report = {'scope': 'Exact fractional trim; decoded f64 samples, mono synthetic plus optional source',
              'cases': cases}
    (args.output / 'report.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
