"""Compare decoded floating-point audio after exact trimming; private fixtures only."""
import argparse
import hashlib
import json
import struct
import subprocess
from pathlib import Path


def run(ffmpeg, args):
    result = subprocess.run([ffmpeg, "-hide_banner", "-nostdin", "-loglevel", "error", *args],
                            capture_output=True, timeout=60)
    if result.returncode:
        raise RuntimeError(result.stderr.decode(errors="replace")[-4000:])
    return result.stdout


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--ffmpeg", required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=False)
    source = args.output / "source.m4a"
    run(args.ffmpeg, ["-f", "lavfi", "-i",
        "aevalsrc=0.21*sin(2*PI*997*t)+0.071*sin(2*PI*1777*t):s=48000:d=3",
        "-c:a", "aac", "-b:a", "192k", str(source)])
    audio_filter = "atrim=start=0.715:end=1.715,asetpts=PTS-STARTPTS"
    expected = run(args.ffmpeg, ["-i", str(source), "-af", audio_filter,
        "-c:a", "pcm_f32le", "-f", "f32le", "-"])
    report = {"scope": "decoded float samples and sample count after exact trim; synthetic AAC",
              "expectedSamples": len(expected) // 4, "cases": []}
    for name, codec, options in (("alac-default", "alac", []),
                                  ("alac-32", "alac", ["-bits_per_raw_sample", "32"]),
                                  ("pcm-float", "pcm_f32le", [])):
        output = args.output / (name + ".mov")
        case = {"name": name, "codec": codec}
        try:
            # Decode the preceding audio packets before trimming. Seeking
            # directly to the trim boundary skips samples in a partial packet.
            run(args.ffmpeg, ["-copyts", "-ss", "0", "-t", "1.965", "-i", str(source),
                "-af", audio_filter, "-c:a", codec, *options, str(output)])
            actual = run(args.ffmpeg, ["-i", str(output), "-c:a", "pcm_f32le", "-f", "f32le", "-"])
            a = struct.unpack("<" + "f" * (len(expected) // 4), expected)
            b = struct.unpack("<" + "f" * (len(actual) // 4), actual)
            case.update(actualSamples=len(b), exactSamples=actual == expected,
                        maxAbsoluteError=max((abs(x-y) for x, y in zip(a, b)), default=0),
                        expectedSha256=hashlib.sha256(expected).hexdigest(),
                        actualSha256=hashlib.sha256(actual).hexdigest())
        except (RuntimeError, subprocess.TimeoutExpired) as error:
            case.update(exactSamples=False, error=str(error))
        report["cases"].append(case)
    (args.output / "report.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
