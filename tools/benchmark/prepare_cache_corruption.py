"""Corrupt one primary record in a NEW disposable real-cache copy.

Only PFC and its ownership/generation records are copied. Never opens videos,
never mutates the source cache, and refuses an existing destination.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import struct


def sha(path):
    digest = hashlib.sha256()
    with path.open("rb") as file:
        while chunk := file.read(1024 * 1024):
            digest.update(chunk)
    return digest.hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-cache", type=Path, required=True)
    parser.add_argument("--output-root", type=Path, required=True)
    parser.add_argument("--source-name", required=True, help="Exact episode filename inside its observation key")
    args = parser.parse_args()
    source, target = args.source_cache.resolve(), args.output_root.absolute()
    if not source.is_dir() or target.exists():
        raise ValueError("Use an existing source cache and a NEW destination")
    records = [p for p in sorted(source.iterdir()) if not p.is_symlink() and p.is_file()
               and p.suffix in {".pfc", ".owner", ".generation"}]
    candidates = []
    primary = []
    for path in records:
        if path.suffix != ".pfc":
            continue
        with path.open("rb") as file:
            header = file.read(24)
            if len(header) != 24:
                continue
            magic, version, key_size, payload_size = struct.unpack("<8sIIQ", header)
            if (magic != b"PFCACHE1" or version != 1 or not 0 < key_size <= 16384
                    or payload_size > 512 * 1024**2 or path.stat().st_size != 24 + key_size + payload_size):
                continue
            key = file.read(key_size).decode("utf-8", errors="strict")
            if key.startswith(("motion-v", "scene-sequence-v")):
                primary.append(path)
            if key.startswith("motion-v") and args.source_name in key:
                # PFCH includes its own checksum. A byte flip must be rejected
                # by the actual artifact decoder rather than by a fake parser.
                if file.read(4) != b"PFCH":
                    raise ValueError("Expected checksummed all-people PFCH artifact")
                candidates.append((path, 24 + key_size, payload_size))
    if len(candidates) != 1:
        raise ValueError(f"Expected exactly one matching primary artifact, found {len(candidates)}")
    copied_bytes = sum(p.stat().st_size for p in records)
    if shutil.disk_usage(target.parent).free < copied_bytes + 5 * 1024**3:
        raise RuntimeError("Insufficient headroom for cache copy plus 5 GiB reserve")
    before = {p.name: sha(p) for p in primary}
    target.mkdir()
    for path in records:
        shutil.copy2(path, target / path.name)
    (target / ".pf-disposable-benchmark").write_text(
        "ParallelFinder disposable benchmark snapshot v1\n", encoding="utf-8")
    selected, offset, length = candidates[0]
    damaged = target / selected.name
    position = offset + length // 2
    with damaged.open("r+b") as file:
        file.seek(position)
        original = file.read(1)
        if len(original) != 1:
            raise RuntimeError("Selected offset was not readable")
        file.seek(position)
        file.write(bytes([original[0] ^ 1]))
    after = {p.name: sha(target / p.name) for p in primary}
    changed = [name for name in before if before[name] != after[name]]
    if changed != [selected.name]:
        raise RuntimeError("Expected exactly one changed primary record")
    report = {"schemaVersion": 1, "sourceCache": str(source), "disposableRoot": str(target),
              "sourceName": args.source_name, "corruptedRecord": selected.name,
              "flippedByteOffset": position, "copiedBytes": copied_bytes,
              "primarySha256Before": before, "primarySha256After": after,
              "scope": "One payload byte changed in disposable PFCH copy; no videos modified"}
    (target / "corruption-provenance.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
