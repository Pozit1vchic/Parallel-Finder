# AVA video-action classifier development audit

## Problem and scope

The reviewed, newly retained Soldier Boy walking pair remains Unknown under
the geometric classifier. At Gen V 5.84–7.34 s there are no confident ankle
observations and only three confident knee observations. S03 1652.03–1653.69 s
has the same cropped-leg limitation. Weakening alternating-foot requirements
would manufacture gait evidence that is absent from the pose data.

The next classifier layer therefore evaluates the selected person in original
video, independently of actor identity and matcher admission. Existing identity
thresholds, user pose caches and public release are unchanged.

## Model and primary sources

The official [PyTorchVideo AVA detection tutorial](https://pytorchvideo.org/docs/tutorial_torchhub_detection_inference)
documents person-box action inference and its RGB/temporal/normalization
contract. The [model implementation](https://github.com/facebookresearch/pytorchvideo/blob/main/pytorchvideo/models/hub/slowfast.py)
provides the pretrained SlowFast R50 AVA checkpoint. AVA is a multilabel
person-action dataset; these outputs do not verify actor identity.

Pinned construction package: PyTorchVideo 0.1.5. Official checkpoint:
`https://dl.fbaipublicfiles.com/pytorchvideo/model_zoo/ava/SLOWFAST_8x8_R50_DETECTION.pyth`,
271252349 bytes, observed SHA256
`9bd6584a8212f91e4c7d96ec7d6bff31250e1c810fcee968471614a1b9658ac8`.
Checkpoint loaded with `weights_only=True`; all trained state keys match.
Code license and its pinned revision are recorded in the model metadata.

Exported model is preserved outside deletable build folders in
`models/ava-slowfast-r50.onnx`: 135286691 bytes, SHA256
`b9316ea2de6a29282fa1567246dc74670052525319e48cbab02b6b91ff10c481`.
Metadata and license are saved beside it. No Python runtime is required by the
native estimator. The later integration checkpoint below describes its current
application and release-staging use; this foundation section records the
earlier model-only audit.

## Temporal and person checks

`tools/benchmark/audit_ava_actions.py` runs bounded local cases, including
forward, reverse, repeated midpoint, and repeated midpoint with synthetic
camera zoom/pan. Boxes come from fresh native re-detection, spatially associated
to the previously selected pose. Spatial association is not identity proof.
All sources stay local. Intervals are explicit and bounded; individual model
scores are not presented as calibrated accuracy.

Nine short cases across Gen V, S03, S04 and Interview have been reviewed:

- Gen V and S03 cropped-leg approaches: walk output approximately .9998/.9999.
- Independent S03 wide approach: .9858; frozen control .0553.
- S04 approach, involving a different person: .8213, reverse .6216, frozen
  .1655. This is a classifier-only positive, not a Soldier Boy identity link.
- Standing dialogue, Interview head/hand motion, hand-to-mouth and sideways
  bending/reaching cases stay below .19 in the original-direction walk output.
- Across these cases, repeated-camera controls stay below .057 in walk output.

The first Gen V standing interval inadvertently extended into the following
shot. Contact-sheet inspection caught it; its end was restricted before the
38.288 s boundary and the test rerun. The case named `s03-drink` is only a
hand-to-mouth negative for walking, not a verified drinking label. A model's
high walk score on a few examples is not broad professional accuracy.

## Native implementation and numerical checks

Added `pfgpu::VideoActionEstimator`, accepting owned, continuous RGBA frame
views and explicit midpoint person boxes. It performs bounded uniform
32-frame sampling, aspect-preserving short-side-256 bilinear preprocessing,
RGB normalization and two-input ONNX execution. Invalid boxes, inconsistent
frame geometry, unbounded clips and malformed output scores are rejected.
The estimator returns all 80 raw scores and cannot choose or admit an actor.

The runtime now supports graph-ordered multiple float inputs. Tests cover
second-input allocation/name failures, cleanup, input-count and shape errors,
and a real two-input subtraction graph that detects swapped bindings.
Existing single-input CUDA graph execution remains intact.

`tools/benchmark/export_ava_actions.py` checks ONNX export with two widths and
one/two subject boxes: maximum CPU Torch/ORT difference 2.24e-7. Native decoder
and estimator runs cover five real clips with forward/reverse/frozen controls
on CPU and CUDA. Both providers use identical original PTS and retained boxes.
With CUDA's default math policy, maximum difference across all action outputs
is .002452; relevant walk decisions on these examples remain separated. This
is numerical evidence on those inputs, not a general precision guarantee.

Native decoder preprocessing uses the app's fast-bilinear RGBA path, whereas
the first Python FFmpeg audit used its default resize. The native semantic
checks therefore matter separately from the Python demonstration.

Regression reports reside in `research/soldier-new-sources-20261014/`:
`ava-foundation-regression.xml`, `real-single-input-cuda-regression.xml`,
`ava-native-cpu.json`, `ava-native-cuda.json`, and
`ava-native-provider-parity.json`. Two explicit real-asset tests verify the
existing pose model and four old single-input models through CUDA graph and
ordinary execution. Optional absent b1/b8 assets are not fabricated.

Final combined core/runtime regression: 240 passed, five opt-in cases skipped.
The existing real CUDA pose asset and four-model graph regression were run
explicitly and both passed. Full UI suite: 48 passed, six opt-in cases skipped.
The real Gen V confirmation/trim/exact-export test was run separately and
passed (three QTest entries, no skipped checks). Application rebuilt after
the multiple-input runtime change. These counts precede the application
integration described below and do not validate that later change.

## Integration checkpoint after the foundation audit

The application now invokes AVA on eligible, identity-verified motion pairs.
It checks native-frame scene continuity, freshly associates the selected torso,
and compares walking against a repeated-midpoint control. Both sides must
independently support walking. Source-generation/model/preprocessing keys live
in a separate bounded action cache, without modifying the pose cache format.
Manual classifications take priority. Edited automatic intervals are reviewed
again rather than inheriting evidence for their old boundaries.

The actual Gen V/S03 pair at approximately 5.84–7.34 / 1652.03–1653.69 seconds
received `step/toward_camera` in an application run with 38 total results.
The CPU producer integration test passed again after endpoint-coverage guards:
cold/warm cache, generation invalidation, cut veto, cancellation and preservation
of a sibling pose-cache sentinel (three QTest entries, 22.981 seconds).
The expanded UI trim/sorting/pixel-exact export test subsequently passed
(three QTest entries, 46.313 seconds). It verifies automatic walking after
shortening one side, monotone class groups in the QML results rail, ordered
folders/file numbering, and every decoded RGBA frame of both walking exports
against the original half-open source intervals, including absence of extra
trailing frames. This is not a native 10-bit/HDR or After Effects compatibility
test. An earlier test run hit the default 300-second QtTest function limit;
another exposed an incorrectly configured test preset, fixed by explicitly
matching the recorded application parameters.

Full integrated core suite: 442 passed, 24 opt-in cases skipped, no failures.
Full integrated UI suite: 48 passed, seven opt-in cases skipped, no failures
(34.033 seconds). Actual-source producer CUDA rerun also passed all three
QTest entries (12.898 seconds), including the updated coverage/cut checks.
The complete UI scenario passed again with warm caches (30.350 seconds).
Both original sources reported complete cache hits and zero pose detections;
head review reused all 665 requests without decoding, and action review reused
all four observations without decoding. All 38 result pairs remained present.
The trim/sort/pixel-exact export assertions passed on this warm run too.

## Remaining work

Recheck the actual walking pair after editing in the UI and class-ordered video
export. Extend and independently validate other actions; this audit does not
complete hood/phone/expression/body-turn recognition or masked source admission.
