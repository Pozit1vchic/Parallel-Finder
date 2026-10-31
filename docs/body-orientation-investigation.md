# Body-orientation investigation

Requirement B02 in [the completion audit](quality-requirements.md) remains open.
The analysis pipeline now optionally measures signed body yaw when the pinned
local model is installed, independently of head rotation and torso roll. Body
pitch remains unknown. The broader orientation/UI/release requirement is not
closed by the limited real-source controls below.

## Candidate and provenance

The author's [JointBDOE implementation](https://github.com/hnuzhy/JointBDOE)
provides a body detector with an additional continuous orientation output. Its
[README](https://github.com/hnuzhy/JointBDOE/blob/main/README.md) links the
[author's model repository](https://huggingface.co/HoyerChou/JointBDOE).
This offers a direct body-orientation measurement to investigate, alongside the
previous RTMW3D research, whose camera/depth conventions were not established
for arbitrary movie crops.

Pinned research inputs:

- Source revision: `362f999e22bd50a4e73aca882b58c13f8a96a13c`.
- Actual released asset: `coco_s_1024_e500_t010_w005_best.pt` (25,166,533 bytes).
  The README examples use a different training-run suffix; this record identifies
  the downloaded asset itself, not a guessed equivalent.
- Original checkpoint SHA-256:
  `bc6d63ee0f685a888e5ff94a84d8244ce23a817223010e100459137bacae3e27`.
- Fixed-shape ONNX export: RGB `1×3×1024×1024`, letterbox padding 114, divide by
  255; `1×65280×7` output with xywh/objectness/person score/raw angle divided by
  360. The score belongs to person detection, not calibrated angle correctness.
- ONNX SHA-256:
  `3bdfefba3f0bbcd23454ceefaa3088c2e95a7b9d120502899f65249ae98e1c93`.

The author sources, original checkpoint, isolated Python environment and generated
graphs are under ignored `build/` directories. They are not in the release
manifest; the integration below uses a hash-verified optional developer copy.
Code headers
identify GPL-3.0 ancestry. A follow-up inspection found that the author's model
card explicitly declares MIT at repository revision
`8ed0d81c7fb24c3b3e1d4fbb79fe2eaf6e15ccb3`; see the pinned
[model card](https://huggingface.co/HoyerChou/JointBDOE/blob/8ed0d81c7fb24c3b3e1d4fbb79fe2eaf6e15ccb3/README.md).
The declaration is preserved in candidate metadata. The source code's license
and dataset annotation terms are distinct records; packaging attribution/notice
review remains open. No model has been silently added to release assets.

## Reproducible export

`tools/model_export/export_body_orientation.py` requires the exact source
revision, unchanged tracked source and checkpoint hash above. It downloads
nothing and loads the original checkpoint with PyTorch's weights-only loader
and the pinned object graph's explicit class allowlist. It refuses existing output
paths, validates the ONNX graph, then finalizes staged output and metadata.

The isolated local environment uses CPython 3.12, CPU torch 2.9.1 and torchvision
0.24.1. Other research dependencies are numpy, onnx, onnxruntime, opencv-python,
pyyaml, matplotlib, pandas, scipy, seaborn, tqdm and requests. The exact local
installation logs remain in `build/body-orientation-*-install.log` and
`build/body-orientation-dependencies.log`; these are not application dependencies.

Example after obtaining the pinned upstream inputs:

```powershell
& .\build\body-orientation-cpython\Scripts\python.exe `
  tools/model_export/export_body_orientation.py `
  --source-root build/research-jointbdoe `
  --checkpoint build/research-models/jointbdoe-s.pt `
  --output build/research-models/body-orientation-research.onnx
```

The checked-in recipe reproduced the exact graph SHA-256 above on this machine.

## Observed evidence and remaining checks

Three real source frames (Butcher by the car, standing behind another person,
and seated with a foreground bystander) were evaluated. At 640 input, individual
CPU inferences took approximately 56–68 ms in one local run; no general throughput
claim is made. Raw angles were recorded without presenting them as body yaw.
An apparent person detection on an empty chair demonstrates why this model must
be associated with independently observed pose/person tracks before use.

For the fixed 1024 export, a numerical comparison with the original PyTorch
predictor on those same three images produced:

| Quantity | Largest observed difference |
| --- | ---: |
| Predicted box coordinates | 0.009553 input pixels |
| Objectness/person scores | 0.000001938 |
| Raw model orientation | 0.002726 degrees |

Local evidence: `build/body-orientation-export-equivalence.json`,
`build/body-orientation-reproduced-export.log`,
`build/body-orientation-research-output.json`. These establish export fidelity,
**not ground-truth angle accuracy**.

Initial validation queue (the subsequent sections record progress on items 1–4;
broader independent controls, UI and release gates in item 5 remain open):

1. Establish angle zero/sign/front/back convention with independent real view
   labels, including wraparound, left/right and body/head disagreement.
2. Measure association ambiguity against existing person boxes/head landmarks;
   reject unsupported bodies and nearby/double detections rather than borrowing
   another person's direction.
3. Evaluate temporal, crop and horizontal-flip consistency. Missing or conflicting
   evidence must retain uncertainty; objectness cannot substitute for it.
4. Profile actual CPU/CUDA ONNX execution and determine cancellable adaptive
   sampling/cache versioning without repeating full video inference.
5. Complete checkpoint/license/distribution review and then implement the measured
   orientation contract, UI interpretation and continuous sorting tests.

The timestamped source survey at `build/body-orientation-source-survey.jpg` helps
select further real controls. It contains user-provided video pixels and remains
local, outside source control.


## Native body yaw implementation and real controls

`pfgpu::BodyOrientationEstimator` now executes the pinned fixed-shape ONNX model
through the application's existing session cache and CPU/CUDA provider layer.
It uses an independently implemented bilinear RGB byte resize, integer letterbox
padding and normalization, validates input/output contracts, applies person-score
NMS, and maps detections back to source pixels. Mirrored detections are mapped
back spatially and angularly. It returns yaw only: pitch and roll are not invented
from a model which supplies neither.

The signed convention is `yaw = remainder(180 - 360 * raw, 360)`:
zero faces the camera, +90 faces screen-left, -90 screen-right and +/-180 away.
The author's visualization uses `(-sin(raw), -cos(raw))`; real frontal, side and
back views corroborate the conversion. Continuous values are retained, not
quantized to label buckets or substituted from the head estimator.

`associateBodyOrientations` requires mutual unique subject/detection association
in both views: IoU at least .5 and a margin of .15 over competing detections and
subjects. Horizontal-flip yaw disagreement above 25 degrees remains unknown.
A circular midpoint handles the +/-180 seam. Detection confidence is retained
as person-detection evidence, not advertised as angle accuracy. Spatial and mirror
support do not by themselves prove temporal continuity or identity.

`tests/fixtures/body-yaw-observations.json` retains actual native CPU detections
for 11 visually reviewed source controls, source/frame hashes, target rectangles
and broad acceptable yaw intervals. No image pixels or face embeddings are
committed. The view intervals were chosen before inspecting model output; four
approximate rectangles were corrected from full-resolution source pixels after
an initial association pass, with this disclosed in the fixture. These are coarse
visual judgements, not measured 3D ground-truth angles.

CPU and CUDA runs both support **9 of 11** controls, all nine within their reviewed
coarse intervals, with **two unresolved** cases. The unresolved Kyle views remain
misses of visible body views, not renamed correct negatives: the side view has
insufficient overlap and the back view's original detection misses the requested
person. The latter prevents borrowing the shirtless bystander's frontal angle.
The balcony control observes the standing torso around +66 degrees while its
head looks toward the camera; a seated foreground person remains a separate
candidate. The foreground back-view control remains near +/-180 across a flip.

Local reports in `build/body-orientation-controls/`:

- `native-cpu-four-threads.json` and `native-cuda.json`: supported/coarse-correct
  9, unresolved 2, coarse-wrong among supported 0 on both providers. Maximum
  supported CPU/CUDA yaw difference is 0.456 degrees on these controls.
- Median warm **two-view inference + resize/association** is 346.9 ms on CPU
  (four threads), 24.7 ms on CUDA; first control excluded. File loading, source
  video decode and the rest of analysis are outside this timer. This is not an
  application throughput claim.
- `native-reference-parity.json`: 45 matched detections compared to an OpenCV
  preprocessing reference. Largest yaw delta is 0.889 degrees, normalized box
  coordinate delta .00460 and detection-score delta .00601. Fixed-point byte
  resizing is not claimed bit-identical to OpenCV. Pixel tests allow one byte.

`body-yaw-replay-tests.log` passes four controls: preprocessing/mirroring,
decode/NMS/contracts, one-to-one association/unknowns/circular midpoint, and
production association replay of the frozen real detections. Replay asserts both
the supported cases and the two unresolved cases, so abstaining on every frame
cannot pass. `pf_body_orientation_probe` verifies the pinned model hash and
re-evaluates actual source frames; it refuses an existing output path.

At this native-estimator milestone, ordinary analysis did not yet call the
model. The subsequent integration below adds post-match temporal/crop review,
persistence and descriptor/continuity integration. Body pitch, more difficult
real orientations, user-visible directional controls and final packaging remain
open.


Exact private input reproduction is now checked in as
`tools/benchmark/prepare_body_orientation_controls.py`. It verifies both original
video hashes, seeks recorded zero-based frame indices, checks actual frame
position and decoded/resized RGBA hashes, and refuses an existing output folder.
The fixture also records original requested and actual timestamps, dimensions and
OpenCV version. `pf_body_orientation_probe` checks RGBA hashes when provided, so
changing PNG compression cannot masquerade as changed model input. Reproduction
and fresh CPU/CUDA runs are retained under `build/body-yaw-exact-reproduction`.

The final Release build and all 13 CTest targets pass in 130.37 s
(`body-yaw-full-suite.log`), in addition to the separate actual-model runs.

## Post-match application integration

`BodyOrientationReview.cpp` now runs from `AnalysisController` after head/action
review and before family organization. Only body comparisons request work;
head-only analysis and pre-cancelled work do not open either model or create a
cache. For each compared interval it selects three distinct observed pose times,
deduplicating requests shared across results. It re-runs the existing pose model
on these few frames because persisted pose frames do not contain detector body
boxes, and verifies ownership against the original observed skeleton. It never
uses the largest person as an identity decision.

An accepted sample requires visible torso support, mutual spatial association,
genuine original/flip agreement and a second genuine original/flip inference on
an expanded crop. Full-frame and crop yaw must agree within 20 degrees. Three
valid, sufficiently spread observations are needed for the interval descriptor;
conflicting duplicates, wrong source/model/pose ownership, incomplete coverage,
out-of-scene ranges and unsupported angular jumps remain unknown or ambiguous.
The midpoint sample retains its continuous signed angle across the +/-180 seam.
The support score describes observation support, not calibrated angle accuracy.
Head rotation and unsupported body pitch are not inferred from this value.

The result-only observations are copied into grouping evidence and exposed in
the side descriptors (`bodyYaw`, `bodyView`, support state/counts). Existing
continuity ordering consumes the now-populated continuous body yaw. The primary
pose cache format and matcher admission are unchanged. Orientation filtering
and broader trajectory/scope-family review remain open; the subsequent readout
section adds the user-visible direction explanation.

The separate 64 MiB `movement-refinement-v1/body-orientation-v1` cache includes
source generation, model hashes, observed points, pose inference settings and
provider in each request key. An absent or empty source namespace disables
persistent reuse. Only completed observations are stored; cancellation retains
earlier complete samples while unfinished slots stay unknown. Source mutation
during a measurement discards this review. The model is hash-verified before
reuse; the developer copy is optional and is not a release-package declaration.

`pf_body_review_tests` runs the real first Butcher shot with native decoder,
YOLO26x pose and native body orientation. Coarse frontal expectation comes from
the existing reviewed `butcher-0` control; target selection uses the reviewed
right-half body location independently of predicted yaw. At actual timestamps
0.021000, 0.438083 and 0.813458 seconds, CUDA observes 14.627, 20.577 and 27.106
degrees. CPU also supports all three; its latter angles are 20.581 and 27.111.
These are three nearby frontal observations, not a general accuracy benchmark.

Both runs prove three unique cold requests, three warm cache hits with zero
decode/pose/body calls, descriptor/presentation/grouping propagation, no
persistent reuse with an empty namespace, and cancel-after-one/resume with one
cached plus two newly measured samples. CUDA takes 11.243 s and CPU 20.970 s for
the entire test including fixture preparation and repeated invalidation runs;
these times are not per-analysis latency or a controlled GPU speed benchmark.
Evidence: `build/body-review-real-cuda-results.txt` and
`build/body-review-real-cpu-results.txt`, five checks pass in each. The nine
`SceneDescriptor` controls also pass (`body-review-descriptor-tests.log`).

Final Release rebuild and all **14/14 CTest targets pass in 119.33 s**
(`build/body-review-full-suite-verified.log`), including 56 UI checks (seven
explicit opt-in skips) and six-language stage-counter assertions. The first
fresh-build run passed 13/14: isolated startup failed with Windows missing-DLL
status `-1073741515` before runtime deployment. The existing
`tools/release/deploy-runtime.ps1` staged the dependencies; the focused
startup/UI replay and subsequent full run pass. No startup test was weakened.
This is a developer runtime verification, not clean installer/upgrade proof.
The final cache-key profile/batch/keypoint fields also have a fresh five-check
CUDA real-source run (`body-review-real-cuda-verified-results.txt`, 11.249 s).

## Direction readout in Explain Match

`BodyOrientationReadout.qml` adds paired A/B columns to the existing centered
explanation: `A (view / signed angle / supported frames) | B (same fields)`.
The layout follows the actual comparison surfaces. It reuses the existing
near-black canvas (`#0C0D10`), panel, primary text (`#F5F1EC`), secondary text
(`#A1A1AA`), hairline (`#3F3F46`) and user-selected accent, Segoe UI Variable
for language and the existing numeric font for angles. No extra cards, motion
or palette are introduced. The popup scrolls on a short window while its close
button stays outside the scroll area. Existing reduced-motion settings apply.

Eight localized view names accompany the continuous angle; labels do not feed
back into similarity or quantize stored angles. Zero is a valid angle, signed
values near +/-180 retain their sign, and head yaw does not supply a missing
body yaw. The component requires supported body evidence and at least three
observations, showing Unknown or Ambiguous with no angle otherwise. Counts are
supporting frames, never a probability or an orientation-accuracy percentage.
The reference text explicitly defines image-left/image-right relative to the
camera. All new copy is present in six languages.

Focused UI checks cover all eight directions, zero, the angular seam, independent
head/body values, missing/out-of-range/insufficient/ambiguous evidence, six
languages, short-window close-button access and reduced motion. The actual
resource-built popup screenshot was inspected:
`build/ucrt64-release/tests/body-direction-readout.png` (synthetic measured-value
fixture, not a recognition result). Full UI regression: **57 passed, seven
explicit opt-in skips**, plus application smoke passes
(`build/body-direction-ui-suite.log`, 42.72 s).

## Sparse upper-body observations and difficult temporal controls

`tests/fixtures/body-yaw-temporal-controls.json` contains four reviewed source
intervals, each with three requested times, target rectangle, source hash and
coarse yaw interval chosen before running temporal inference. A private contact
sheet was inspected (`build/body-temporal-survey.jpg`). The foreground-back and
shirtless-neighbour intervals end before their visible next-shot cuts. The left
profile subject in the Kyle montage is a woman; the fixture explicitly does not
call her Kyle or claim an identity-recognition result.

The first run exposed an architectural mismatch: a six-visible-joint articulated
pose check was also being used to recover a detector's body rectangle. This
rejects actual upper-body observations when elbows, hips and facial landmarks
are occluded. `associateObservedPose` retains that requirement for articulated
pose/action evidence. The new `DetectorOutputReplay.hpp` separately locates the
already selected observation at the **same source/model/settings/timestamp** by
comparing all 17 raw coordinate/confidence triples, including low-confidence
values as a numerical output fingerprint. It requires maximum point error
<=1e-4 image diagonals, confidence error <=1e-3, a unique runner-up margin of
2e-3 diagonals and nondegenerate coordinate spread. This is detector replay,
never a claim of actor identity across frames or reliable invisible anatomy.

Only this stronger same-frame replay enables an upper-body observation with one
>=.8 shoulder/hip anchor inside the actual detected body box. The person score
>=.4, actual original/flip and crop/flip body inferences, mutual IoU >=.5,
association margin >=.15, mirror difference <=25 degrees, crop difference <=20
degrees, and interval temporal checks remain required. Full articulated pose
quality is not promoted by the alternative body-orientation path. A new v2
request-key namespace prevents reusing earlier unknown/accepted measurements;
the expensive primary pose/identity cache format remains unchanged.

Controlled actual-model comparison on the same 12 reviewed frames:

| Measure | Previous review, CUDA | Detector replay, CUDA | Detector replay, CPU |
| --- | ---: | ---: | ---: |
| Coarse-correct supported samples | 4 | 6 | 6 |
| Supported samples outside reviewed interval | 0 | 0 | 0 |
| Visible but unresolved samples | 8 | 6 | 6 |
| Supported interval descriptors | 1 / 4 | 2 / 4 | 2 / 4 |

The profile interval now supports all three samples around -81 degrees; the
balcony interval remains around +68 degrees despite its camera-facing head.
The six back-view samples remain **misses**, not correct negatives: full/cropped
mirror disagreement or missing crop detection prevents a supported result.
The shirtless neighbour's frontal angle is not borrowed. These are correlated
frames from four intervals in two montages, not population accuracy or measured
3D ground truth. Minimum-support regression floors preserve the two measured
positive controls; they are explicitly separate from the visual labels and do
not relabel the other visible bodies as absent.

Reports: `build/body-temporal-difficult-cuda.json` (before),
`body-temporal-difficult-cuda-v2.json` and `body-temporal-difficult-cpu-v2.json`
(after), with `body-temporal-comparison.json` retaining report, fixture and model
hashes. Native YOLO26x pose SHA-256 is
`a536ab7f2220b109543455801ea2bd4142c90044cf1aad46a48e044a92564419`;
body model hash remains the pinned one above. Six opt-in Qt checks pass on CPU
in 95.866 s and CUDA in 35.336 s, including both difficult controls and the
previous frontal cold/warm/cancel/resume check. These whole-test times include
different amounts of setup and are not a controlled provider speed benchmark.
Maximum CPU/CUDA yaw difference among the six supported difficult samples is
0.01591 degrees. Both accept all 12 original detector-output replays; warm runs
retain identical known/unknown samples with zero decoding or either inference.
Unconfirmed candidate yaw is normalized internally and serialized as null, so
cached unknowns cannot acquire a fabricated frontal label or retain a different
candidate angle. Crop failure reasons now distinguish missing detection from
mirror/association conflict in the diagnostic samples.

Fifteen core checks pass across scene descriptors, articulated correspondence
and raw-output replay (`build/body-output-replay-core-tests.log`), including
independent simultaneously observed head/body values, sparse replay, unique
ownership, modified raw triples, wrong time, invalid numbers and duplicates.
Broader real orientation recovery, dimension filters/sorts, supported pitch and
release packaging remain separate open requirements.

Ordinary analysis also retains the primary decoder's NVDEC/CPU override and
validated automatic four-worker conversion policy for detector replay. The v2
request fingerprint records decode backend/device/resize/conversion options and
working dimensions. A separate real run requests NVDEC and asserts the actual
backend, rather than silently accepting CPU fallback. All four controls use
NVDEC and four conversion workers; all 12 same-frame detector replays succeed,
with the same six supported/coarse-correct samples, six misses, zero supported
coarse errors and warm zero-inference reuse. Evidence:
`build/body-temporal-difficult-nvdec.json` / `.txt`, six checks pass in 34.898 s.
This validates the hardware decode path too, not a provider speed benchmark.

The readout additionally suppresses retained evidence for a removed A/B side;
removing A leaves B's independent angle intact. This is covered in the actual
popup UI check. The subsequent full UI/selector/application-smoke replay passes
(`body-direction-side-state-ui-suite.log`, 42.93 s), including 57 UI checks and
seven explicit opt-in skips.

Final current-tree Release verification: all **14/14 CTest targets pass in
115.39 s** (`build/body-direction-nvdec-final-suite.log`), including 57 UI checks
and the separate live-process/real-media export cancellation controls. The
export test's earlier five-second file-size watcher failure and its phase-based
replacement are documented in `export-cancellation-evidence.md`; no CutService
production behavior changed. Full installer/upgrade and remaining quality
requirements are not closed by this run.
