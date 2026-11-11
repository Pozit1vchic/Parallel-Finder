# Returned top-10 montage relevance audit

The completed temporal and matched-track review finds no confirmed strong
montage parallel in the first ten returned results of either held-out version
under the previously frozen review rubric. This is a limitation of these small
returned lists, not an estimate of episode-wide Precision or proof that every
returned result is false.

| Version | Strong, grade 2 | Weak, grade 1 | Clear mismatch, grade 0 | Unresolved | Strict P@10 bounds |
| --- | ---: | ---: | ---: | ---: | --- |
| Before | 0 | 10 | 0 | 0 | 0–0 |
| After | 0 | 9 | 0 | 1 | 0–0.1 |

The bounds account for unresolved labels; they are not statistical confidence
intervals. Lenient bounds that count weak similarity are 1–1 and 0.9–1,
respectively. That permissive number must not be presented as strong montage
precision: grade 1 includes ordinary standing, isolated gaze similarities and
the camera returning to substantially unchanged poses.

## Method and evidence

The unmodified `chernobyl-prediction-review-plan.json` was frozen before the
selected prediction sheets were rendered. Selection is returned array positions
0–9 for each version, without choosing attractive examples. All twenty results
have `matchType=pose` and `headOnlyComparison=false`. Review used six source-PTS
temporal samples per side and native keypoint overlays on first/middle/last
cached pose samples inside each returned interval. All four actual observation
artifacts from the old/new E03/E04 analyses were used to resolve source, track
and pose-window associations; no new inference was performed by the pose probe.

The assessor is the assistant, without independent human confirmation. The
predictions and version identities are not fully blinded. Scores and classifier
labels are hidden on temporal sheets. This post-hoc audit is separate from the
previously frozen positive-control Recall experiment and cannot turn predicted
examples into independently selected ground truth. No algorithm or threshold
was tuned from these held-out judgments.

Visual files stay outside Git:

- `build/goal-heldout-top10-prediction-review/{before,after}/pair-*.jpg` and each
  variant's rendering provenance.
- `build/goal-heldout-top10-pose-review-01/{before,after}/pair-*/overlay/review-001.jpg`,
  the native query/pose reports, overlay inputs and extraction logs.
- `build/goal-heldout-top10-pose-review-01/metrics.json`, with report, plan and
  judgment hashes. The scoring tool also checks source/track/time/scope identity
  and the reviewed visual artifact hashes.

The committed judgments contain an explanation for every returned index. Eight
new-version pose inputs are byte-identical to reviewed old-version inputs;
those judgments reuse the same actual pose evidence, with explicit old-index
links. Other new-version examples were reviewed separately.

## What the overlays resolved

- Index 1 selects the rear subject holding a long object across the torso;
  the foreground flashlight holder is not the selected track. The repeated
  configuration is weak under the frozen returning-camera rule.
- Old index 2/new index 3 selects the background red-and-green-jacket subject,
  so the foreground person's gestures do not justify the pair.
- Old/new index 8 selects the background brown-and-tan-jacket subject, rather
  than the foreground paper exchange or person in a bright jacket. Again, the
  held shoulder strap recurs when the camera returns.
- New index 9 identifies the selected person carrying a rectangular object
  beside the car and a person near the rooftop wall. The object obscures arm
  geometry; the available temporal and pose samples do not settle whether the
  held posture is comparable. It remains unknown.

The observed quality problem is the prominence of generic/static or returning-
camera matches at the top of the list. Correct track association alone does not
make these specific montage parallels. Improving this behavior requires work on
the training split or a newly reserved dataset, followed by a frozen independent
test, rather than relaxing the definition of a positive on these examples.

## Concurrent benchmark accounting

The native pose probes, visual extraction and telemetry diagnostic checks
overlap `goal-chernobyl-cache-disabled-01/uncached-01`. Before that run completed,
it was explicitly excluded from speed comparisons in
`build/goal-uncached-timing-policy.json`; it remains a functional uncached check.
The three designated timing runs are uncached-02, uncached-03 and the queued
uncached-04 in `goal-chernobyl-cache-disabled-02`. Those measurements are pending.
The application executable and models remain unchanged across these runs.
