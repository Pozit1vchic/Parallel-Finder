# Scene families: retained evidence and inspection

The organizer retains a shared, immutable evidence record for each family. Result
pairs refer to the same record rather than copying observations or decoding video
again. `rawOccurrences` counts all pair-side appearances; `members` contains unique
interval/scope variants. Repeated references to one interval do not inflate the
number shown to the user.

`compareSceneFamilyOccurrences` returns a stable reason and the measurements that
were actually used: temporal overlap, normalized pose distance and context cosine
similarity. Missing measurements remain -1 internally and null in JSON. The
representative's self-record does not claim a measured zero pose distance.
Complete-link admission retains comparisons already calculated; replacing the
representative does not repeat the copied-footage callback or inference.

The production controller first verifies identity, ranks candidate motion results,
then independently reviews head orientation and video actions. Only afterwards
does it organize families and calculate evidence quality. Result evidence and
family presentation maps are built from these enriched windows. The early rank
step does not organize families; moving organization before these reviews would
lose measured head evidence and specific posture information.

The representative quality combines scoped observation evidence (55%), identity
confidence (20%), temporal coverage (15%), match similarity (10%) and a measured
camera-influence penalty when available. It is an evidence score, not an accuracy
probability. Body depth orientation and camera influence remain unknown when
unmeasured. Full trajectory compatibility and broader real duplicate controls
remain open in the requirements audit.

## Export and manual edits

`organizedClip` carries the representative's own scene boundaries. A representative
from an earlier copied shot must not inherit the original comparison's later shot
boundary. The helper checks source, family ID and range validity, and manual edits
use their exact requested range. Composing a new comparison clears obsolete family
references. Disabled sides do not introduce metadata into exports.

JSON remains version 2 with additive `sceneFamilySchema: 1` and a top-level
`sceneFamilies` dictionary. Each family is emitted once per export. Ranges preserve
17-digit precision, sufficient to reconstruct canonical microsecond fingerprints;
`rangeConvention: matcher_evidence` distinguishes these observed endpoints from
the frame-adjusted exported clip endpoints. Member evidence contains no image,
face embedding or pose arrays.

## User interface

The result's variants action opens a centered inspector using existing theme and
motion tokens. It shows unique variants, the selected representative, admission
reason and evidence quality. Side selection is limited to enabled source sides.
Each member can be played within its own scene boundary using the existing muted
comparison player. Closing, changing sides or changing records unloads playback;
inspecting does not alter the selected result. Manual range adjustment hides the
original-family action until those bounds are reset.

The list is virtualized, the popup is bounded to the current window, and geometry
changes respect reduced motion. Strings are included in all six catalogs.

## Verification and limits

- `family-evidence-release-focused.log`: 22 core, exporter and physical export
  checks passed. Tests cover observed reasons, missing evidence, representative
  replacement without repeated comparisons, cancellation, shared unique members,
  canonical shot boundaries and JSON fingerprint reconstruction.
- `ExportQueue.OrganizedFamiliesProduceThreePhysicalClipsForFourSideRequests`
  passes production organization through the real lossless cutter and decoder,
  using tiny media copies with Cyrillic/Japanese paths. Four side requests produce
  three physical outputs with the expected decoded frame counts. Seeded poses
  make this an integration/transport check, not a neural quality benchmark.
- `sceneFamilyInspectionKeepsResultsAndCleansUpPlayback` checks actual fixture
  playback, side switching, unchanged selection, small-window bounds, close and
  reduced motion. The reviewed screenshot is
  `build/ucrt64-release/tests/scene-family-inspector.png`; it uses fixture media.
- Frozen Butcher replay uses 2,037 windows and the same 25-pass harness on baseline
  `faa09d2` and this implementation. All 125 result pairs, scores and canonical IDs
  agree. Organization median is 3.785 ms before and 3.856 ms after (0.071 ms extra
  metadata work); this is not a speed improvement. Reports and exact settings are
  retained under `build/family-evidence-*`.

- `family-evidence-verified-suite.log`: all 13 CTest targets pass in 123.81 s.
- `family-evidence-real-ui.txt`: all three lifecycle checks pass on the supplied
  Kyle video, including one confirmed 37-track lead and cached reselection with
  zero pose inference. This real UI run is separate from the fixture family
  playback test.

These checks do not close the real duplicate-positive quality dataset, broad
retrieval metrics, body orientation, series/incremental E2E or packaging gates.
