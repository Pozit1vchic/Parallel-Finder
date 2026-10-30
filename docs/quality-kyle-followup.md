# Kyle Crane identity correction — RC.23.3 development

This follows the larger [quality implementation log](quality-implementation.md).
The supplied source is the 66.405-second, 3840×2160 Kyle montage. Source videos,
frames, diagnostic embeddings and downloaded research weights remain local.

## What changed

1. **Architecture.** Detection, identity-safe tracking, identity components and
   confirmation of distinct people are separate stages. A disconnected component
   no longer automatically means another unique person. Ambiguous observations
   remain available for manual selection; they are neither deleted nor forcibly
   merged with someone else.
2. **Modules.** `core/src/DominantPerson.cpp` implements track retirement and
   candidate distinctness; `core/src/FaceAssociation.cpp` prevents one measured
   face from being assigned to overlapping people. `CharacterAnalysisCache`
   persists distinctness in metadata schema 2. `AnalysisController` uses the
   versioned all-character artifact and supplies clean image/box metadata.
3. **Selection.** The selector occupies the center of the main window. Confirmed
   identities are ordered by observed presence. An explicit footer opens
   unresolved appearances. Relative distinctness requires repeated independent
   people in the same frames, or strong face disagreement; absence of a match is
   insufficient. Selection/reselection, cancellation and batch persistence use
   the existing background workflow. Missing identity models produce a clear
   error instead of presenting pose-only tracks as recognized people. Local
   builds now stage the same optional identity assets as the package.
4. **Classifier/matcher.** Head comparisons use observed head coverage, temporal
   support and measured SO(3) rotation distance. A close-up is no longer penalized
   for invisible legs. Body comparisons still require body evidence. Degenerate
   coincident skeletons cannot count as good observations. Body in-plane roll
   is measured independently; signed body yaw/pitch stays unknown when unsupported.
5. **Sorting/deduplication.** Shared scoped similarity/quality feeds presentation
   and ranking. Head families use head evidence. Existing complete-link families,
   strict copied-range verification, canonical export reuse and continuity order
   remain in place. This correction does not lower matcher admission thresholds.
6. **Performance/cache.** All-character representation v8 invalidates prior
   identity artifacts once. Subsequent selection, mode changes and reselection
   reuse the artifact. Clean representative frames have a separate preview key;
   the highlight is drawn by QML and responds to the current theme.
7. **Provider installation.** Persistent verified parts, bounded parallel
   transfers, resume validation, cancellation, Unicode extraction and staged
   probing are covered in the parent log; this follow-up does not change them.
8. **UI/motion.** `CharacterAppearance.qml` replaces the baked white rectangle
   with rounded accent corners and quiet surrounding shading, using the existing
   theme. Preview pixels remain clean. The selector preserves application
   components, typography and reduced-motion behavior. Result explanations name
   head/body evidence; an empty primary list links directly to Possible results.
9. **Tests.** New controls cover tracker identity switches, shared-face ownership,
   unresolved components, repeated distinct people, persistence of distinctness,
   scoped head support, invalid rotations, head families and roll continuity.
   The opt-in real-video UI test requires one long Kyle group and zero new pose
   detections on cached reselection. The standard workflow tests missing models,
   selection, mode change, reselection, cancellation and persisted batch choices.
   The final core/cache focus passed 13/13 checks; the full CTest run passed
   all 12 targets in 124.86 seconds (`build/kyle-v8-suite.log`). Five Python
   metric checks and the four additional-language catalog checks also passed.
   After the final all-unresolved fallback, the workflow check passed again
   (`build/kyle-final-workflow.txt`), as did application startup. The real-video
   cached UI check passed 3/3 in 1,873 ms: zero decoded/pose frames and all
   130/130 representative requests served from cache (241 ms preparation).
   Its inspected screenshot is
   `build/ucrt64-release/tests/kyle-character-selection.png`.
10. **Before/after evidence.** Six visually reviewed membership controls are
    stored in `tests/fixtures/kyle-identity-switch-controls.json`. Replaying the
    **same saved detections** with body-association thresholds .45/.65 gives:

    | Association | Same person correct | Different people correct | Total |
    | --- | ---: | ---: | ---: |
    | Previous .45 | 1/3 | 2/3 | 3/6 |
    | Corrected .65 | 3/3 | 3/3 | 6/6 |

    One reviewed reverse cut previously moved Kyle's track onto the foreground
    woman and prevented it from rejoining Kyle on the right. Another transition
    mixed Kyle with a shirtless man. The corrected replay keeps Kyle in one
    37-track group covering about 64 seconds. This tiny regression set does not
    establish population identity accuracy or retrieval quality. Before scoped
    head evidence, all four Kyle result pairs were Possible; after correction,
    three head pairs are Strong and the partial-body pair remains Possible.
    This describes result categorization, not independently labelled precision.
    A final normal-build CUDA run after the v8 cache change took 30,875 ms,
    produced one Kyle group with 64.117 seconds of observed presence and one
    separately confirmed woman group. The remaining 29 groups were unresolved
    appearances, not extra unique-person claims. That cold-generation duration
    is not compared with a warm-cache run as a speed claim.
11. **Limits.** Strong contradictions and repeated co-visibility are conservative:
    a real secondary character may remain under unresolved appearances. All such
    candidates remain selectable. Long-film cross-camera identity and retrieval
    Recall@K/MRR still need broader independent judgements. General family recall
    and every external GPU/download combination are not validated by these tests.
12. **Commits.** `5fd54be` fixes tracking, face ownership, model staging and preview
    styling. `a26f595` fixes head/body evidence and shared rotation comparisons.
    `460273f` adds relative distinctness, its versioned persistence and the
    all-unresolved fallback. The accompanying benchmark commit retains the
    reviewed controls, replay/evaluation tools and this report.
    The parent log records the preceding architecture and provider changes.
13. **Version/release.** The local Release build is labelled
    `0.1.0-rc.23.3-dev`; it is a development executable, not a published release.
    No release tag, uploaded installer or successful remote CI is claimed.
14. **Remote actions.** No push, tag or publication was attempted. The full
    specification is not declared empirically complete while the validation
    limits above remain. No remote failure or approval block is being inferred.

## Reproduce the reviewed association controls

Use `PF_DEBUG_TRACKS_JSON` during an opt-in all-character diagnostic run to retain
local observed geometry and embeddings; the scene log supplies cut timestamps.
`pf_character_probe TRACKS.json SCENE_LOG .45` and the identical command with
`.65` replay the production tracker without decoding or neural inference.
Then run `python tools/benchmark/evaluate_identity_controls.py REPLAY.json
tests/fixtures/kyle-identity-switch-controls.json`. Every reviewed anchor must
resolve exactly once by timestamp and box; missing anchors are errors, not
silently dropped cases. The tracked fixture contains geometry and judgements,
not image pixels or face/body embeddings.

## Body-orientation investigation

A standalone `pf_pose3d_probe` examines externally supplied SimCC3D models. It
does not download assets, alter application inference or produce claimed body
angles. A local RTMW3D-x export was checksum verified as
`4a289c0e99d47eb595e99679d9d4a2d1def1b4241f9adcbafba44b9ff585ebcd`.
One full-body Butcher frame produced approximately 115–121 ms CPU inference and
4.3–4.9 pixel mean agreement with eight confident existing 2D joints. Agreement
with another detector is **not ground truth for depth or body rotation**.

The [official RTMPose3D project](https://github.com/open-mmlab/mmpose/tree/main/projects/rtmpose3d)
and its [3D codec](https://github.com/open-mmlab/mmpose/blob/main/projects/rtmpose3d/rtmpose3d/simcc_3d_label.py)
use explicit depth/root-scale conventions; the
[pose estimator](https://github.com/open-mmlab/mmpose/blob/main/projects/rtmpose3d/rtmpose3d/pose_estimator.py)
reconstructs camera coordinates using intrinsics or defaults. Arbitrary movie
crops and zooms have not been calibrated here. The external export and its
redistribution provenance also require review. Accordingly this research model
is not bundled or activated, and raw depth bins are not presented as degrees.
