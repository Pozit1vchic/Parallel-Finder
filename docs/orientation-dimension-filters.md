# Independent orientation filters

The existing result filter menu now has independent body and head direction
controls. Both use the existing eight view labels, plus any view and unknown.
They select representative horizontal direction relative to the camera; they
do not assert that an entire moving interval holds one fixed orientation.
Specific filters require agreement in every surviving side of the pair. Unknown
selects a pair when at least one surviving side has no supported direction.
Removed sides do not participate. Combining both filters and exporting a
selection uses only rows visible under both predicates.

Body support is shared with the existing Explain Match readout in
`OrientationEvidence.js`. It requires measured evidence, a finite signed angle,
a consistent view label and at least three integer supporting samples. Head
support comes from its independent validated rotation descriptor, with its own
angle, view and observation strength. Missing head data does not fill body data,
and missing body data does not fill head data. Nulls, strings, invalid angles,
fractional sample counts and inconsistent cached labels remain unsupported.
Continuous angles remain unchanged; labels only choose a 45-degree sector.

The positive-yaw/image-left convention was checked against the author's
[6DRepNet360 rotation and axis utilities](https://github.com/thohemp/6DRepNet360/blob/master/sixdrepnet360/utils.py).
This is a source convention check, not an independent real-model accuracy
benchmark. In particular, taking the horizontal azimuth of the forward rotation
axis differs from using principal Euler yaw outside its limited range.

## Correctness fixes exposed by the controls

- A vertical head rotation previously produced optional yaw and roll values
  from zero projections, effectively presenting an invented frontal yaw.
  `SceneDescriptor` now retains the valid full rotation and pitch while leaving
  those singular Euler components unknown. Both upward and downward matrices
  fail the new check before the fix (`build/orientation-singularity-before.log`)
  and pass afterwards. Rotation-based head similarity remains available.
- The old posture filter still read a removed side. The new surviving-side
  regression expects two standing single-sided results, but gets zero before
  the fix (`build/orientation-removed-side-before.txt`). Posture and orientation
  filters now share the same surviving-descriptor traversal.
- Visual inspection of the actual popup exposed a stale selection display on
  language changes: sorting still used classification, but its translated combo
  displayed the first similarity option. `PfComboBox` now offers an optional
  controlled `selectionIndex` that is restored after model refresh. Result
  filter and sort controls use it. Other callers retain their current behavior
  unless they opt in. Six-language checks preserve selected body/head views
  and the actual sort index rather than merely checking nonempty translations.

## Validation and remaining scope

The Release build succeeds. Eleven descriptor tests pass, including the singular
head case (`build/orientation-dimension-core-tests.log`). Four focused UI
functions cover independent measurements, all eight directions and raw sector
boundaries, unsupported data, deletion, reset, export membership, six languages,
selection restoration, keyboard navigation and a 450-pixel-high window. The
popup is opened through its actual filter button and scrolled to both controls.
Its generated screenshot is inspected privately under
`build/ucrt64-release/tests/orientation-result-filters.png`.

These are deterministic descriptor/interface controls using controlled metadata
and rotation matrices. They do not establish recognition accuracy on the user's
videos. Dedicated orientation sort modes, head pitch/roll filtering, difficult
real-view recovery and final model packaging remain separate open requirements.
The primary observation cache contract is unchanged: presentation filtering
does not run model inference or require deleting cached analysis.

The final complete CTest run passes 14/14 targets in 116.36 seconds
(`build/orientation-dimension-full-suite.log`), including 58 default UI checks.
Seven explicitly opt-in real-media UI workflows remain skipped in this default
run. Six-language catalog validation passes. The inspected final screenshot
also confirms that the displayed classification sort agrees with its real
selected index after the language-refresh fix.
