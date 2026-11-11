"""Fetch planned KTH videos by ZIP ranges, retaining hashes and published labels.

Research/evaluation only: the dataset is available for non-commercial use.
Downloaded material belongs under ignored build/, never in release artifacts.
"""
import argparse
import concurrent.futures
import hashlib
import io
import json
from pathlib import Path
import re
import struct
import time
import urllib.request
import zipfile
import zlib


BASE = "https://www.csc.kth.se/cvap/actions/"
PLAN = Path(__file__).with_name("kth-validation-plan.json")


def fetch_range(url, start, end=None):
    header = f"bytes={start}-{end}" if end is not None else f"bytes=-{start}"
    limit = end - start + 1 if end is not None else start
    for attempt in range(3):
        try:
            request = urllib.request.Request(url, headers={
                "Range": header, "User-Agent": "ParallelFinder-independent-validation/1"})
            with urllib.request.urlopen(request, timeout=45) as response:
                match = re.fullmatch(r"bytes (\d+)-(\d+)/(\d+)", response.headers.get("Content-Range", ""))
                if response.status != 206 or not match:
                    raise ValueError("Server must support verified byte ranges")
                first, last, total = map(int, match.groups())
                expected_start = start if end is not None else max(0, total - start)
                expected_end = min(end, total - 1) if end is not None else total - 1
                data = response.read(limit + 1)
                if first != expected_start or last != expected_end or len(data) != last - first + 1:
                    raise ValueError("Incorrect range or incomplete response")
                return first, data
        except (OSError, ValueError):
            if attempt == 2:
                raise
            time.sleep(attempt + 1)


def entries(action):
    url = f"{BASE}{action}.zip"
    start, tail = fetch_range(url, 65536)
    # zipfile accounts for the missing prefix as a negative concatenation
    # offset. Restore the absolute header positions for range extraction.
    with zipfile.ZipFile(io.BytesIO(tail)) as archive:
        result = archive.infolist()
    for item in result:
        item.header_offset += start
    return url, result


def extract_member(url, item):
    if item.flag_bits & 1 or item.compress_type not in (0, 8) or not 0 < item.file_size <= 32 * 1024 * 1024:
        raise ValueError("Unsupported dataset member")
    _, header = fetch_range(url, item.header_offset, item.header_offset + 29)
    fields = struct.unpack("<4s5H3I2H", header)
    if fields[0] != b"PK\x03\x04" or fields[2] & 1 or fields[3] != item.compress_type:
        raise ValueError("Invalid local ZIP header")
    body_start = item.header_offset + 30 + fields[-2] + fields[-1]
    _, compressed = fetch_range(url, body_start, body_start + item.compress_size - 1)
    if item.compress_type == 8:
        decoder = zlib.decompressobj(-15)
        data = decoder.decompress(compressed, item.file_size + 1)
        if not decoder.eof or decoder.unused_data or decoder.unconsumed_tail:
            raise ValueError("Invalid compressed member")
    else:
        data = compressed
    if len(data) != item.file_size or zlib.crc32(data) != item.CRC:
        raise ValueError("Dataset member failed size/CRC validation")
    return data


def fetch_video(task):
    url, item, destination, split, person, action, scenario = task
    target = destination / item.filename
    if target.exists():
        data = target.read_bytes()
        if len(data) != item.file_size or zlib.crc32(data) != item.CRC:
            raise ValueError(f"Existing dataset file is damaged: {target}")
    else:
        data = extract_member(url, item)
        partial = target.with_suffix(".avi.part")
        partial.write_bytes(data)
        partial.replace(target)
    return {"file": target.name, "subject": person, "action": action, "scenario": scenario,
            "split": split, "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest(),
            "archiveUrl": url, "archiveMemberCrc32": f"{item.CRC:08x}"}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--destination", type=Path, required=True)
    parser.add_argument("--split", choices=("tuning", "development", "holdout", "all"), default="tuning")
    parser.add_argument("--workers", type=int, choices=range(1, 5), default=3)
    args = parser.parse_args()
    plan = json.loads(PLAN.read_text(encoding="utf-8"))
    splits = plan["splits"]
    all_people = [person for people in splits.values() for person in people]
    if len(all_people) != len(set(all_people)):
        raise ValueError("Subjects must not overlap across dataset splits")
    selected = {person: split for split, people in splits.items() for person in people
                if args.split in ("all", split)}
    args.destination.mkdir(parents=True, exist_ok=True)
    tasks = []
    for action in plan["actions"]:
        url, members = entries(action)
        for item in members:
            match = re.fullmatch(rf"person(\d{{2}})_{re.escape(action)}_d([1-4])_uncomp\.avi", item.filename)
            if not match:
                raise ValueError(f"Unexpected dataset filename: {item.filename}")
            person, scenario = map(int, match.groups())
            if person in selected:
                tasks.append((url, item, args.destination, selected[person], person, action, scenario))
    expected = len(selected) * len(plan["actions"]) * len(plan["scenarios"])
    if len(tasks) != expected:
        raise ValueError(f"Incomplete planned dataset: {len(tasks)} of {expected} files")
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.workers) as pool:
        rows = list(pool.map(fetch_video, tasks))
    # Download only the small published temporal-boundary annotation as a whole.
    with urllib.request.urlopen(BASE + "00sequences.txt", timeout=30) as response:
        annotations = response.read(1024 * 1024 + 1)
    if not annotations or len(annotations) > 1024 * 1024:
        raise ValueError("Invalid temporal annotation download")
    (args.destination / "00sequences.txt").write_bytes(annotations)
    report = {"schemaVersion": 1, "planSha256": hashlib.sha256(PLAN.read_bytes()).hexdigest(),
              "source": BASE, "terms": plan["terms"], "split": args.split,
              "annotationsSha256": hashlib.sha256(annotations).hexdigest(), "videos": rows}
    output = args.destination / f"manifest-{args.split}.json"
    output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"manifest": str(output.resolve()), "files": len(rows),
                      "videoBytes": sum(row["bytes"] for row in rows)}))


if __name__ == "__main__":
    main()
