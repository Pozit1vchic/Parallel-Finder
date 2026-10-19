"""Find repeated result cards whose source intervals substantially overlap.

This is a narrow duplicate audit: it does not detect identical footage copied
to different timestamps or different files. It accepts the application's
analysis-smoke JSON report format and makes A/B swaps equivalent.
"""
from __future__ import annotations

import argparse
import json
import ntpath
from pathlib import Path
from typing import Any


def _source_key(value: Any) -> str:
    if not isinstance(value, str) or not value.strip():
        raise ValueError("Every result must contain non-empty left/right source paths")
    return ntpath.normcase(ntpath.normpath(value.strip()))


def _number(row: dict[str, Any], key: str) -> float:
    try:
        value = float(row[key])
    except (KeyError, TypeError, ValueError) as exc:
        raise ValueError(f"Result is missing numeric field {key!r}") from exc
    if not (value == value and abs(value) != float("inf")):
        raise ValueError(f"Result field {key!r} must be finite")
    return value


def _interval(row: dict[str, Any], side: str) -> tuple[float, float]:
    # Scene bounds are what a user sees as a result card. Fall back to the
    # matched action bounds for reports created before scene bounds were added.
    start_key, end_key = side + "SceneStart", side + "SceneEnd"
    if start_key not in row or end_key not in row:
        start_key, end_key = side + "Start", side + "End"
    start, end = _number(row, start_key), _number(row, end_key)
    if end <= start:
        raise ValueError(f"Result interval {side!r} must have positive duration")
    return start, end


def _overlap_fraction(a: tuple[float, float], b: tuple[float, float]) -> float:
    overlap = max(0.0, min(a[1], b[1]) - max(a[0], b[0]))
    shorter = min(a[1] - a[0], b[1] - b[0])
    return overlap / shorter if shorter > 0 else 0.0


def find_duplicate_intervals(results: list[dict[str, Any]], threshold: float = 0.7) -> list[dict[str, Any]]:
    if not 0.0 < threshold <= 1.0:
        raise ValueError("threshold must be in (0, 1]")
    prepared = []
    for index, row in enumerate(results):
        if not isinstance(row, dict):
            raise ValueError(f"Result at index {index} must be an object")
        prepared.append((
            _source_key(row.get("leftSource")), _source_key(row.get("rightSource")),
            _interval(row, "left"), _interval(row, "right"), row.get("id", index),
        ))

    duplicates = []
    for i, (la, ra, li, ri, lid) in enumerate(prepared):
        for j in range(i + 1, len(prepared)):
            lb, rb, lj, rj, rid = prepared[j]
            if la == lb and ra == rb:
                overlap_a, overlap_b = _overlap_fraction(li, lj), _overlap_fraction(ri, rj)
            elif la == rb and ra == lb:
                overlap_a, overlap_b = _overlap_fraction(li, rj), _overlap_fraction(ri, lj)
            else:
                continue
            if overlap_a >= threshold and overlap_b >= threshold:
                duplicates.append({
                    "firstIndex": i, "firstId": lid,
                    "secondIndex": j, "secondId": rid,
                    "overlapLeft": overlap_a, "overlapRight": overlap_b,
                    "orientation": "direct" if la == lb and ra == rb else "swapped",
                })
    return duplicates


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--report", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--overlap-threshold", type=float, default=0.7)
    args = parser.parse_args()
    report = json.loads(args.report.read_text(encoding="utf-8-sig"))
    results = report.get("results") if isinstance(report, dict) else None
    if not isinstance(results, list):
        raise ValueError("Report must contain a results array")
    duplicates = find_duplicate_intervals(results, args.overlap_threshold)
    output = {
        "sourceReport": str(args.report.resolve()),
        "resultCount": len(results),
        "overlapThreshold": args.overlap_threshold,
        "duplicateIntervalPairs": duplicates,
        "scope": "overlapping source scene intervals; copied footage at different timestamps is not detected",
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(output, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({key: value for key, value in output.items() if key != "duplicateIntervalPairs"}
                     | {"duplicatePairCount": len(duplicates)}, ensure_ascii=False))


if __name__ == "__main__":
    main()
