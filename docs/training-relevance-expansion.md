# Expanded training relevance review

## Frozen selection and evidence

Commit `0634bfe` froze `chernobyl-training-expansion-plan.json` before reviewing
new images or changing ranking. The source is the original 188-record cold
S01E01/E02 report, SHA-256
`7249935b33340c765c3cc35eca1217d6bb90ea9ec63eeb6e51e7bc39e7b60a5d`.
All twelve previously reviewed indices are excluded. The expansion includes
all eight unreviewed head-only predictions, two named posture predictions,
six deterministically sampled non-head motion predictions, and eight sampled
generic body-pose predictions. Sampling and every selected index are explicit
in the plan; this is not a representative prevalence sample.

All 24 temporal sheets and 24 native selected-track overlay sheets were
inspected. Temporal sheets show up to six actual-PTS frames per side; overlays
show three native pose samples per side. Some short intervals yield five
temporal frames. Native observation probes read the isolated PFCH artifacts,
not new neural inference. Extraction/probe/render arguments and provenance are
under `build/goal-training-expansion-review-01`; media is excluded from Git.

`chernobyl-training-expansion-labels.json` records the exact report, frozen-plan
hash, source/track/ranges/scope, notes and hashes of the reviewed visual files.
The plan hash uses LF bytes verified identical to commit `0634bfe`, rather than
Windows-converted CRLF working-tree bytes. Both hashes and that normalization
are recorded; selection and rubric did not change. Git attributes preserve LF
for the plan and labels on future checkouts.
There is no independent human confirmation. The review concerns the matched
track, including partially hidden background actors, rather than whichever
foreground actor has the most conspicuous movement.

## Judgments and conservative revision

The final expanded set has **2 strong, 16 weak and 6 unresolved** judgments.
There are no examples assigned grade 0, but this is not a claim that all weak
or unresolved predictions are true positives. Grades 1 and unknown remain
separate from a clearly supported useful parallel.

- Indices **41 and 44** support visible, comparable head rotations across
  different settings. The temporal extents/pitch are not necessarily identical.
- Index **45** was initially grade 2. A denser, up-to-twelve-frame-per-side
  review showed small head adjustments without a clear shared distinctive
  trajectory. It was revised to grade 1 before any fit or ranker change. The
  prior draft and its SHA-256, explanation and additional image hash are retained
  in the annotation revision. Initial audit outputs are not overwritten.
- All six sampled non-head motion pairs are unresolved because selected bodies
  are cropped or hidden. Several prominent foreground actions belong to other
  tracks. In index 187, for example, the matched right track is a small person
  behind the large foreground man's shoulder, not that man's visible head dip.
- The sampled body-pose pairs are weak generic/partial resemblances. Index 33
  again demonstrates a full-frame actor versus a person on a video display;
  nested returning footage is not automatically pixel-identical whole footage.

Together with the earlier twelve reviewed predictions, there are 36 unique
training annotations: **3 strong, 25 weak and 8 unknown**. All three strong
examples are head comparisons. There are still no labeled strong body-action
or distinctive full-body-pose examples in this selected output sample. This
imbalance prevents treating a head-only boost as general relevance calibration.

## Preference audit and the scope confound

The expanded sample supplies 32 strong-over-weak preferences. Unknown examples
abstain; unavailable `poseSimilarity=-1` abstains rather than becoming measured
zero. Existing fields, with no fitted weights, produce:

The table was checked both in the original report and in the latest committed
implementation's native `goal-lazy-result-previews-native-02/warm-03.json`.
Exact oriented source/track/start/end/type/scope keys transfer the frozen
judgments without altering their grades or visual evidence. Fifteen selected
positions changed, but the preference counts below are unchanged. Missing or
ambiguous comparison identities cause an error instead of silently shrinking
the evaluated sample. Source-content integrity is verified separately; identity
transfer alone does not prove the videos remained unchanged.

| Field | Strictly correct / comparable | Unavailable |
| --- | ---: | ---: |
| Returned order | 20 / 32 | 0 |
| Raw similarity | 7 / 32 | 0 |
| Initial rankScore | 7 / 32 | 0 |
| Final bestMatchScore | 20 / 32 | 0 |
| Observation evidenceQuality | 24 / 32 | 0 |
| Measured poseSimilarity | 9 / 18 | 14 |

The apparent benefit of evidence quality vanishes when scope is held fixed:

Here scope is the reported `headOnlyComparison` flag, not a claim that body
visibility has been independently established.

| Field | Within head comparisons | Head versus body comparisons |
| --- | ---: | ---: |
| Returned order | 6 / 12 | 14 / 20 |
| Raw similarity | 7 / 12 | 0 / 20 |
| Observation evidenceQuality | 4 / 12 | 20 / 20 |
| Measured poseSimilarity | 6 / 12 | 3 / 6, 14 unavailable |

Thus replacing final relevance with observation completeness would improve the
cross-scope portion of this sample while making its within-head ordering worse.
The aggregates do not establish a better ranker. The evidence suggests a
cross-scope comparability problem alongside a separate within-head temporal
ranking problem; it does not prove a calibrated relevance score.

Current code inspection agrees with that distinction: organization uses raw
similarity and observation completeness; measured head/pose similarity is
metadata, and initial classifier `rankScore` is not the final organization
score. Missing hips prevent full-body descriptor normalization; predicted or
hidden joints must not be filled in to manufacture full-body evidence.

`evaluate_training_ranking.py` now reports the three disjoint preference groups
(within head, within body, cross scope) and explicit grade counts. Six
regression tests cover a misleading cross-scope-only gain, unknown abstention,
missing/non-finite/-1 measurements, ties, invalid scope, exact-identity transfer,
changed/missing comparisons and duplicate identities. All passed. The
existing provenance validator checked all final annotations and image hashes.
Final current-implementation evidence: `current-ranking-audit-canonical.json`; the
original-report scoped result is `ranking-audit-by-scope.json`. Earlier provisional audits remain
preserved and must not be cited as the final two-positive result.

## Consequence and remaining work

No production ranker weights, thresholds, categories or matching gates changed
in this review. No new speed, Precision, Recall or independent quality gain is
claimed. E03/E04 labels were not used for this selection or fitting.

The next temporal investigation should inspect the already measured 3D head
rotation sequences, including onset, amplitude, direction and observation gaps,
against these frozen head judgments. Body relevance requires additional
source-selected positive/negative action controls rather than assuming the
unresolved returned body pairs are negatives. Any candidate ranking change
still needs a separately frozen evaluation with both movement and meaningful
static-pose examples; the selected training preference counts cannot serve as
that evaluation.
