# Inline A/B preview and Dean regression checkpoint

## Latest verification: 2026-09-28

- Static pose comparison now has a separate observed-articulation score: signed joint angles, relative head-to-endpoint reach, and bilateral elbow/opposite-wrist relations. Confidence is finite and at least 0.5 for the primary chains; context uses existing confidence-filtered poses. Lower-quartile support rejects one lucky observation. Unknown bilateral support cannot erase an observed contradictory limb. Identity and mirror-mode gates remain intact; this is **not** a learned full-frame matcher or proof of general viewpoint invariance.
- Removed background-colour penalties from static pose similarity. Scene similarity remains separate. Motion direction now uses positive raw cosine instead of giving orthogonal directions automatic 0.5 credit. This changes motion acceptance; synthetic repeats and tempo controls do not establish broad real-world recall.
- Added recorded detector fixtures for the three aiming views, folded-arm false matches, and disputed motion pairs. All 46 MotionMatcher tests pass. New negative cases also cover exact hand-at-nose and an occluded opposite wrist with a contradictory observed arm.
- Final Dean run including missing-bilateral contrary-evidence hardening: 7 results / 16.367 seconds; **3/3 positive anchors retrieved, 0/3 labelled negative pairs returned** (`build/recovery-final-verified-dean.out.log`). Positives are correctly typed as pose, not motion. No timestamps or file-specific exceptions were added to production code. This reproduces the preceding acceptance run (7 results / 19.293 seconds).
- Soldier run: 83 results / 103.602 seconds, 50 pose + 33 motion; **2/3 positive anchors retrieved, 0/5 labelled negatives returned** (`build/recovery-acceptance-soldier.out.log`). The 79/94 anchor is missing from the capped result list, although the prior `recovery-direction-soldier` report contained it. This is an outstanding acceptance regression, not a completed release criterion. These wall times include cached work and preview generation and are not comparable inference-throughput benchmarks.
- Independent bounded code review found two defects (pair-to-single Pause reset and discarded zero-distance hand-at-face evidence), then a bilateral-context negative-evidence regression. Each was reproduced with a failing test and fixed. The review does not cover the whole dirty checkout or certify matching quality.
- Provider scans previously forced `QT_QPA_PLATFORM=offscreen` while the shipped folder had only the Windows plugin. `--pf-provider-probe` now constructs QCoreApplication instead of QGuiApplication and does not request any platform plugin. Windows desktop launches acquire a per-session named instance handle **before** GUI/GPU initialization; duplicates request activation of the existing window and exit. Diagnostic/probe processes remain separate and bypass the desktop lock. No global PATH changes are made.
- Added `startup_isolation`: with a deliberately unavailable Qt platform and developer PATH removed, a duplicate desktop launch exits successfully and the CPU provider probe still emits valid JSON while the desktop lock is held. Passed in 0.69 seconds. Native foreground activation remains subject to Windows focus restrictions; it was not visually verified by this test.
- Latest complete CTest: **5/5 targets passed in 18.10 seconds**, `build/recovery-precommit-tests.log`. Opt-in network/provider-download tests remain opt-in. The development executable is rebuilt; installer/portable release artifacts are not rebuilt by this verification.
- User requested one combined commit of the current checked state, including their pre-existing edits. Backup files remain on disk and are ignored, not deleted; binaries, providers, model weights and analysis caches are not included.
- Repository-wide diff whitespace check now passes; the extra EOF blank line mentioned in the historical checkpoint below was removed.

## Delivered behavior

### Follow-up: independent A/B controls and keyboard navigation

- Fixed both panel buttons routing to `togglePair()`. Each now starts/pauses/resumes only its own inline player; the shared A/B button explicitly starts a coordinated pair. Solo completion/errors do not stop the other solo player. Selection changes still unload both players.
- Regression uses real H.264 decoding, invokes each actual button, checks pause/resume and independent clip end, then checks paired playback and selection cleanup. Before the fix, the other panel entered video mode and the test failed; after the fix it passes.
- Fixed Up/Down shortcuts being disabled for the entire SourcesRail subtree, including the Analyze button that retains focus after a run. Text inputs, sliders, dropdowns, settings/export and file/folder dialogs retain their arrow handling.
- Added a real keyboard-event test in Main.qml with source-button focus. Before: no selection signal. After: one correct selection signal per Up/Down, with no selection change inside text input or settings.
- Rebuilt development executable; complete CTest run: **4/4 targets passed, 22.49 seconds** (`build/preview-arrows-final-tests.log`). This does not claim the opt-in network test ran or that real-video matcher acceptance passed. Repository-wide diff check still reports an unrelated extra EOF blank line in `tests/core/test_job_manager.cpp`; changed UI/test files pass the scoped check.

- Video plays inside the existing A/B frames. There is no external-player call and no synchronous preview export before playback.
- Both players load the original sources, seek to the selected matched starts, then begin from one shared action. Pause/replay are shared; selecting another result stops and unloads both sources.
- Playback stops when either matched interval ends. Playback is muted to avoid mixing two soundtracks. This is paired playback, not frame-accurate DTW time warping or a shared hardware clock.
- Static pose cards retain still-image display. Invalid media reports an error instead of launching another application.
- Qt Multimedia is now a required build/runtime dependency. The release script already invokes QML-aware deployment and follows native DLL imports. Development builds need these runtime files too.

## Evidence

- The embedded-output regression failed before implementation and passes afterward.
- A real synthetic H.264 fixture with a Cyrillic and `#` path decodes into both QVideoSinks and automatically stops at the clip boundary. This exercises decoding, not just object creation.
- Full build and all four CTest targets passed after the changes (18.87 seconds in `build/recovery-inline-final-tests.log`).

## Matcher investigation — not resolved by a result-count increase

- Dean Winchester is now a separate actual-video test, not a renamed Soldier Boy input. Baseline: 7 motion results, 17.708 seconds.
- Added a regression showing that different active body parts plus shared root/camera drift could score 0.7107. The new anatomical-activity check rejects that case and preserves the repeated-arm positive control. Missing joints are excluded on both sides, not interpreted as stationary limbs.
- This fix did **not** change Dean's seven real results. It does not resolve the user's quality complaint and is not presented as doing so.
- Inspected the 9.34–10.18 / 36.37–37.20 pair as frame sequences, not just thumbnails. A clearer head turn is compared with much weaker motion. Component scores: DTW 0.8619, temporal 0.7746, direction 0.7300, anatomy 0.6320, active-body distribution 0.9613; final 0.7643. Thus anatomical activity overlap alone cannot reject this false-positive class.
- Full Dean overview exposes short aiming/drinking shots that need explicit positive labels and investigation of segmentation, short-window support and temporal-gap rejection. Do not simply cap reuse of a scene: a scene may contain multiple genuinely repeated gestures.
- Latest Soldier run retained 100 results (50 motion/50 pose). It recomputed 2445 pose detections and took 251.359 seconds, whereas earlier 60-second runs reused pose cache. These are **not comparable throughput benchmarks**. Current saved provider is Auto; no user setting was rewritten for these checks.

## Reproduction artifacts (local, not release payload)

- `build/recovery-dean-before.out.log`, `build/recovery-active-dean.out.log`.
- `build/dean-disputed-sequence.jpg`: five temporal samples per side of the disputed pair.
- `build/dean-overview.jpg`: full-source overview for selecting positive/negative intervals.
- `build/dean-poses.json`: raw pose observations from an opt-in `PF_DEBUG_POSES_JSON` dump; excludes face/ReID embeddings.
- `PF_DEBUG_MATCHER` emits accepted motion-component scores for diagnosis. Debug logs are not calibrated probability evidence.

## Still pending

Real-video precision/recall improvement on Dean, calibrated score display, the contextual reversed-NMS edge case, full StaticFrameMatcher separation, full cache policy, live provider switching and final installer/patch archive. No final release is claimed by this checkpoint.

## Follow-up verification — September 28

- Isolated startup initially failed because the development output lacked runtime DLLs. QML-aware deployment plus native dependency closure populated the output. A subsequent apparent hang was a diagnostic configuration error: `QT_QPA_PLATFORM=offscreen` requested a plugin absent from the Windows deployment. Using the shipped `windows` plugin rendered the actual window and exited 0 (`build/recovery-inline-windows.out.log`). No production code change was needed for that diagnostic error.
- The isolated playback fixture then exposed missing FFmpeg CLI dependencies, including `avfilter-12.dll`; this produced the error dialog reported by the user. Added the complete 16-library dependency closure of the copied FFmpeg executable, not just that one DLL. The isolated real-H.264 paired playback test now passes with no skipped tests and no developer PATH, QML import paths or plugin paths (`build/recovery-inline-isolated-decode-fixed.log`, 2.777 seconds). Generated dependencies reside under `build/ucrt64-release`, not in source control. This is not yet a packaged-release or clean-machine certification.
- A new deterministic regression demonstrated opposite head trajectories receiving 0.80928 similarity because shared root/camera translation dominated the direction score. COCO direction now uses anatomical channels when both observations contain measurable articulation, retaining root fallback for rigid translation. Repeated head motion, 1.5x tempo and rigid-translation positive controls pass. The body-energy threshold is not a calibrated real-video confidence threshold.
- Dean after that correction: 8 motion candidates, 17.739 seconds (`build/recovery-direction-dean.out.log`). The disputed 9.34 / 36.37 pair remains at 0.73475. **The original Dean quality complaint is still unresolved.** Increased count is not treated as better precision or recall.
- Soldier after the correction: 100 candidates, 62.412 seconds (`build/recovery-direction-soldier.out.log`). This cached run is not comparable to the earlier 251-second cold run; result retention alone does not establish semantic accuracy.
- Result list, accessibility labels and comparison header now distinguish motion from static pose and head-only pose. Labels and sorting controls use RU/EN translations. The actual rendered-text regression failed before the fix and passes after it (`build/recovery-label-red.log`, `build/recovery-label-green.log`). Scores remain heuristic similarity values, not probabilities.
- Full current CTest run: 4/4 targets passed in 20.66 seconds (`build/recovery-sept28-tests.log`). Independent scoped code review found no important issues in direction-channel handling and type-aware labels; it did not validate real-video quality or threshold calibration.

## User-labelled adjacent aiming shots

- User supplied screenshots at Dean 00:02, 00:03 and 00:04. Recorded the group as candidate positive pairs 2/3, 2/4 and 3/4 in `tools/benchmark/dean-reference.json`; this grouping does not prove temporal motion from screenshots alone.
- Measured detector cut boundaries: 1.25125, 1.75175, 2.50250, 3.50350, 4.25425 and 5.25525 seconds. The three labelled aiming shots span about 1.00, 0.75 and 1.00 seconds; observed sample spans are shorter at 6 FPS.
- Ruling: waive the same-source minimum time gap only for independently identity-verified, explicitly different, non-overlapping shot intervals containing their observations. Keep same-shot and overlapping-observation rejection. The fixed 5–6-second floor contradicted the newly supplied positive montage examples; incorrect scene provenance remains a risk, so no exception is made for labels alone.
- Regression `AdjacentVerifiedShotsAreNotBlockedBySameSourceTimeGap` failed with zero matches before the fix and passes, including missing-identity and overlapping-shot negative controls.
- Static pose support is separate from motion: at least three independently timestamped samples spanning 0.30 seconds; motion support remains unchanged. Extraction uses the same static minimum, and the derived-window cache contract advances to v23 so previously discarded shots are recomputed. `ShortStaticShotHasIndependentTemporalSupport` failed before and passes after; two samples and a short stationary motion request are rejected. This remains pose matching, not the full-frame StaticFrameMatcher still required by the plan.
- All four CTest targets passed after the short-static change (21.12 seconds, `build/recovery-short-static-tests.log`).
- Regression `ThreeAdjacentShotsRetainThreeDifferentScenePairs` reproduced NMS collapsing A/B, A/C and B/C into one result. Known different scene pairs no longer fall through timestamp-proximity deduplication. All 39 MotionMatcher tests pass after this correction; full build and real-video acceptance of this final NMS change are still pending at this checkpoint.
- Final rebuild including this NMS correction and all four CTest targets passed (19.46 seconds, `build/recovery-adjacent-final-tests.log`).
- Cold v23 Dean analysis generated 94 windows / 643 pose detections, 214.918 seconds with verbose matcher logging and TensorRT loaded. Warm final run: 12 results, 18.413 seconds (`build/recovery-short-final-dean.out.log`). **None of the three labelled aiming pairs is retrieved yet.** These fixes remove demonstrated early-filter errors but do not meet real-video acceptance.
- Exact source frames 66/90/114 reproduce the user's three screenshots at approximately 2.75/3.75/4.75 seconds (`build/dean-labelled-shots.jpg`). The user's displayed 2/3/4 seconds were rounded; exact 2.0 seconds still shows the preceding shot. Reference anchors were refined accordingly without production timestamp exceptions.
- New windows now include the desired shots: 2.5025–3.16983, 3.5035–4.17083 and 4.33767–5.17183. For the first two, identity score 0.763 is accepted, but pose DTW similarity is 0.6528 and anatomy 0.4306 (mirrored: 0.6464 / 0.4743), so the static gate rejects them. Thus the remaining failure is in view-dependent pose features/scoring, not solely the time-gap or window-length filters. A global threshold decrease would be unsupported and was not made.
