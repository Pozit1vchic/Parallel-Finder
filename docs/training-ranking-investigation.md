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

Initial inspection resolves a significant ambiguity in training rank 0: its
left selected person is inside an in-car video display, while the right selected
person is shown full-frame in the same decorated-car setup. The detector is
observing a person on that screen; calling it a non-person detector artifact
would be incorrect. This raises a nested-footage/returning-view redundancy
question that whole-frame copied-footage matching alone may not settle. It
requires temporal inspection and an explicit judgment before any fix.

Training indices 1–3 select the overlaid standing subjects rather than nearby
foreground gestures. Their apparent generic-posture correspondence likewise
needs the full temporal review. No training Precision estimate is claimed from
these deliberately selected examples.

Extraction and the small diagnostic-only test build finished during the already
excluded functional uncached-01 run. The application executable was unchanged;
the three separately designated timing runs remain reserved for no heavy
diagnostic work.
