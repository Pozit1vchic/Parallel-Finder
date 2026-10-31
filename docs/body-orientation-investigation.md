# Body-orientation investigation

Requirement B02 in [the completion audit](quality-requirements.md) remains open.
The production descriptor currently measures torso roll and independent head
rotation; it does not yet estimate signed body yaw/pitch. A successful research
export is not completion of that requirement.

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
graphs are under ignored `build/` directories. They are not application assets,
not in the release manifest, and not activated by ordinary analysis. Code headers
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

Still required before production:

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

This implements and validates the native estimator, **not the complete application
feature**. Next: actual temporal/crop controls, cancellable adaptive post-match
review and separate model-versioned observation persistence; then descriptor,
ordering/filter/UI integration. Ordinary analysis does not yet call this model.
Body pitch, more difficult real orientations and final packaging remain open.


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
