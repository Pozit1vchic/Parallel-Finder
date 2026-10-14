"""Development audit: decoded pixels and frame boundaries, not codec labels.

Uses private generated fixtures; never reads or modifies analysis caches.
The report deliberately does not certify audio, montage, or editor support.
"""
import argparse
import json
import subprocess
from pathlib import Path


def run(ffmpeg, args):
    result = subprocess.run([ffmpeg, "-hide_banner", "-nostdin", "-loglevel", "error", *args],
                            capture_output=True, text=True, timeout=120)
    if result.returncode:
        raise RuntimeError(result.stderr[-4000:])
    return result.stdout


def hashes(text):
    return [line.rsplit(",", 1)[-1].strip() for line in text.splitlines()
            if line and not line.startswith("#")]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--ffmpeg", required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--source", type=Path)
    parser.add_argument("--start", type=float, default=0.715)
    parser.add_argument("--pixel-format", default="yuv420p10le")
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=False)
    report = {"scope": "video only; exact decoded pixel hashes and frame count",
              "source": str(args.source) if args.source else "generated 8/10-bit fixtures",
              "audioVerified": False, "montageVerified": False,
              "afterEffectsVerified": False, "cases": []}
    formats = (args.pixel_format,) if args.source else ("yuv420p", "yuv420p10le")
    for pixel_format in formats:
        source = args.source or args.output / (pixel_format + ".mkv")
        if not args.source:
            run(args.ffmpeg, ["-f", "lavfi", "-i",
                "testsrc2=size=320x180:rate=30:duration=3,format=" + pixel_format,
                "-c:v", "ffv1", "-an", str(source)])
        intervals = ((args.start, args.start + 1),) if args.source else ((0.7, 1.7), (0.715, 1.715))
        for start, end in intervals:
            seek_start = max(0, start - 1)
            seek = ["-copyts", "-ss", str(seek_start), "-t", str(end - seek_start + 0.25)] if args.source else []
            expected = hashes(run(args.ffmpeg, [*seek, "-i", str(source), "-vf",
                f"trim=start={start}:end={end},setpts=PTS-STARTPTS",
                "-an", "-pix_fmt", pixel_format, "-fps_mode", "passthrough", "-f", "framemd5", "-"]))
            for encoder in ("libx264", "hevc_nvenc"):
                output = args.output / f"{pixel_format}-{start}-{encoder}.mp4"
                case = {"pixelFormat": pixel_format, "start": start, "end": end,
                        "encoder": encoder, "expectedFrames": len(expected)}
                try:
                    options = (["-preset", "veryfast", "-qp", "0"] if encoder == "libx264"
                               else ["-preset", "p5", "-tune", "lossless"])
                    run(args.ffmpeg, ["-copyts", "-ss", str(start), "-t", str(end - start + 0.25), "-i", str(source),
                        "-vf", f"trim=start={start}:end={end},setpts=PTS-STARTPTS",
                        "-an", "-c:v", encoder,
                        *options, "-pix_fmt", pixel_format, "-fps_mode", "passthrough", str(output)])
                    actual = hashes(run(args.ffmpeg, ["-i", str(output), "-pix_fmt", pixel_format,
                        "-fps_mode", "passthrough", "-f", "framemd5", "-"]))
                    case.update(actualFrames=len(actual), exactPixelsAndRange=bool(expected) and actual == expected)
                    listing = output.with_suffix(".concat.txt")
                    escaped = output.resolve().as_posix().replace("'", "'\\''")
                    listing.write_text(f"file '{escaped}'\nfile '{escaped}'\n", encoding="utf-8")
                    montage = output.with_name(output.stem + "-repeated.mp4")
                    run(args.ffmpeg, ["-f", "concat", "-safe", "0", "-i", str(listing),
                        "-map", "0:v:0", "-c", "copy", str(montage)])
                    combined = hashes(run(args.ffmpeg, ["-i", str(montage), "-pix_fmt", pixel_format,
                        "-fps_mode", "passthrough", "-f", "framemd5", "-"]))
                    case.update(repeatedMontageFrames=len(combined),
                                repeatedMontageExact=bool(expected) and combined == expected + expected)
                except (RuntimeError, subprocess.TimeoutExpired) as error:
                    case.update(exactPixelsAndRange=False, error=str(error))
                report["cases"].append(case)
    (args.output / "report.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
