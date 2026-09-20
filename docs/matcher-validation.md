# Matcher regression workflow

The product searches for the same person's repeated movement. A successful
build or a nonzero result count is not an accuracy benchmark.

## Automated checks

`cmake --build --preset ucrt64-release --parallel 4`

`ctest --preset ucrt64-release --output-on-failure`

The tracked `pf_matcher_regression` target covers motion versus static mode,
different people, changed clothes with agreeing face evidence, conflicting
face evidence despite matching clothes, dropped observations, sampling rate,
tempo, overlapping windows, missing joints, and preview alignment. It also
covers consecutive scene cuts, jointly invisible limbs, identity-gated head
crop comparison, orientation disagreement, and correct static result labels.
These are synthetic correctness checks, not learned-model accuracy tests.

`tools/benchmark/run_matcher_regression.ps1` creates a known repeated segment
from a supplied moving-person video. It requires a result at the expected
eight-second offset and fails on zero matches. This only checks the complete
pipeline; repeating a clip is easier than independently performed gestures.

The user supplied three positive anchor pairs in
`tools/benchmark/soldier-reference.json`. They must be checked on temporal
neighbourhoods in the original source. Do not tune the code to exact timestamps.
False-positive examples and additional independent videos are still needed
to establish release-level precision and recall.

Check a saved report with:

`tools/benchmark/check_reference_pairs.ps1 -ReportPath <report.json>`

This checks three positive neighbourhoods, two user-reported cross-person
negatives, and the secondary-character pair at 177/185 seconds. The latter
has matching identity within the pair but does not belong to the dominant
character. The diagnostic reel has its own coordinate mapping in
`soldier-reference-reel.json`; never use those coordinates on the original.
On 2026-09-20, all three positives were retrieved on that reel in combined
mode, as **pose** matches. This does not prove that their motion trajectories
repeat. The overlapping blue-room footage in the reel must not be counted as
an independent correct gesture.

## Search semantics and remaining release checks

- Motion mode requires displacement and a supported time alignment. Held
  poses are not silently promoted to motion matches.
- Combined mode also searches pose similarity. These results are explicitly
  labelled "похожая поза"; they are not step/wave predictions.
- Face identity is an independent gate. A head-only pose comparison requires
  agreeing face evidence and five comparable head landmarks. Missing limbs
  on both sides are not treated as disagreeing limbs.
- Dominant-person groups are built from strongest face links first. Merging
  groups is forbidden if any reliable face prototypes across them disagree;
  a clothing-only intermediate track cannot bypass that veto. This avoids
  treating thresholded face similarity as a transitive equivalence relation.
- Displayed similarity is a heuristic score, **not a calibrated probability**.
- Full-source recall, false positives on independent videos, fresh CPU/GPU
  timings, DirectML/CUDA/TensorRT execution and clean-machine model delivery
  remain release checks. Passing synthetic tests alone is not release approval.

## Identity models

Body-ReID remains available. When independent face observations are available
for both windows, SFace agreement takes precedence over clothing resemblance;
face disagreement vetoes a body match. Scores are similarities, not probabilities.

Install the face models with:

`tools/benchmark/install_face_models.ps1 -Destination <models-directory>`

This verifies the pinned SHA-256 values before installation. Keep the model
licenses with the distribution. Models are discovered in the application's
existing model directories. Missing face models leave the body-only path
available; model runtime errors appear in the analysis status.

Primary references:

- [OpenCV YuNet model and MIT license](https://github.com/opencv/opencv_zoo/tree/main/models/face_detection_yunet)
- [OpenCV SFace model and Apache 2.0 license](https://github.com/opencv/opencv_zoo/tree/main/models/face_recognition_sface)
- [SFace preprocessing and alignment](https://github.com/opencv/opencv/blob/4.x/modules/objdetect/src/face_recognize.cpp)

The initial face cosine threshold follows the OpenCV example (0.363); it is
not a measured false-accept rate for edited TV footage. Scene/track aggregation
and identity behaviour must be evaluated on the release dataset.

## Diagnostics

### Full-source check, 2026-09-20 (identity policy v21)

Local reports: `build/soldier-full-report.json` (before) and
`build/soldier-identity-v21-report.json` (after). Input is the original 269.886 s
source, fast/combined settings, YOLO26m 640 b1, CPU, result limit 50.

- Returned 50 results; the unwanted secondary-character pair at 177/185.667 s
  is absent, as are all results involving its previous track IDs 119/121.
- All three labelled negative pairs are absent.
- 44/50 previous results remain within one second on both endpoints. Five
  additional previous results disappeared besides the reported false match;
  these need manual review before claiming unchanged recall.
- Positive anchor check is **2/3**, not complete: 63/262 s remains missing in
  the capped full-source results. The reference-check script intentionally
  fails until all three positives are retrieved.
- Fresh end-to-end CPU run: 407453 ms. Earlier policy v20 run: 236708 ms.
  These are single runs, not an isolated benchmark; no speedup is claimed.
- All four CTest targets pass; the tracked matcher/scene/identity target
  contains 43 tests. GPU execution and full-dataset precision remain unverified.

Set `PF_DEBUG_MATCHER=1` for preparation/filter/alignment counters.
`run_analysis_smoke.ps1` now prints result JSON and elapsed milliseconds,
accepts `-TimeoutSec`, and can require `-MinimumPairs` / `-ExpectedOffsetSec`.
Always distinguish a cached run from fresh model inference when reporting speed.
