# Duplicate character cards in Katniss

The published 0.3.0 reproduced the user's first two numbered cards on the
first original Katniss source. The isolated analysis diagnostic stopped at
the ordinary selection screen. It reused the completed first-source cache
from the four-source run; no personal settings or source files were changed.
The cache-root diagnostic retains the original configured path, while the
benchmark requests a separate root containing copies of the completed entries.

Card 1 (candidate 2) reports 404 tracks and 7,034 observations. Card 2
(candidate 45) reports 247 tracks and 3,797 observations. Both are numbered
confirmed candidates. Reconstructed crops at the representative and preview
timestamps show Katniss on both cards. This establishes the duplicate display;
it does not establish that every track in either group belongs to her.

Read-only replay of the source PFCH reports face-prototype cosine 0.97830 and
body-prototype cosine 0.98211. The prototype observation-time lists have no
intersection, but **39 timestamps** are shared by the stored windows of the
two groups. A prototype's missing overlap is therefore not evidence of
separate appearances. Four reviewed timestamp overlays illustrate why a
whole-group merge would be unsafe: at 3312.518 s both detections mark the same
head, at 195.028 s they mark two different visible people, and at 3057.846 s
one detection falls on a blurred foreground hand/object. A crowded frame at
1090.673 s is ambiguous. The strong aggregate resemblance coexists with
duplicate detections and wrongly assigned tracks. The next correction must
separate those cases rather than merge all 651 tracks on prototype similarity.

Local evidence: `build/goal-031-katniss-first-source-capture-01/candidates.json`,
`identity-pair-window-samples.json`, and the `review` subdirectory. The native
diagnostic is `pf_grouping_probe CHARACTER.pfc --character-pair REVIEW.json 2 45`.
No media or embedding vectors are published in this report.

The published build also reached the selection screen with all four original
sources (3,141 detected scenes). The capture stopped there by design and
saved 40 candidates. The first two numbered cards were again IDs 2 and 45,
with the same 404 and 247 tracks as in the first-source capture. This confirms
that adding the other films does not remove the duplicate numbered cards.
Local evidence: `build/goal-031-katniss-four-source-capture-02/candidates.json`.

## Confirmation rule correction

The existing distinction check pooled every separated observation across the
two candidate groups. Three isolated collisions from three different track
pairs could therefore confirm two people even when no pair repeatedly appeared
together. A regression test reproduces this with one anchor and three short
fragments that each share only one frame with it. It failed on the former
rule and passes when the existing three-frame/span requirement is applied to
the same two tracks. The control with two consistently separate people still
passes, as do the other 40 character and tracker checks in the focused run.

The character-artifact key is advanced because cached `distinctIdentity`
values were produced by the older rule. Existing files are not erased.
This change does not automatically merge ambiguous groups or discard their
tracks. A fresh native Katniss run is still needed to verify the numbered
cards and the actual tracks admitted by selecting the character.

Additional cache evidence (`identity-pair-evidence.json`) records similarities
of the sampled tracks to both prototypes. For example, the foreground object
at 3057.846 s has body similarity above 0.86 to both prototypes and no
observed face. Body resemblance alone is not a safe way to resolve this case.

## Native verification after the correction

A fresh CUDA run of the first original source with commit `28fd954`
again produced numbered cards 1 and 2 (candidate IDs 2 and 45). Both were
still confirmed, with exactly 404 and 247 tracks and 7,034 and 3,797
observations, respectively. The track-pair correction therefore fixes
its synthetic false-confirmation case but does not resolve the reported
duplicate. A further investigation must identify the actual repeatedly
separated tracks and compare their face evidence with each group before
changing selection or merging membership.

Local comparison: `build/goal-031-katniss-confirmation-after-02/card-comparison.json`.

The same run's character artifact again shows 39 shared window timestamps
and a face-prototype cosine of 0.97830. Four pairs of stored tracks share
at least three samples spanning 0.25 s or more. Frame review includes
Katniss with another person (1740.7 s and 4999.9 s), a close-up with the
previously observed duplicate head detections (3312.5 s), and a cut to
hands and legs where head localization is unreliable (3934.2 s). These
are candidates for checking the confirmation evidence. The artifact
contains prepared windows, so it does not record which raw observations
actually triggered the separation decision.

Local evidence: `repeated-track-pairs.json` and
`repeated-track-scenes.jpg` in the same directory.

The raw separation trace identifies one qualifying pair for candidate IDs
2 and 45: tracks 6275 and 6277 at three samples spanning 4999.703–5000.037 s.
The frames show Katniss entering from the right while Peeta stands at the
left. The other three overlaps in the prepared-window report did not
qualify in the raw trace. The remaining question is which observations
link each track to its candidate's face gallery. This trace establishes
the confirmation trigger without establishing the identity of every
track in either candidate. Local trace: `build/goal-031-katniss-separation-trace-01/trace.json.stderr.log`.
