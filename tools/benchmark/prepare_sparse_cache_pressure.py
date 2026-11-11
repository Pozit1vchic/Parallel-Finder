"""Copy real cache records into a disposable NTFS logical-capacity control.

Sparse preview records exercise 15–20 GiB budgeting without filling that amount
of physical disk. This is not a disk-full, model-accuracy or full-pipeline test.
"""
import argparse
import ctypes
from ctypes import wintypes
import hashlib
import json
import msvcrt
import os
from pathlib import Path
import shutil
import struct


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-cache", type=Path, required=True)
    parser.add_argument("--output-root", type=Path, required=True)
    parser.add_argument("--logical-gib", type=int, choices=range(15, 21), default=20)
    args = parser.parse_args()
    if os.name != "nt":
        raise RuntimeError("This pressure control requires Windows sparse files")
    source, target = args.source_cache.resolve(), args.output_root.absolute()
    if not source.is_dir() or target.exists():
        raise ValueError("Use an existing source cache and a NEW disposable output directory")
    records = [p for p in sorted(source.iterdir()) if not p.is_symlink() and p.is_file()
               and p.suffix in {".pfc", ".owner", ".generation"}]
    if not any(p.suffix == ".pfc" for p in records):
        raise ValueError("The source has no PFC records")
    source_bytes = sum(p.stat().st_size for p in records)
    free_before = shutil.disk_usage(target.parent).free
    if free_before < source_bytes + 5 * 1024**3:
        raise RuntimeError("Not enough headroom for the real cache copy and 5 GiB free space")
    target.mkdir()
    for path in records:
        shutil.copy2(path, target / path.name)
    (target / ".pf-disposable-benchmark").write_text(
        "ParallelFinder disposable benchmark snapshot v1\n", encoding="utf-8")

    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    ioctl = kernel.DeviceIoControl
    ioctl.argtypes = [wintypes.HANDLE, wintypes.DWORD, wintypes.LPVOID, wintypes.DWORD,
                     wintypes.LPVOID, wintypes.DWORD, ctypes.POINTER(wintypes.DWORD), wintypes.LPVOID]
    ioctl.restype = wintypes.BOOL
    allocated_size = kernel.GetCompressedFileSizeW
    allocated_size.argtypes = [wintypes.LPCWSTR, ctypes.POINTER(wintypes.DWORD)]
    allocated_size.restype = wintypes.DWORD
    chunk_bytes = 512 * 1024**2
    count = args.logical_gib * 1024**3 // chunk_bytes
    allocated = 0
    for index in range(count):
        key = f"preview-v1|disposable-logical-pressure-{index:03d}".encode()
        path = target / (hashlib.sha256(key).hexdigest() + ".pfc")
        with path.open("xb") as file:
            returned = wintypes.DWORD()
            # FSCTL_SET_SPARSE: fail BEFORE extending if the filesystem cannot
            # guarantee sparse storage. Never create a dense 20-GiB filler.
            if not ioctl(msvcrt.get_osfhandle(file.fileno()), 0x000900C4,
                         None, 0, None, 0, ctypes.byref(returned), None):
                raise ctypes.WinError(ctypes.get_last_error())
            if not path.stat().st_file_attributes & 0x200:
                raise RuntimeError("The file was not marked sparse; refusing to extend it")
            payload_bytes = chunk_bytes - 24 - len(key)
            file.write(struct.pack("<8sIIQ", b"PFCACHE1", 1, len(key), payload_bytes) + key)
            file.truncate(chunk_bytes)
        high = wintypes.DWORD()
        ctypes.set_last_error(0)
        low = allocated_size(str(path), ctypes.byref(high))
        if low == 0xFFFFFFFF and ctypes.get_last_error():
            raise ctypes.WinError(ctypes.get_last_error())
        allocated += (high.value << 32) | low
        if allocated > 64 * 1024**2:
            raise RuntimeError("Sparse allocation exceeds the safety bound; pressure preparation stopped")
    report = {
        "schemaVersion": 1, "sourceCache": str(source), "disposableRoot": str(target),
        "realCacheFilesCopied": len(records), "realCacheBytesCopied": source_bytes,
        "pressureType": "NTFS sparse logical size; NOT physical disk-full testing",
        "fillerPfcFiles": count, "fillerLogicalBytes": count * chunk_bytes,
        "fillerAllocatedDataBytes": allocated, "freeBytesBefore": free_before,
        "freeBytesAfter": shutil.disk_usage(target).free,
        "scope": "PFC budget and retention only; no source video edits or model/matcher inference",
    }
    (target / "pressure-provenance.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
