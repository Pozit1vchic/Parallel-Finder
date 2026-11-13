# Returned pairs across versions

This audit checks the report that fewer parallels are found. It uses existing
full-episode native reports for Chernobyl, not the still-running Katniss
analysis. Both sides of each comparison have the same input video list and
source frame count. The historical executable was built with a diagnostic
all-people adapter; the comparison is not an unmodified installer A/B test.

| Episodes | Historical total | Current total | Historical strong tier | Current strong tier |
|---|---:|---:|---:|---:|
| S01E01/E02 | 147 | 188 | 50 | 57 |
| S01E03/E04 | 117 | 153 | 42 | 55 |

The strong tier is the default view; possible matches are shown separately.
These are result counts, not measures of relevance, precision, or recall.
There are also historical pairs that no longer appear: 49 of 147 and 38 of
117, respectively, have no current temporal counterpart under a permissive
check. That check ignores pair direction, requires the same source on each
side, and requires at least half of the shorter interval to overlap on each
side. It does not establish the same person or action. Exact source/range and
scope retained counts are 98 and 78; track IDs are deliberately not assumed
stable across different observation runs.

For each split the five historical strong-tier rows without a temporal
counterpart with the highest measured similarity were fixed before visual
inspection. The old cached poses were replayed read-only, binding both tracks
to each row's original sources and time bounds. All ten pairs had pose samples
on both sides. Initial track-overlay observations:

- E01/E02 historical index 25: the selected person in blue is standing with
  arms down for much of both intervals, though the foreground gesture and
  camera framing differ. A plausible static editing parallel, not evidence
  of a matched arm action.
- E03/E04 historical index 9: both selected tracks follow the background
  person in the red/green jacket. Much of the pose is similar, but occlusion
  differs and the obvious foreground flashlight action is not the matched
  track. Another plausible static pair to trace through retrieval.
- E03/E04 historical index 4: the upper selected track is the dark background
  figure, not the person walking through the doorway; the lower track is
  partially occluded. The visible evidence is insufficient for a strong
  semantic judgment.

The other seven overlays are prepared but not graded here. These are
assistant observations, not independent annotations. The old-only plausible
pairs warrant tracing candidate generation and admission; restoring all old
rows or loosening thresholds from this sample would not be justified.

## Where the two plausible pairs disappear

All four historical observation entries have the same SHA-256 as their current
counterparts. This rules out differences in the stored observations for these
comparisons; it does not prove that later window preparation is identical.
The read-only localization check verified the cache container and payload
hashes, then bound each raw Matcher payload to its final report by complete
source/range membership (188/188 training rows and 153/153 held-out rows).
Neither plausible pair has a temporal counterpart in the corresponding raw
Matcher output. A results-list filter or ordering change cannot explain these
two absences.

In both cases the exact right-hand interval appears with a different partner:
training old index 25 uses right interval 1123.689–1125.170 (historical score
0.9499, new counterpart 0.9940); held-out old index 9 uses right interval
2450.538–2452.178 (historical score 0.9799, new counterpart 0.8569).
The scores are for different partners and do not establish preference. Both
observations make competition for a scene a plausible explanation, but the
raw cache is written after Matcher selection, so it does not distinguish
candidate retrieval, exact comparison, and internal selection. Do not change
the one-pair-per-shot policy based only on this evidence.

Local localization evidence: `build/goal-031-missing-pair-raw-localization.json`.

### Exact comparison of historical training index 25

A native replay captured the current desktop policy and 35,934 prepared
windows while reusing all 188 raw results. A streaming read selected the
historical tracks and overlapping static windows (four on the left, seven on
the right). The standalone comparison probe tested all 28 combinations using
the captured desktop parameters. Native window indices 1041 and 5830 reproduced
both old intervals and the exact historical similarity, 0.949890073851649,
without a comparison rejection. This shows the pair remains admissible in
direct comparison; it does not prove that full candidate retrieval proposes
those indices or that selection keeps them.

Evidence is under `build/goal-031-missing-pair-desktop-replay-01`: the captured
parameter JSON and `case25-{windows,pairs,comparisons}.json`. A separate native
run uses a copy of the primary cache without raw Matcher entries and enables
the existing selection trace. That run is functional diagnostics only; it
overlaps the Katniss character capture and is not a timing measurement.

The full trace subsequently completed and reproduced all 188 final rows
exactly except session preview URLs. Historical index 25 reaches the internal
selection loop at 250.860/1123.689 with score 0.949890, so retrieval and exact
comparison are not the reason this pair disappears. The earlier
974.678/1123.689 candidate (score 0.994000) survives and uses the same right
shot. This is consistent with the single-use shot limit; the trace does not
log individual skip reasons, so it cannot exclude additional internal vetoes.
Evidence: `build/goal-031-selection-trace-01/warm-01.json.stderr.log`.

On a four-window replay containing the two competing edges, the standard
selector and general selector with a one-use quota returned only 0.994000.
The general selector with a two-use quota returned both edges. Switching to
that general selector in a full native run was not a satisfactory fix: it
returned 157 rows, retained only 27 of the previous 188 exact track/range
identities, and did not recover the target pair. That switch changes other
selection rules as well as the quota.

Instead, the existing Expanded search first completes its ordinary selection
and then admits an additional static body pair only if it connects an unused
shot to a primary shot that has not received another alternative. It keeps
the existing similarity, camera and identity gates and pose budget. The
ordinary search is unchanged. The new expanded policy returned both real
edges in the four-window replay and passed 136 focused tests.

On the full 35,934-window replay, Expanded search returned 269 rows with
the additional pass and 187 without it. All 187 primary rows were preserved
exactly. All 82 additions were body comparisons and no exact source interval
appeared more than twice. The probe flagged 27 additions as identity-verified
and 23 as face-verified. Those are algorithm outputs, so the 82 additions
measure additional candidates; independent precision is still unmeasured.
The historical 250.860/1123.689 edge remains absent. The replay instead
returned 250.860/1214.697 (0.9724), while retaining 974.678/1123.689
(0.9940). The four-window result therefore does not generalize to that
specific edge in the full candidate pool.

Local replay evidence: `build/goal-031-expanded-full-replay-02/comparison.json`.

A first visual review used three frames per interval and the matched track's
keypoints on its midpoint observation. The overlay uses the 1280×720
inference size limit and displays only keypoints with confidence at least
0.4. Examples among the first 12 additions:

| Time ranges starting at (seconds) | Visual observation |
| --- | --- |
| 1385.870 / 1630.010 | The same man stands upright with his arms lowered in both shots. |
| 650.012 / 1048.523 | The same woman is upright with a similar arm position at two different shot scales. |
| 1123.369 / 1509.840 | The same man holds bent elbows with his hands close together in both shots. |
| 1418.193 / 2182.014 | The man bent forward with hands on his knees is paired with a more upright figure. |

These are illustrative observations from one replay, not a labeled precision
sample. The last row shows why score and additional result count alone are
insufficient to judge the new pass. Local annotated frames are in
`build/goal-031-expanded-full-replay-02/pose-review-01.jpg`.

Exact comparison of the weak 1418.193/2182.014 pair reproduces the
0.9404 score. Its direct DTW and whole-body shape scores are only 0.4984
and 0.1935. The identity-verified articulation branch overrides both
with 0.9404. The midpoint shoulder-to-hip axis is nearly vertical in
both projections, so an image-plane torso-angle gate would miss the
visible forward bend. Across frames with confident shoulders, hips
and knees, median projected shoulder-to-hip length divided by
hip-to-knee length was 0.870 versus 1.343 for this pair, compared with
1.463 versus 1.325 and 1.421 versus 1.381 for two plausible full-body
pairs. Another plausible pair had no shared confident knee support.
Projected ratios are also affected by camera angle. These four checks
identify the scoring override but do not establish a safe global
cutoff. Local exact replay: `visual-controls.json` and
`visual-controls.stderr.log` in the same evidence directory.

For expanded shot reuse only, the selector now also requires DTW alignment
of at least 0.55 on the existing 0–1 scale. The full 35,934-window replay
kept 269 results and all 187 primary rows byte-equivalent as parsed data.
Seven expanded rows were replaced, including the weak 1418.193/2182.014
pair. All three visually plausible controls in the table stayed present.
Midpoint keypoint overlays for all seven removals and replacements show
mixed results. At 302.184/394.671, the replacement shows similar lowered
arms and upright shoulders under different lighting. At
1418.033/1681.534, the woman braced with both hands on her knees is now
paired with a woman leaning over a seated person whose forearms are
partly obscured. This remains ambiguous as an editing parallel. At
951.036/1217.337, the match aligns the upper body but one shot obscures
the lower body, so standing versus sitting cannot be resolved from the
overlay. These are observations from the development replay, not
independent validation of the cutoff. Local overlays are
`pose-review-01.jpg` (removed) and `pose-review-02.jpg` (replacements)
in the alignment replay directory.
All 136 focused Matcher and result-cache tests passed.

Local evidence: `build/goal-031-expanded-alignment-replay-01/comparison.json`.

Local evidence: `build/goal-031-existing-pair-count-comparison.json`,
`build/goal-031-missing-pairs-{training,heldout}-review`, and
`build/goal-031-missing-pair-tracks-01`. Current reports are the same native
rows used by the head ranking integration checks. None of the source frames
or generated artifacts is included in Git.
