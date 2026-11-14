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

The cached pose coordinates place track 6275 on Peeta (left) and track
6277 on Katniss (right) at all three triggering timestamps. Peeta's
prepared-window face vector scores 0.85177 against its assigned gallery
(candidate 2) and 0.86300 against candidate 45. Katniss's scores are
0.69833 and 0.69857, respectively. The two track vectors score 0.57572
against each other. Their positions establish two people, but neither
track's cached face score distinguishes the two candidate galleries:
the assignment margins are −0.01123 and +0.00024. These are prepared
window vectors, not independent face observations at each timestamp.
The result supports testing a requirement for track-to-gallery evidence
when near-identical galleries are confirmed by co-visibility.

Local evidence: `build/goal-031-katniss-confirmation-after-02/full-overlap-face-review.json`.

The first-film CUDA run with the face-backed separation rule rejected
6275/6277 as evidence of distinct identities. Candidate 2 stayed numbered;
candidate 45 became an unresolved appearance. Their track counts remained
404 and 247 respectively, so this removes the duplicate numbered entry
without resolving the split in track membership. Selecting candidate 2
still needs coverage verification.

Local evidence: `build/goal-031-katniss-face-backed-01/verification.json`.

The desktop search after selecting candidate 2 prepared 2,629 Matcher
windows from 397 tracks. It retained two windows from the Peeta track
6275 and none from the Katniss track 6277. The numbered-card correction
therefore does not fix the underlying target admission on this scene.
Use those two tracks as positive/negative controls for later recovery
and exclusion changes.

The selected-character desktop run completed with 20 returned pairs.
The cached windows covering the three confirmation timestamps contain
zero direct face samples for either track. The aggregate window face
scores reported above cannot be treated as per-frame identity evidence
for this scene. A subsequent correction needs evidence from other
observations or a targeted face recheck.

Local evidence: `build/goal-031-katniss-face-backed-01/direct-face-overlap.json`
and `selected.json`.

Targeted re-detection found a usable face on Katniss at all three
timestamps and none on Peeta. Katniss's fresh samples scored
0.607/0.614, 0.641/0.660 and 0.467/0.477 against galleries 2/45.
Both galleries agree with the visible Katniss samples, and their
score differences remain below 0.02. Recovery based on choosing
whichever gallery scores slightly higher would still split her
appearances; an absent face on Peeta is also insufficient to
reject his track by itself.
Local evidence: `build/goal-031-katniss-face-backed-01/refresh-gallery-scores.json`.

Nearby frames from the same shot provide both sides of the control.
Between 5000.787 and 5002.039 s, three frames have accepted face crops
for both people: the right-hand Katniss scores 0.630, 0.689 and 0.604
against gallery 2; the left-hand Peeta scores 0.206, 0.239 and 0.157.
These checks support testing a shot-local recheck with positive and
contradictory face evidence. The diagnostic re-detected people by
frame; it did not yet associate those later observations to cached
track IDs or alter Matcher admission.
Local evidence: `build/goal-031-katniss-face-backed-01/refresh-nearby-gallery-scores.json`.
