# Training-side ranking investigation

This is an investigation, not a validated replacement ranking policy. Selection
is frozen in `tools/benchmark/chernobyl-training-ranking-plan.json`: the complete
first ten S01E01/E02 results plus the previously reviewed training indices 37 and
64. The held-out E03/E04 judgments are not used for threshold fitting.

Source inspection establishes the following current behavior:

- `MotionRanker::rank` computes classifier/duration-adjusted `rankScore` for
  motion and leaves static pairs at raw similarity.
- `MotionRanker::organize` subsequently calculates
  `bestMatchScore = similarity * (0.55 + 0.45 * evidenceQuality)` times an
  identity-verification factor. The final diversity order uses this score;
  `rankScore` does not feed it.
- `comparisonEvidenceQuality` takes the minimum of the two observation-quality
  values. It measures available evidence, not how distinctive an action is.
- `poseSimilarity` is computed from the two descriptors and retained as metadata
  but does not enter `bestMatchScore`.
- Family grouping uses local pose/time evidence and verified whole-frame copied
  footage. Repeated camera angles with unchanged actors need not be identical
  footage and are not automatically the same event.

These facts support hypotheses about generic-pose prominence, not a conclusion
that changing one factor improves independent Precision or Recall. No ranking
weights or thresholds have been changed for these hypotheses.

The twelve temporal sheets and native matched-track overlays are complete in
`build/goal-training-ranking-review-01`, using the original cold training report
and both source-specific observation artifacts. Each overlay uses three native
pose samples per side. All extracted media remains outside Git.

The completed temporal and overlay review resolves a significant ambiguity in training rank 0: its
left selected person is inside an in-car video display, while the right selected
person is shown full-frame in the same decorated-car setup. The detector is
observing a person on that screen; calling it a non-person detector artifact
would be incorrect. This raises a nested-footage/returning-view redundancy
question that whole-frame copied-footage matching alone may not settle. The
temporal sheet confirms a substantially unchanged stance in that setup; the
judgment is weak/redundant, not a non-person artifact or a pixel-verified copy.

All twelve judgments are saved in
`tools/benchmark/chernobyl-training-ranking-labels.json`, with exact report and
visual hashes, source/track/interval identity and a note for each result. The
selection contains one strong head-turn anchor (37), nine weak examples and two
unresolved examples (6 and 8). The latter have substantial body occlusion; a
visible selected head and predicted hidden joints do not prove a comparable
full-body pose. The repeated hand-behind-head example (64) retains its previous
weak/partial returning-camera judgment. No training Precision estimate is
claimed from this deliberately selected sample.

## Existing-field preference audit

The one strong anchor should precede the nine judged weak examples, giving nine
selected training preferences. Unknown examples abstain. An offline audit of
existing fields, without fitting any parameters, gives:

| Ordering field | Strictly correct | Comparable preferences | Unavailable |
| --- | ---: | ---: | ---: |
| Current returned order | 1 | 9 | 0 |
| Raw similarity | 0 | 9 | 0 |
| Initial rankScore | 0 | 9 | 0 |
| Final bestMatchScore | 1 | 9 | 0 |
| Observation evidenceQuality | 6 | 9 | 0 |
| Measured poseSimilarity | 1 | 8 | 1 |

The last field retains its -1 unknown sentinel, which abstains instead of being
treated as a measured zero similarity. Evidence:
`build/goal-training-ranking-review-01/ranking-field-audit-verified.json`.
`evaluate_training_ranking.py` validates the frozen selection and exact report,
track and visual provenance. Ordering/tie/unknown/missing/non-finite score
controls were executed before using its output.

This small audit does not justify replacing the ranker with evidence quality
or another individual field: it has only one selected strong-positive anchor.
It does establish that merely substituting the existing measured-pose score
does not solve the training preference problem. A relevance/specificity policy
needs broader training support and a newly frozen independent evaluation.

Extraction and the small diagnostic-only test build finished during the already
excluded functional uncached-01 run. The application executable was unchanged;
the three separately designated timing runs remain reserved for no heavy
diagnostic work.
