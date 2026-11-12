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

Local evidence: `build/goal-031-existing-pair-count-comparison.json`,
`build/goal-031-missing-pairs-{training,heldout}-review`, and
`build/goal-031-missing-pair-tracks-01`. Current reports are the same native
rows used by the head ranking integration checks. None of the source frames
or generated artifacts is included in Git.
