# Investigation evidence and temporal object classification

The user requested that obsolete builds be removed without losing investigation
results or the analysis cache. Before attempting cleanup, 2,643 selected files
(268.74 MiB) were copied to `research/preserved-evidence-20261014`. The copy
includes reference frames, JSON measurements, numerical descriptors, logs and
test reports. Every copied file was checked against the source SHA-256; the
manifest contains its relative path, byte count and checksum.

Large downloaded model weights, executable environments, generated export
videos and repeated test caches are not part of this compact evidence copy.
Pinned model provenance remains in the investigation reports and download
entry points. Source code and small regression fixtures remain in the project.
The recursive cleanup command was rejected by the tool's policy, without a
specific reason. No build directory was removed. User analysis caches and
original source videos were not changed.

`VisualObjectEvidence.hpp` now separates a stable phone location from a
directional phone action: hand to ear and ear to hand. A directional hypothesis
requires independent hand-motion support, verified scene continuity, four
independent frames with repeated endpoint states, and one non-reversing
transition. An absent or occluded phone does not establish a hand endpoint.
Repeated ear observations establish location, not movement or speaking.

This remains a hypothesis component, not a production visual-model integration.
The caller must provide validated per-frame object observations and spatial
subject association. The professional classifier and cross-view matcher remain
unfinished; these tests do not establish their real-world precision or recall.

Validation: 154 tests passed across VisualObjectEvidence (6), PoseAssociation
(3), SceneWindow (6), MovementGrouping (29), and MotionMatcher (110). Report:
`research/preserved-evidence-20261014/semantic-temporal-regression.xml`.

## Per-frame visual-model audit

`tools/benchmark/audit_visual_object_states.py` collected single-image object
observations for all nine previously selected sequences using the pinned local
Qwen3-VL model and projector. Files were fingerprinted again before execution;
the server was confined to loopback and explicitly stopped after the audit.
Output is `research/preserved-evidence-20261014/object-states-audit/report.json`.
This changes no application classification or user cache.

The front hood sequence returned `none, hood, hood, hood`. Direct visual review
of frame 00191 (38.0 s) contradicts the second prediction: the head is still
uncovered and the hood is behind it. The off sequence returned `hood, hood,
none, none`; review shows that the later frames are transitional/partially
covered, not sufficient proof of a fully removed hood. Endpoint-only checks
previously looked promising, but these extra frames disprove a claim of robust
state classification. Correct cap/phone examples do not compensate for these
errors. Do not connect these model predictions to automatic semantic grouping
or matcher admission. A specialized, independently validated observation
producer remains necessary; adding temporal voting alone is insufficient.

## Compound head actions in the application

The production orientation classifier previously returned only `Mixed` for
independently measured yaw and tilt/pitch. It now retains `head_turn` plus
`head_tilt` or a returning `head_nod`. The geometric fallback likewise retains
facets only when the existing torso-relative independent vertical-motion guard
passes; cropped yaw still cannot invent a nod from narrowing shoulders.
Full-body grouping merges these measured facets with independent arm movement.
Compound head turns can retain their camera-relative away/back phase using the
same endpoint and pitch uncertainty rules as a single turn.

These facets flow through existing result classification and compound sorting.
Range-edit tests verify `head_turn_away+head_tilt` for both head-only and
full-body results. Pair disagreements still cannot borrow one side's facets;
manual classes retain priority. Matcher admission thresholds are unchanged.
The current development application was rebuilt; no release was published.

Final core regression: 162 tests passed. Full UI run: 48 passed, 5 opt-in
tests skipped. After the last geometric-facet addition, focused range-edit and
class/export-order UI checks were repeated on the rebuilt binary. This fixes
lost compound labels; it is not evidence that object actions or arbitrary
cross-view matcher recall are complete.

## Measured head evidence takes priority

A new regression reproduced another classification defect before the fix:
when geometric face keypoints suggested compound yaw/tilt but the orientation
model measured a static head or isolated tilt, full-body grouping retained the
contradictory geometric facets. The `geometricHead` switch previously guarded
only compound arm/head addition, not the later head fallback.

The fallback now respects that switch throughout. When model observations are
present, the body-static decision excludes facial keypoints and requires eight
observed body joints; the measured head is classified independently. Missing
or uncertain head measurements cannot certify a completely static person.
Independent measured arm actions remain usable even when the head is missing.
This does not alter matcher identity/admission rules or persistent analysis
observations.

Rebuilt application and tests. The new contradictory-evidence test failed on
the previous implementation, then passed after the fix. Final regression:
163 core tests passed; full UI suite passed (48 passed, 5 opt-in skipped).
Report: `research/preserved-evidence-20261014/head-evidence-precedence-regression.xml`.

## Deploying the head-orientation model

The audited ONNX was present only under `build/movement-review`, outside the
normal application's model discovery roots. The Windows packaging script
copied pose/body/face models but omitted it. The exact audited model and original
MIT license are now preserved under project `models`, with versioned metadata,
and staged in the current application's `models` folder. This protects the
necessary asset from deleting old build folders and makes it discoverable by
ordinary app-local model lookup.

`stage-classifier-model.ps1` checks pinned size/model SHA-256 and license SHA-256,
copies model/metadata/license, validates copied bytes, and refuses conflicting
existing assets. Both runtime deployment and release packaging invoke it.
Verified success and idempotence, missing-model rejection, conflicting-file
rejection without overwriting, and loading the staged ONNX for three finite
3x3-output inference cases. These last inputs are preprocessing test images,
not an accuracy benchmark. No full video analysis or release publication was
performed. The professional-classifier and matcher requirements remain open.

## Real-source check of the staged model

The head-review integration test now accepts an explicit case prefix. Default
full-set assertions are unchanged; subset runs identify their scope in the
report and fail on an empty selection. This permits checking the still-present
Business Batya source without silently skipping deleted source videos.

The staged application model was tested on five `batya-1-independent-*`
intervals through `reviewMatchedHeadOrientations`, not a substitute Python
inference path: 61 requests, 45 accepted measurements, 16 unobserved. The warm
pass reused all 61 entries, decoded zero frames, and exactly preserved measured
matrices/observation flags. Generation changes prevented old-entry reuse;
cancellation stopped the new pass. Assertions also confirmed unchanged original
pose timestamps, keypoints, source IDs and scene bounds. User analysis caches
were not used by this test (a separate temporary head-measurement cache was used).

Both head-only and full-body classification outputs were recorded. Four of the
five head-only cases yielded static/tilt labels, one remained unknown; **all five
full-body cases remained unknown**. Thus successful model inference/cache reuse
does not establish good complete-scene grouping. Further work must examine body
visibility/unknown-action gating without relabelling an unknown hand action as
head movement. No actor-identity or matcher-quality claim follows from these
Batya cases. The previous visual labels are provisional, not certified ground
truth. Report: `research/preserved-evidence-20261014/staged-head-real-source-v2.json`;
integration test: 3 passed, 0 failed, 0 skipped, about 20 seconds.

## Retaining measured head motion in cropped scenes

Coverage inspection distinguished the real missing-body case from unclassified
visible actions. Case `batya-1-independent-3` has both shoulders throughout but
no visible wrists, hips or knees. Other cases include full-body/arm evidence
that remains unclassified. A full-body grouping fallback now retains a measured
non-static head action only when no arm/hip/leg joint has at least 75% temporal
coverage. It requires the existing valid shoulder/pose observations and head
model coverage; it never invents a static full scene from a static cropped head.

`MovementClassification.headOnlyEvidence` records that restricted scope. The
result row exposes `classificationPartial`; the card says that head motion is
observed but the body is undetermined. A manually assigned type still takes
priority. Pair agreement is required before carrying the partial group. Visible
unclassified arm motion does not use this fallback. Matcher admission and old
cache measurements are unchanged.

Real-source replay: case 3 changed from `unknown` to `head_tilt` with the partial
flag; the other four full-scene labels remained unknown. This recovers one
measured facet, not complete classification of these scenes. Cold/warm/changed
generation/cancellation and immutable-pose assertions passed again. Report:
`research/preserved-evidence-20261014/partial-head-real-source.json`.

Validation: 164 core tests passed, full UI suite 48 passed/5 skipped, then focused
UI tests additionally verified the visible partial-scope caption with 417 rows
and preservation of range/manual classification behavior (4 passed). Current
development application rebuilt; no public release.

## Body identity input validity

The local body model still matches the previously used compact, quantized
OSNet x0.25 asset (891,054 bytes, SHA-256
`75146f8fa0893808981a8d2d52586789e59521f2b06e3d7415439e7e718dd2ea`).
The larger x1.0 and interpolation experiments already documented in
`edit-matcher-audit-2026-10-13.md` did not recover the back-view example; they
were not rerun or substituted for the current model.

Inspection found a separate input-contract bug: `ReIdEstimator::makeInput`
clamped empty, reversed or wholly out-of-frame person boxes into at least a
one-pixel crop. Nonfinite box coordinates could also reach integer sampling.
Such a crop is not valid person identity evidence. These inputs now throw
`invalid_argument`; finite, positive-area crops intersecting the frame preserve
the existing preparation and clipping behavior. This is rejection of invalid
input, not a change in identity thresholds, model or valid descriptors.

Regression tests cover empty/reversed/offscreen boxes, NaN/Inf coordinates and
equivalence of legitimate partly clipped crops on a nonuniform image. The
identity/matcher regression passed all 153 tests across ReIdEstimator,
DominantPerson, FaceConsensus and MotionMatcher. Application rebuilt; old
analysis cache preserved. Cross-view identity/action recall remains unfinished.
Report: `research/preserved-evidence-20261014/reid-crop-identity-regression.xml`.

## Temporal alignment cannot bridge missing seconds

Two new regressions reproduced a production matcher defect before the fix.
Eight observations split into two short runs around a three-second gap still
received similarity .994, and a longer case's result spanned the missing
interval (5.3 seconds). The alignment limited steps by vector indices, but did
not check the elapsed source time between selected observations.

Alignment now checks elapsed time on both source axes before extending a run:
each step must be finite, positive and at most .65 seconds. This preserves
isolated skipped samples at the usual 4+ FPS observation rates, while a longer
gap starts a separate run. A sufficiently supported continuous section survives
and supplies its own result boundaries; two insufficient sections cannot add
their observations together. This applies to motion alignment, not the separate
static-pose path. Existing identity thresholds and immutable cached poses are
unchanged.

Both new tests failed on the previous implementation and passed after the fix.
The matcher/grouping/scene partition/cache regression passed all 160 tests,
including isolated-dropout and sparse-sampling cases. Application rebuilt.
Report: `research/preserved-evidence-20261014/temporal-gap-regression.xml`.
This establishes the time-gap contract on these cases, not correct boundaries
for every real clip or complete cross-view action recall.

## Hidden-face limb candidate support

Inspection of the current view-normalization path showed that torso scale
alternatives are already enabled. A separate earlier discovery filter still
required an observed nose even when shoulders, hips and limbs were measured.
`observedPoseRuns` now permits a complete shoulder/hip anchor in place of the
nose. This applies to short held-pose candidates used by both AnalysisController
and repeat recovery; it does not certify actor identity or semantic action.

Arm chains are checked independently: one sustained measured shoulder/elbow/
wrist chain remains usable when the opposite arm is occluded. Changes in the
observed chain set split the support run. The initial independent-arm version
failed two recorded aiming-pose regressions by extending into an occluded
endpoint/bridging a missing wrist; splitting those observation changes fixed
the failures without weakening the recorded expectations.

New tests cover hidden facial landmarks, visible torso/limbs, one visible arm,
missing torso/limbs and alternating noncontinuous arm observations. Generated
candidates contain no invented body/face embeddings, and requireAppearance
still rejects an unknown person with otherwise matching geometry. The new
hidden-face support regression failed before the fix. All 155 tests across
PoseSupport, MotionMatcher, DominantPerson and FaceConsensus now pass. Current
development application rebuilt; original pose caches unchanged.

Report: `research/preserved-evidence-20261014/hidden-face-candidates-regression.xml`.
This removes a candidate-stage loss, not the unresolved source-identity rejection
of the user's back-view hood example, and does not prove complete cross-view
matcher recall.

## Direct faces without an averaged shot prototype

The opt-in direct-face verification previously ran only when both averaged
shot-face embeddings passed the old evidence gate. Independent, model-tagged
source-frame observations could therefore neither confirm a person nor veto
similar clothing when those averages were absent. Direct verification now runs
before that prototype condition in the opt-in mode. Portrait preparation also
permits such observation candidates; exact identity, geometry and temporal
checks still decide admission.

Supported verification reports the mean of each supported sample's median
cross-reference cosine, derived from the actual direct observations. It does
not report an older prototype's unrelated cosine or a made-up certainty of 1.
This score is descriptor agreement, not identity probability. The normal mode
and old cache remain unchanged; this does not enable the experimental pipeline
by default.

New regression covers comparison and complete pair discovery without averaged
face prototypes, old prototype disagreement, different model tags, out-of-range
observations and a contradictory face despite identical clothing. All 158
identity/matcher/cache tests passed. A read-only replay of the six existing
reviewed Soldier/Elliot comparisons preserved three supported Soldier pairs
and three contradicted cross-actor pairs. Supported raw scores were approximately
.586, .439 and .515. These are six identity checks, not six found parallels or
a held-out professional-accuracy benchmark. Application rebuilt.

Reports: `research/preserved-evidence-20261014/direct-face-without-prototype-regression.xml`
and `research/preserved-evidence-20261014/direct-face-score-real-replay.json`.

## Refusing malformed face observations

`FaceEstimator::inferImage` previously clamped person-box coordinates before
checking finiteness. A NaN dimension could bypass the minimum-crop-size check
and reach model preparation; a NaN confidence threshold could likewise bypass
ordinary confidence comparisons. Invalid box order, empty/offscreen crops,
nonfinite coordinates/confidence/nose hints and confidence outside [0,1] now
return no face evidence before model loading. Diagnostics are reset first, so
an unsuccessful observation cannot retain the preceding accepted face.

Three input-contract tests verify these rejections without requiring a model,
check diagnostics, and prove that valid inputs still reach the detector. All
159 tests across FaceInput, ReIdEstimator, FaceConsensus, MotionMatcher and
DominantPerson passed. Valid crop preparation, trained models, identity
thresholds and cached analysis measurements are unchanged; application rebuilt.
Report: `research/preserved-evidence-20261014/face-input-identity-regression.xml`.
This fixes malformed-input handling, not semantic classification or cross-view
identity recall.

## Explicit confirmation of an automatic class

Choosing the already displayed automatic class now sets `classificationManual`
and records an undo operation. Previously the same-value early exit left the
class automatic, allowing a subsequent range edit to replace the user's choice.
The application and UI tests were rebuilt; the normal UI suite passed 48 tests
with five optional tests skipped. The added assertion in the optional real-video
review test has not yet been executed, so this is not a claim of completed real
review/export validation.
