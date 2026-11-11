# Unreleased engineering changes after 0.2.5

- Classify the matched portion of each movement rather than borrowing a later action from the entire window. Preserve uncertain results.
- Make result ranking exact and deterministic for near ties and unavailable numeric scores.
- Exclude invalid numeric descriptors from nearest-neighbor retrieval.
- Avoid copying frames and keypoints for normal interval classification; reuse established static classification during ranking.
- Support signed file-level incremental update archives with compatible-base checking, unchanged-file verification, resumed downloads and recoverable installation. Keep complete Portable archives for older clients and modified installations.
- Extend release packaging with `PreviousPackage` / `PreviousVersion` and reconstruction health checks.
- Report classification macro-F1 and unknown rate alongside accuracy.

No visual redesign, new model, threshold tuning or release publication is included.
The full architectural objective remains incomplete. See [measurements, tests and open gates](goal-rework-audit-2026-11-11.md).
