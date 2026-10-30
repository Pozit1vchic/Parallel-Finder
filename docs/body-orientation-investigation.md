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
identify GPL-3.0 ancestry; checkpoint redistribution terms have not been established
by this investigation. That is an open packaging review, not an inferred grant.

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
