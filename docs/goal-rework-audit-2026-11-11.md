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

Windows 11, UCRT64 GCC 16.2, Release `-O3 -DNDEBUG`. Baseline code is revision `6374789`; source snapshots and raw logs are under ignored `build/goal-*`. No new model or threshold tuning was used. The table below records the initial engineering controls; independent KTH results are reported separately to avoid mixing protocols.

| Metric | Before | After | Scope / limit |
|---|---:|---:|---|
| Total analysis time | Unavailable | Unavailable | Original fixture videos are absent; KTH extraction is not a complete application baseline |
| Interval classification, 100,000 calls | 520.282 ms median | 12.5685 ms median | Five alternating runs, 240 synthetic frames, 120-frame interval; checksums identical at 1,200,000; not whole-video performance |
| Matcher Recall@1 | 1.0 | 1.0 | Two query directions of one previously developed positive pair; no blind holdout |
| Matcher Precision@1 | 1.0 | 1.0 | Same tiny control, no labelled negatives or competing candidates; does not establish false-positive rate |
| Classification accuracy / macro-F1 | 1.0 / 1.0 | 1.0 / 1.0 | Existing 11-case reviewed regression sample, not independent validation |
| Unclassified rate | 4/11 | 4/11 | All four are labelled unknown; forcing a label would be an error |
| Peak RAM / VRAM | Unavailable | Unavailable | Full real-video comparison not run; microbenchmark is insufficient |
| Update payload, 0.2.3 → 0.2.5 | 436,672,284 bytes | 5,035,600 bytes | Existing local Portable archives, 1,975 of 2,004 files reused; includes changed large files in full |

The 98.85% update-byte reduction is measured on that release pair only. Binary delta dependencies were not introduced: file exclusion already removes the bulk of this routine download. A future changed model/runtime can produce a substantially larger package. The local reconstructed update used an ephemeral test key; production trust was not changed and no release was published.

## Validation and unresolved gates

The unchanged supported build passed; baseline CTest was **13/14**, with the UI suite crashing while Qt initialized Windows locale state. Early integrated runs reproduced it. A separate warning-collector lifetime fix did not resolve the crash and is not credited as its root-cause correction.

A minimal QtCore process then reproduced the access violation: initialize COM STA, request system UI languages, release the final COM reference, request languages again. Qt 6.11.2 caches a WinRT locale object. Holding an owned `RoInitialize` reference before QGuiApplication construction until after Qt teardown removes this condition; failed initialization is never uninitialized by us. A new nested COM/locale/UI regression protects this lifecycle. Full UI suites subsequently passed twice, then the integrated **15/15 CTest targets passed in 138.91 seconds**, including `pfui_tests` in 46.21 seconds (`goal-final3-ctest.log`). This supersedes the earlier unresolved suite status. See [Microsoft's initialization contract](https://learn.microsoft.com/en-us/windows/win32/api/roapi/nf-roapi-roinitialize) and [Qt's locale implementation](https://raw.githubusercontent.com/qt/qtbase/v6.11.2/src/corelib/text/qlocale_win.cpp).

The first integrated run after the UI correction exposed a separate Windows settings race. An atomic QSaveFile commit could conflict with an in-process reader. The new two-reader/500-save control reproduced 656 errors before correction. Bounded shared-mutex striping now covers only file I/O, keeping JSON work and callbacks outside the lock. All 11 settings tests pass; the targeted UI responsiveness check recorded a maximum heartbeat gap of 37 ms. Successful load/save also clear stale error strings.

Real Windows updater-process tests now cover full and incremental success, failed-health rollback and damaged-base handling, alongside download interruption/Range resume and full-archive fallback. The actual 0.2.3 reconstruction with the 5,035,600-byte payload passed the packaged app health check in 18,176 ms. Production signing trust remains unchanged.

Independent controls are now available: 216 held-out KTH videos, 863 pose intervals and a separately frozen 216-clip temporal action evaluation. [The detailed protocol and results](low-resolution-pose-validation.md) document confirmed low-resolution preprocessing failure, a 0.249% → 91.491% detection-coverage improvement, conservative gait classification gains, temporal-versus-frozen representation controls and substantial remaining recall limitations. These are action proxies, not cinematic editing truth. No model weights or classification thresholds were tuned on the holdout. The original application footage is still absent.

The Release build succeeded. Its log also contains existing C++ range-copy and deprecated filesystem conversion warnings; they are not suppressed and a warning-free clean build is not claimed. Tests after the semantic run-label integration are recorded in the detailed report when complete. No installer was published or clean-machine certified.

## Changed modules and commits

- Core: `MotionIndex.cpp/.hpp`, `MotionRanker.cpp`, `MovementClassifier.cpp`; regressions in `test_motion_index.cpp` and `test_movement_classifier.cpp`.
- Measurement: `classifier_interval_bench.cpp`, benchmark CMake/README, `evaluate_quality.py`, `test_quality_metrics.py`.
- Updater: manifest types/parser, `UpdateService`, `UpdateTransaction`, `updater_main`, updater tests. Release scripts: `sign-update.ps1`, `build-windows.ps1` and release instructions.
- GPU preprocessing and cache contracts; native pose/action benchmark probes and frozen KTH protocols.
- Windows GUI apartment lifetime and settings I/O synchronization.
- Documentation and additive run labels in existing localization/category catalogs. Palette, layout, fonts and model weights are preserved; stale small-source observations receive explicit cache invalidation.

Focused implementation commits:

- `412e5aa` — Fix matched-interval classification and deterministic ranking.
- `d11be72` — Add signed file-level incremental updates and release validation.
- `b16750d` — Bound UI warning collector lifetime (separate issue).
- `1780437` — Fix low-resolution pose enlargement and freeze independent action controls.
- `dc39037` — Freeze temporal action validation policy and reuse native AVA probe.
- `09619ea` — Serialize settings file replacement against concurrent readers.
- `bdbb54f` — Keep the GUI apartment alive and exercise process-level update rollback.
- `ed4341a` — Classify associated temporal running evidence with frozen validation gates.

The run-label integration passed all **15/15 CTest targets in 140.34 seconds** (`goal-running-ctest.log`), followed by 8/8 targeted video-action evidence tests. The later benchmark settings-isolation change passed 12/12 settings tests; the native all-people smoke regression passed three QtTest phases. Subsequent test additions are not covered by those earlier logs and await their next full integration run.

The user has now supplied real series for the next validation stage. All 16 files were probed, and a full baseline all-people analysis of two episodes is running with isolated settings/cache and process/GPU telemetry. [The new protocol and cache audit](chernobyl-cache-validation.md) supersede any implication that no real footage is available; earlier fixture paths remain absent but no longer prevent this work.

Further work requires cinematic positive/negative judgements and separate confidence-calibration data, full cold/warm application and CPU/GPU resource profiling, and clean-machine package validation. Existing ONNX models were reused, not replaced or newly trained. The low-resolution and KTH controls are reproducible progress; they do not establish complete semantic matching or professional movement accuracy.

The objective remains **partially implemented and not release-certified**. Full UI regression is now green; confidence calibration, production semantic retrieval/reranking, independent editing-parallel precision/recall, broad camera/occlusion controls and full resource comparisons remain open. See the linked validation report for current evidence rather than treating the earlier tiny regression fixture as independent validation.
