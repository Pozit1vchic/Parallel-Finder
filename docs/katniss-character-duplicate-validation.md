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
No media or embedding vectors are published in this report. The four-source
capture remains a separate, unfinished test.

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
