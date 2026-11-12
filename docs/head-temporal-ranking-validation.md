# Temporal head evidence: validation and limits

This work investigates ordering already returned head-motion pairs. It does
not establish full Matcher precision, recall, or action classification accuracy.
The judgments are assistant reviews of source images and pose overlays, not
independent human confirmation. The held-out set is separate from training,
but consists of returned predictions rather than exhaustive ground truth.

## Frozen protocol and observations

Training measurements were specified in
`tools/benchmark/head-motion-training-audit-plan.json` before reading cached
rotations. The primary candidate for held-out verification was then frozen in
`tools/benchmark/chernobyl-head-heldout-review-plan.json`: descending minimum
of the two sides' maximum geodesic angular separation within uninterrupted
valid observation runs. Unknown measurements retain their positions; equal
measurements retain their previous order. No degree threshold or fitted
similarity weight is introduced.

All eight held-out head predictions were reviewed before their rotations were
read. Their frozen judgments are in
`tools/benchmark/chernobyl-head-heldout-review-labels.json`: five strong and
three weak editing parallels. Opposite image directions can be relevant editing
parallels in these judgments; they do not prove equal physical trajectories.

| Set | Strong-versus-weak preferences | Existing order correct | Angular extent correct |
|---|---:|---:|---:|
| Training: nine head predictions | 18 | 10 | 18 |
| Held out: eight head predictions | 15 | 8 | 15 |

These preference counts reuse the same small number of predictions and are
not independent trials or a calibrated accuracy estimate. Extent can include
estimator noise, body motion, or camera motion. It does not establish direction,
axis, temporal alignment, or semantic agreement. Body-action retrieval remains
unresolved: previously defined independent positive controls are still missed.

## Cache replay and compatibility

`pf_head_cache_replay` reads exact production keys including source, pose,
timestamp, model identity, provider, and source-generation namespace. It checks
source/model content hashes and reads cache entries directly without LRU updates,
inference, decoding, or cache writes. Its output must be a new file outside the
managed cache roots. The extracted shared head codec preserves the existing v1
format; transport decoding is not a new SO(3) integrity check.

Training replay read 166 cached requests, including 158 observed and eight
explicitly unobserved samples. Held-out replay read 132 cached requests, including
129 observed and three explicitly unobserved samples. Counts precede interval
clipping. Missing entries remain missing and never become frontal head poses.
The training read-only manifest comparison covered 8,823 files, checking paths,
sizes, modification times, and hashes. Missing-cache, wrong-source-hash, and
protected-output controls also passed.

Native application compatibility retained all 188 pairs and their fields except
session preview URLs. Head review reused 3,936/3,936 requests and action review
95/95, with zero decoded frames or review-model initializations. This was one
functional compatibility run, not a new performance median. The full build
passed 16/16 CTest suites in 141.79 seconds before the excursion API addition.

Local evidence is under `build/goal-head-motion-training-audit-01`,
`build/goal-head-heldout-review-01`, and
`build/goal-head-cache-codec-native-01`; these generated artifacts and media
are excluded from Git.

## Production integration status

The core `headRotationExcursion` measurement uses the existing proper-rotation
tolerance and half-open interval boundary policy. It rejects malformed sample
ordering, breaks runs at missing or invalid observations, and returns unknown
when no valid pair exists. Two identical valid observations measure zero.
Its value is an angular measurement, not a classification confidence.

At this stage the measurement is not connected to result ordering. No claim is
made that the native interface has adopted the candidate ranking or that raw
Matcher similarity scores have improved. Integration must preserve body/static
positions, record membership, and measured similarity fields, and verify range
edits and unavailable evidence before being reported as complete.
