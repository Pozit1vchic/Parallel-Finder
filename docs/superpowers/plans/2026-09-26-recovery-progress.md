# Recovery progress — 2026-09-26

## Workspace and baseline

- User explicitly chose in-place implementation in `D:/Parallel-Finder` with backup.
- Baseline copies of tracked and non-ignored untracked files are in `build/recovery-20260926-baseline`; preserve them when producing the final patch-only archive.
- Existing user changes are not rolled back or committed wholesale.

## Verified fixes in this pass

### Export: MP4 codec-none failure

Problem: `-map 0` selected subtitle/data streams as well as video/audio. A synthetic MKV containing SubRip reproduced the reported `Automatic encoder selection failed ... codec none` error in Exact mode and unsupported SubRip-in-MP4 in Fast mode.

Fix: video clip export explicitly maps the primary video and optional audio, excluding subtitles/data/attachments. This intentionally does not preserve subtitles; it is not a general container-copy operation.

Files: `services/src/CutService.cpp`, `tests/core/test_cut_service.cpp`.

Evidence: integration test failed before the fix, passed after it for both modes. It also decodes both output clips and checks temporary files are gone. This test requires FFmpeg and skips if unavailable; FFmpeg was present in this run.

### Matcher: required identity check bypass

Problem: the mandatory identity condition only rejected mismatches when evidence was available; missing or weak evidence silently passed. The existing benchmark regression `AppearanceGateRejectsInsufficientEvidence` failed before the fix.

Fix: `requireAppearance` requires verified evidence regardless of availability. Existing body-evidence fallback and reliable-face disagreement veto are unchanged. This does not solve character identity across different roles of the same actor.

Files: `core/src/MotionMatcher.cpp`, `tests/core/test_motion_matcher.cpp`.

The core suite had a contradictory test expecting pose-only fallback with mandatory identity. Updated it to the user's explicit same-person requirement; tests weak evidence, absent evidence, direct comparison, pair search, and explicitly disabled low-level identity checking.

## Verification

- Release preset rebuilt successfully with MSYS2 UCRT64.
- `ctest --test-dir build/ucrt64-release --output-on-failure`: all 4 CTest targets passed, 9.71 seconds. Log: `build/recovery-tests.log`.
- Changed source/test files pass `git diff --check`.
- Whole-tree whitespace check still reports the pre-existing trailing blank line in `tests/core/test_job_manager.cpp`; not modified in this pass.

## Continuation — 2026-09-27

Implemented and tested after the initial checkpoint:

- Sequential background export queue, per-item failures, progress and cancellation, including cancellation after the encoder starts. Full scene export replaces the 25-second cap. Encoder discovery is cached per service and cancellable; long exports have no arbitrary three-minute deadline. Preview requests retain an explicit timeout.
- FFmpeg/video default, `frame` base name, select-all/clear, removal/migration of the obsolete clips mode, motion-only play button, local-file URL conversion for the cache-folder picker.
- Independent child-process validation of installed DirectML/CUDA/TensorRT bundles. Startup and validation use the same user-first resolver, including wrapped bundle folders and Unicode paths. Downloads and validation cannot run simultaneously. Actual analysis still needs a restart to switch runtime: hot activation is NOT implemented.
- Installer task labels localized in source; installer compilation not yet verified.
- Hybrid results have independent per-type limits and duplicate suppression. Reversed A/B pairs are deduplicated. This is NOT the separate full-frame StaticFrameMatcher required by the remaining plan.
- Candidate index regression: a 120-pair synthetic fixture previously returned only 17 pairs. Exact retrieval for indexes up to 512 nodes now returns all 120. Degenerate flat/incomplete neighborhoods in larger indexes use exact fallback. Large nondegenerate searches remain approximate; no blanket performance claim is made.

Verification:

- Full Release build and all 4 CTest targets passed on 2026-09-27, 10.30 seconds (`build/recovery-tests.log`). Opt-in integration tests must not be counted as exercised merely because the default suite passes.
- Explicit real-video Exact/Fast export test passed (3.772 seconds), checking resolution, frame rate, audio and a duration above the former 25-second cap. The supplied Soldier Boy file is actually 1920×1080 at 60 fps, despite its 4K filename.
- Explicit isolated validation of all three locally installed provider bundles passed (8.144 seconds).
- Initial Soldier Boy analysis completed in 100.722 seconds with 50 pose results. After the candidate-index/per-type-budget changes, a completed run returned 96 results (46 motion, 50 pose), all with verified identity evidence, in 109.275 seconds. These are algorithm outputs, not a human precision/recall annotation.

### Preview and cache-path follow-up

- The 96 results requested 192 exact previews for only 75 unique source/timestamp pairs. `PreviewMemo` now reuses successful URLs within one analysis; failed renders remain retryable and no decoded frame buffers are retained. Two tests failed before implementation and passed after it.
- Same-video run after this change: 70.055 seconds, exactly the same 96 type/source/time/similarity records as before. Repeated diagnostic run: 58.900 seconds. Treat these as individual warm-cache measurements, not a universal speedup guarantee.
- Detailed repeated-run timings for 75 renders: open 657 ms total, seek/decode/convert 16,954 ms, image save 15,420 ms, overall previews 33,757 ms. Earlier run: matcher 850 ms, rank 53 ms, previews 41,996 ms. Preview preparation, not candidate retrieval, is the measured bottleneck. PNG encoding and random decoding still need work.
- Added cancellation checks between preview records. Partial results and counts stay aligned after cancellation.
- Old saved `file:///` cache-folder values migrate to local paths on load; ordinary literal `%20` folder names remain untouched. Migration test reproduced the old failure before passing.

### Closed-gesture regression

`MotionRanker` could discard an accepted raise-and-lower gesture because the classifier only inspected its equal endpoint poses. A new regression failed with zero retained results. Classification now recognizes a sustained intermediate excursion when endpoints are static, labels it mixed (not a guessed specific gesture), and leaves temporal/identity validation to the matcher. Tests retain the closed gesture but still discard small jitter and a single-frame outlier. Real-video acceptance after this change is separate from the 96-result measurements above.

Latest validation after the closed-gesture change:

- Full Release build and 4/4 CTest targets passed (13.19 seconds); compiler still reports existing-in-this-pass C++23 deprecation warnings for `u8path`, not build errors.
- Soldier Boy CLI analysis exited 0 in 60.009 seconds with 100 results: 50 motion and 50 pose (configured per-type caps). All have `identityVerified=true`. This flag is model evidence, not manual proof of every pair's identity or motion accuracy.
- Matcher 846 ms, rank 56 ms, 79 unique result previews 34,974 ms. Log: `build/recovery-final-soldier.out.log`, timings in `.err.log`.
- Diagnostic Release EXE: `build/ucrt64-release/ParallelFinder.exe`; portable/installer artifacts were NOT rebuilt and are not interchangeable with this development build.
- No commits/pushes, destructive cleanup or final patch ZIP in this continuation. User baseline backup remains intact.

### Review and limitations

A bounded independent review found no critical/important issue in the small-index exact path or per-type budgets. It identified an orientation-dependent contextual-NMS edge case without scene IDs. The ordinary four-window permutation fixture does not reproduce different counts; an isolated-identity harness does (direct 1, reverse 2). Because that fixture uses two distinct identities, the desired behavior must preserve legitimate distinct-character pairs rather than blindly suppressing more results. Deferred for a dedicated identity-aware NMS test/fix; not represented as fully resolved.

## Not yet completed

Latest 2026-09-28 continuation: see `docs/recovery-inline-preview-2026-09-28.md` for independent A/B playback, actual keyboard-event tests, headless provider-probe and single-desktop-instance fixes, 5/5 CTest evidence, and the new real-video acceptance boundary (Dean 3/3 positives and 0/3 negatives; Soldier 2/3 positives and 0/5 negatives). The Soldier 79/94 recall regression is still open. User subsequently authorized one combined local commit; older "no commits" entries above describe their historical checkpoint only.

Analysis worker process/live provider switching, full Motion/Static feature separation, measured preview/decode performance fixes, comprehensive cache-root migration, remaining appearance/UI requirements, installer build/validation, real scenepack precision/recall acceptance, and final patch-only ZIP remain pending. No installer or release archive was produced in this pass. Passing synthetic tests is not proof of real-video precision/recall or release readiness.
