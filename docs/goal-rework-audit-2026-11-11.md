# Architectural rework audit — 2026-11-11

Starting revision: `6374789`. Existing untracked `background-matcher-investigation.md` belongs to the user and is outside this change.

## Architecture and existing evidence

The application already uses C++23, Qt/QML, FFmpeg decoding, ONNX inference, bounded frame processing, an HNSW-style candidate index, temporal DTW matching, independent movement grouping, scene families, and persistent observation/estimator caches. `AnalysisController` orchestrates these stages. GPU sessions and execution providers live in `gpu`; decoding, geometry, matching and classification in `core`; caches, model/provider distribution and export in `services`; signed installation transactions in `updates`.

Read existing `AUDIT.md`, matcher/classifier audit RC22.7, quality implementation, runtime distribution, benchmark instructions, updater implementation, release scripts and CI. Git history includes previous cache-copy and preparation-stall fixes; tags extend through v0.2.5. Existing historical timings are not a new baseline.

The updater currently authenticates metadata with Ed25519, checks complete ZIP and inventory hashes, resumes downloads and uses a separate Windows worker with staging/rollback. It still downloads the complete portable ZIP. Models and GPU providers have separate distribution channels, but packaged unchanged runtime files are downloaded with each app update.

## Findings to reproduce before fixing

- `MotionRanker::rank` uses epsilon score comparisons. Approximate equality is non-transitive and does not satisfy the comparator contract required by sorting. A shared exact, NaN-aware comparator already exists but this path does not use it.
- Ranking classifies full windows rather than matched intervals. Investigate valid interval handling and legacy callers before changing it.
- `MotionIndex` accepts NaN/Infinity descriptors; NaN distances can reach heap/sort comparisons. Reproduce invalid input handling before choosing a policy.
- Metadata fetch aborts after appending all currently buffered bytes. Assess bounded network reads using existing provider download tests as reference.

## Hypotheses, not measured bottlenecks

Repeated classification in rank/filter; copying full frame records for interval classification; graph visited-vector allocation per layer; quadratic diversity ranking; controller size. None establishes end-to-end speedup without profiling. Existing temporal classifier confidence is explicitly evidence strength, not a calibrated probability. Independent semantic/action ground truth is required before claiming accuracy improvement or replacing models.

## Work order and dependencies

1. Build the unchanged supported UCRT64 configuration and run all CTest targets; preserve logs under ignored `build`.
2. Add failing regressions for verified ordering, descriptor and interval defects, then fix existing modules.
3. Benchmark affected paths against the original revision with identical inputs and compare results.
4. Extend signed update metadata and release packaging compatibly; reuse existing download and transaction safeguards and test interrupted/resumed updates and rollback.
5. Evaluate matching/classification against available labelled controls, distinguish engineering fixtures from independent quality validation, document unavailable metrics.
6. Run full build/regressions, update release notes and create focused commits. No release publication is authorized by the objective.

## Completion criteria

This audit is not completion of the objective. Precision/recall, macro-F1, cold/warm analysis time, RAM/VRAM and actual release-transfer bytes must be reported with their measurement conditions or explicit blockers. UI layout and visual identity remain the reference. No new ML dependency is justified by this audit alone.

## Implemented and reproduced

- Two rank-order regressions failed on the original code: numeric scores could remain behind NaN, and near-equal scores could appear in increasing order. Ranking now uses the existing exact NaN-aware comparator with index tie-breaking.
- A matched-interval regression returned `mixed` instead of `right` because the full observation window included a later return movement. Ranking now classifies each half-open matched interval independently; legacy zero/zero ranges retain whole-window behavior. Invalid explicit ranges stay unknown. The existing unknown-side regression verifies that these results remain accessible.
- Ranking no longer repeats classification to discard confirmed static pairs: it reuses labels that already require positive evidence on both sides.
- Nonfinite descriptors previously entered the index. Empty, NaN and infinite descriptors/queries now represent unavailable evidence and do not participate in heaps or sorting. Valid retrieval behavior and workspace/concurrent-reader controls pass.
- Normal interval classification borrows a `std::span` of original frames instead of allocating/copying all keypoint vectors. Noncontiguous legacy observations retain the previous filtering fallback.
- Signed, backward-compatible file-level incremental update variants now reuse unchanged installation files. Selection hashes run on one owned cancellable worker; the transaction verifies copied files before activation. The existing trusted signature, resumed download, staging, Windows helper, health-check, journal and rollback paths remain in use. Network buffering is bounded for metadata and package reads.
- Release tooling generates the smaller archive from the actual previous Portable ZIP, signs its metadata together with the complete inventory, and validates reconstruction over the previous package. Older clients retain the full ZIP path.
- Quality reports now include per-class precision/recall/F1, macro-F1 and unknown rate, including predicted-only classes. Unknown is an explicit category, not a dropped case.

## Measurements

Windows 11, UCRT64 GCC 16.2, Release `-O3 -DNDEBUG`. Baseline code is revision `6374789`; source snapshots and raw logs are under ignored `build/goal-*`. No new model or threshold tuning was used.

| Metric | Before | After | Scope / limit |
|---|---:|---:|---|
| Total analysis time | Unavailable | Unavailable | Original fixture videos are absent; no complete fixed-video/model baseline established |
| Interval classification, 100,000 calls | 520.282 ms median | 12.5685 ms median | Five alternating runs, 240 synthetic frames, 120-frame interval; checksums identical at 1,200,000; not whole-video performance |
| Matcher Recall@1 | 1.0 | 1.0 | Two query directions of one previously developed positive pair; no blind holdout |
| Matcher Precision@1 | 1.0 | 1.0 | Same tiny control, no labelled negatives or competing candidates; does not establish false-positive rate |
| Classification accuracy / macro-F1 | 1.0 / 1.0 | 1.0 / 1.0 | Existing 11-case reviewed regression sample, not independent validation |
| Unclassified rate | 4/11 | 4/11 | All four are labelled unknown; forcing a label would be an error |
| Peak RAM / VRAM | Unavailable | Unavailable | Full real-video comparison not run; microbenchmark is insufficient |
| Update payload, 0.2.3 → 0.2.5 | 436,672,284 bytes | 5,035,600 bytes | Existing local Portable archives, 1,975 of 2,004 files reused; includes changed large files in full |

The 98.85% update-byte reduction is measured on that release pair only. Binary delta dependencies were not introduced: file exclusion already removes the bulk of this routine download. A future changed model/runtime can produce a substantially larger package. The local reconstructed update used an ephemeral test key; production trust was not changed and no release was published.

## Validation and unresolved gates

- Unmodified build succeeded (`goal-baseline-build.log`, no pending work). Baseline CTest: 13/14 passed in 120.35 seconds; `pfui_tests` crashed during `resultArrowKeysWorkAfterSourceButtonFocus`, with the stack in `qt_localeFromLCID` while a QML engine loaded.
- Integrated build succeeded. First integrated CTest repeated the same failure: 13/14, 118.94 seconds. This is a pre-existing reproducible suite failure; its root cause is not established by the Qt stack alone.
- The affected UI test passes separately with CTest's `offscreen` platform (3 QtTest phases passed, 1,427 ms). A native isolated attempt failed to open its popup; that attempt is not counted as a passing UI check.
- Targeted classifier/grouping/ranker run: 65 passed, one opt-in performance test skipped. Index/ranking run before the matched-interval addition: 13 passed. New regressions were verified failing before their fixes.
- Initial incremental update tests: all four methods passed (six QtTest phases). They cover trusted selection, reconstruction, corrupted base refusal, restart rollback, incompatible versions, unsafe reuse inventory and cached package reuse. Subsequent checks extend the download test with interruption, Range resume and full-package fallback.
- Real archive reconstruction over 0.2.3 with the 5,035,600-byte incremental package passed the packaged application's actual health check (18,176 ms; three QtTest phases).
- Eight quality-metric unit tests passed. PowerShell release scripts parse successfully. Final post-change checks are recorded below when complete.

Final available regression gates: all 13 CTest targets outside the already-failing `pfui_tests` suite passed in **87.28 seconds** (`goal-latest-ctest.log`); the exclusion is explicit and is not a full green CTest claim. A separate run from the failing UI method through the remainder of that suite passed **20 QtTest phases**, with **one optional skip**, in **14,906 ms** (`goal-ui-remaining.txt`). The full-suite order-dependent crash remains unresolved. The final supported Release build succeeded without new warnings in its rebuild log. No installer was published or clean-machine certified.

## Changed modules and commits

- Core: `MotionIndex.cpp/.hpp`, `MotionRanker.cpp`, `MovementClassifier.cpp`; regressions in `test_motion_index.cpp` and `test_movement_classifier.cpp`.
- Measurement: `classifier_interval_bench.cpp`, benchmark CMake/README, `evaluate_quality.py`, `test_quality_metrics.py`.
- Updater: manifest types/parser, `UpdateService`, `UpdateTransaction`, `updater_main`, updater tests. Release scripts: `sign-update.ps1`, `build-windows.ps1` and release instructions.
- Documentation: update architecture, this evidence report, unreleased notes and documentation index. No QML, palette, layout, fonts, model weights or user cache files were changed.

Focused implementation commits:

- `412e5aa` — Fix matched-interval classification and deterministic ranking.
- `d11be72` — Add signed file-level incremental updates and release validation.

Recommended next work depends on independent labels: compare existing pose, visual and video-action representations on a blind action/viewpoint split, fit confidence calibration on tuning data only, then rerun a separate holdout. Profile a fixed set of original videos with pinned models/providers using the existing cold/warm pipeline tools before selecting further retrieval or GPU optimizations. Investigate the baseline UI-suite crash with a minimized test sequence before release approval. None of these recommendations is presented as completed implementation.

The objective is **partially implemented, not complete and not release-ready**. No independent labelled positive/negative movement corpus or calibrated confidence model is available. The fixture report explicitly records `sourceVideosAvailable: false`; its original `D:/for_tests_pf` directory is absent. The user has been asked for data/model paths. Walking/running and viewpoint-invariant semantic recall improvements, confidence calibration, independent precision/recall/F1 gains, full CPU/GPU resource profiling, clean-machine installer validation and the complete UI-suite failure remain open. Existing ONNX models were neither replaced nor newly trained. The changes do not justify claiming those requirements as completed.
