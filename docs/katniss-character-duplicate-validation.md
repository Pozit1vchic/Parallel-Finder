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

An experimental pre-Matcher face recheck sampled 1,568 frames on the
first film and took about 50 seconds with the prepared analysis cache.
It proposed 188 additional track/shot admissions and no exclusions. In
the control scene it produced three positive votes on 6275 and no
qualifying decision on 6277. Earlier diagnostics linked a Katniss
observation just before co-visibility to 6275, so aggregating by
track and shot may cross a tracking switch. This policy did not pass
the control and is disabled by default. The opt-in diagnostic flag
is `PF_AUDIT_TARGET_FACE_RECHECK=1`.

Decoded frames locate the switch at a real hard cut between sampled
timestamps 4999.536 and 4999.703 s. Track 6275 follows Katniss in a
close-up before the cut and Peeta on the left after it. The prepared
cache assigns both sides of this cut to scene 862, so a track/scene key
cannot isolate the two people here. Track 6277 starts on Katniss on
the right after the cut. This directs the fix toward cut detection
and track continuity before identity voting.
Local evidence: `track-transition-scenes.json` in the face-backed run,
and `transition-frames.jpg` in the prior first-film confirmation run.

The existing native interval detector found exactly one boundary on
4998.9–5002.1 s, at 4999.703041667 s, with score 1.82536 relative to
its threshold. The desktop first proposes cuts from sparse scene
samples, then refines already proposed boundaries and retracks. This
interval demonstrates a boundary that the native detector can resolve
but which is absent from the final desktop scene assignment. It does
not yet isolate whether the initial sampled detection or its refinement
lost the cut.
Local evidence: `control-native-cut.json` in the face-backed run.

A read-only replay split the cached windows at that confirmed timestamp.
Track 6275's static window split into 4999.036–4999.536 s and
4999.703–5000.037 s in different scenes; track 6277 remained on the
second side. The split invalidated inherited identity evidence for
22 affected windows. This verifies that the existing split primitive
can isolate the samples, while identity must be recomputed before
either side can be admitted as the selected person.
Local evidence: `native-cut-cache-replay.json` in the face-backed run.

A shot-local scene sequence provides a short proposal for this cut. The
4 Hz cached sequence in scene 862 proposes 4999.536–4999.786 s
(score 1.637); native decoding within that bracket confirms exactly
4999.703 s. Across 898 cached sequences the proposal pass returned 453
brackets. Only the control bracket was decoded in this check; the
remaining candidates are unverified and the count is not a count of
missed cuts. Local evidence: `sequence-cut-candidates.json` and
`sequence-bracket-native-cut.json`.

The initial desktop scene sampler also runs at 4 Hz. The shot-local
sequence starts from a different time origin and uses a different
descriptor and proposal rule. This comparison therefore establishes
that the cached sequence can propose the cut, but does not show that
raising the desktop sampling rate would solve the issue.

The full first-film diagnostic with native candidate confirmation found
the same cut at 4999.703042 s before building Matcher windows. It checked
6,137 proposals and produced 1,560 distinct boundaries after merging
with the 938 original boundaries. The confirmation step took roughly
233 seconds on this run. The outcome confirms cut detection in the
diagnostic configuration. After selecting candidate 2, the Matcher input
contained 15 checked Katniss observations after the cut, up from zero,
and zero checked Peeta observations, down from three. The two windows
crossing the cut disappeared. The four checked pre-cut Katniss
observations also disappeared. This isolates the identity switch but
does not yet preserve all known target coverage. Local evidence:
`build/goal-031-katniss-native-cuts-01/cut-control.json`.

The diagnostic search finished with 26 results versus 20 in the
earlier selected-character run. The unfiltered diagnostic cache still
contains static windows 4999.036–4999.536 (track 6565) and
4998.702–4999.536 (track 6566). The pre-cut interval therefore still
has prepared pose windows, even though none of the four reference
observations survived selected-character admission. The remaining
issue is identifying the correct pre-cut track and recovering it for
selection. Local evidence: `all-track-window-review.json` and
`native.json` in the native-cuts run.

A targeted replay of six pre-cut timestamps found an accepted Katniss
face on each frame, with similarities of 0.810–0.856 to the selected
gallery. The strict same-frame pose association independently linked
only the 4999.036 s detection to cached track 6565 (similarity 0.841).
Thus the footage contains strong face evidence, but copying the other
five embeddings from a separate pose inference run would lack a
verified track association. The next useful check is to score the
decoded faces using the cached pose coordinates directly.
Local evidence: `precut-admission.json` in the native-cuts run.

With both diagnostic passes enabled, the face recheck on the split
windows recorded four positive observations and no contradictions for
track 6565. The selected Matcher input now contains all four checked
pre-cut Katniss observations and all 15 post-cut ones, with zero
checked Peeta observations and zero windows crossing the cut.
The face recheck proposed 109 additional track/shot admissions across
the film; those have not been fully reviewed. Both passes still require
diagnostic flags. Local evidence: `cut-control.json` and
`rechecked.json.stderr.log` in the native-cuts run.
