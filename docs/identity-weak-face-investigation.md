# Weak face detections: local research, not production behavior

The supplied `butcher_parallel_notsorted.mov` is 3840×1632. Several
recognizable Butcher tracks in the saved v16 observations have no face
embedding. A first hypothesis that downscaling alone caused the missing faces
was not supported: OpenCV YuNet also detected selected faces in 1280×544 crops.
The application uses its own fixed-size YuNet preprocessing and a confidence
threshold of 0.85, as well as face size, eye span, nose and ownership checks.

On the saved tracks, 24 tracks with at least four observations, no stored face
embedding and an observed nose were selected at evenly spaced positions in the
track list. Two frames per track were extracted from the original video at the
saved timestamps and scaled to 1280×544. The existing `pf_face_probe` ran the
application's CPU detector and recognizer at thresholds 0.65, 0.75 and 0.85,
with the saved pose nose and forward alignment. At 0.65 it accepted 7 of 48
frames from four tracks; at 0.75 it accepted one frame, and at 0.85 none.
These tracks were selected for missing face evidence, so the numbers are not
representative of the whole video. Three tracks had two accepted frames, with
within-track face cosine scores 0.851, 0.762 and 0.813. Visual inspection
showed Butcher in tracks 1120 and 1242; track 352 showed a different person
out of focus. The face embeddings of different Butcher shots can still be
very dissimilar: selected tracks 782 and 1120 scored 0.156.

Separately, three source frames of track 782 at 525.709788, 526.210288 and
526.543955 seconds produced detector scores 0.754, 0.747 and 0.761. To check
ownership, all other saved detections with a confident nose at the same
timestamps were also passed through the face probe. Their measured landmarks
were checked against the geometry and duplicate rules of
`enforceUniqueFaceOwnership`. All three 782 faces passed. In a broader check of
13 weak faces and their simultaneously visible detections, two weak faces
failed the existing ownership geometry; this is why adding all weak vectors
directly would not be a valid production experiment.

An offline replay inserted **only the three ownership-checked 782 vectors**
into the corresponding saved observations, then called
`pf_character_probe --frozen-tracks`. It changed the group count from 491 to
490 by joining tracks 782 and 842; both show Butcher, with 842 showing his
back in a scene with a mirror. The counts of all existing applicable Butcher
identity fixtures, including the selected different-person controls, were
unchanged. This is one recovered link, not proof of overall precision, and it
does not address the other known missed links or remove mirror images.
The two selected positive/negative comparisons are recorded without image
data in `tests/fixtures/butcher-weak-face-controls.json`. The baseline replay
splits the positive pair and separates the negative pair; the offline replay
joins the positive and still separates the negative.

At the separate poster control around 441 seconds, the face probe did not
accept the printed woman's face at thresholds 0.65, 0.75 or 0.85 on any of
three selected frames. Three selected frames each of the real person next to
the poster and two other real people were accepted at all three thresholds.
This tests face acceptance in one scene only; it is not a poster filter and
does not show that other images of people will be rejected.

Two process-level CPU runs of the 48-frame face probe took 1.267 s each at
0.85 and 1.440/1.459 s at 0.65, including startup and file reads. This is not
an end-to-end application benchmark. A production design would need to retain
weak faces separately, apply ownership and multiple independent observations
before using them as identity evidence, and invalidate the character-analysis
cache key; the serialized cache format need not change if only the final
ordinary results are saved. Repeated detections can still belong to a poster
or the wrong person. The weak threshold remains **disabled** pending broader
positive, negative, poster and end-to-end performance validation.

The local models were the already-used YuNet
`8f2383e4dd3cfbb4553ea8718107fc0423210dc964f9f4280604804ed2552fa4`
and SFace `0ba9fbfa01b5270c96627c4ef784da859931e02f04419c829e83484087c34e79`.
Their upstream licensing references are in `docs/matcher-validation.md`.
Local probe inputs, outputs, contact sheet and the modified track replay are
under `build/research-identity/`; they contain derived face data and video
frames and are intentionally not committed. The supplied source video and
those local artifacts are required to reproduce these particular measurements.

## Full-video experimental recovery

An explicitly opt-in `PF_AUDIT_WEAK_FACES=1` prototype samples the original
0.85 detector first, and retries unsuccessful crops at 0.65. Face ownership
is checked against all persons in the frame; a weak crop cannot erase an
already accepted strong face. Weak embeddings are withheld from tracking and
used only after shot retracking if at least three observations span at least
0.3 seconds, each pair agrees by cosine 0.70, and none conflicts with an
existing strong face in that track. The experimental cache key is separate.
This code remains disabled by default.

On the complete 12-minute Butcher source, a cold CUDA run yielded 96 recovered
observations. All 9,573 saved person observations resolved against the prior
run by timestamp and box; 5,669 observations already had a strong face and
none lost it. Frozen-track replay changed candidate counts from 491 to 485,
and unresolved counts from 480 to 474. The previously reviewed Butcher
profile/back pair now joins; the reviewed blurred different person stays
separate. Existing applicable selected Butcher identity fixtures show no new
false positives; four other previously known visible-person misses remain.
These fixtures overlap and are not a general precision estimate. Several
newly associated views are dark or occluded and require independent review;
the small reduction does not resolve the user's duplicate-identity problem.

## Two-observation audit and portrait counterexample

The v3 diagnostic capture also records weak vectors that did not pass the
three-observation rule. `tools/benchmark/replay_weak_face_candidates.py`
replays these remaining vectors into a separate local track file, which can
then be evaluated using `pf_character_probe --frozen-tracks`. This is an
offline research tool; it does not change application behavior.

Requiring two observations with cosine at least 0.70 promoted 22 additional
observations across 11 tracks, but did not change the 484 groups / 473
unresolved produced by the three-observation experiment plus overlapping-head
recovery. Lowering that consistency threshold to 0.50 promoted 83 additional
observations across 24 tracks and produced 482 groups / 470 unresolved.
Selected identity controls still passed their previous outcomes, but this is
not sufficient evidence to enable the rule: it also split three tracks from
the previous 490-track lead component into another component.

Visual review found that tracks 143, 325 and 1086 are all detections of the
same framed portrait, rather than a person physically present in the scene.
The weaker replay joins the third portrait occurrence to the first two. Those
three observations are now listed as printed images in
`tests/fixtures/butcher-printed-person-controls.json`. This is an explicit
counterexample to treating improved face grouping as improved person detection.
These settings stay disabled; two observations alone and the lower consistency
threshold do not solve the hundreds of unresolved appearances.

The local artifacts are `build/butcher-weak-face-v2-tracks.json`,
`build/butcher-weak-face-v2-replay.json`, `build/butcher-weak-face-v2-stderr.txt`
and `build/weak-face-context.jpg`. They contain derived video/face data and
are not committed. Sixty-four focused identity/face tests pass. The prototype
must remain opt-in until broader same/different-person controls, ambiguous
competing detections, poster/reflection cases and end-to-end cost are checked.

## Outlier-tolerant weak-face consensus counterexample

An offline diagnostic tested retaining a mutually consistent subset of at
least three weak faces spanning 0.3 seconds, requiring at least 75% of the
available weak observations and the existing 0.70 face agreement / 0.50 strong
face veto. It promoted 41 observations across eight tracks in the v3 Butcher
capture. Frozen-track replay stayed at 484 groups / 473 unresolved, but group
membership changed: the framed portrait track 1086 joined portrait track 143.
Consequently an unchanged group count does not mean unchanged identities, and
the proposed consensus is not safe to use as a person filter or to ship.

The research-only `tools/benchmark/replay_weak_face_consensus.py` reproduces
this diagnostic from the local v3 capture; its output contains face vectors and
must remain local. Its two focused tests pass. This experiment does not prove
the remaining changed associations are correct, and no application behavior
was changed.

## Review of weak-face group changes

`tests/fixtures/butcher-weak-recovery-review-controls.json` records eight
selected same-person pairs reviewed from actual source frames. The production
v19 replay joins five and splits three; the opt-in v2 weak-face replay joins
seven but splits the repeated car scene that production joined.
The latter views at 332.87 and 688.56 seconds are the same scene, so the split
is a concrete regression even though the total candidate count decreases.
These cases share anchors and were selected from the experiment; they are not
an independent estimate of accuracy. Other newly linked, dark or obscured views
are still not independently verified.

The strong face on the first car track has cosine 0.601–0.726 against six
recovered faces from the repeated view; their body prototypes score 0.927.
However, its single face scores below 0.363 against the averaged faces of 12
other tracks in the experimental lead group, including several visibly
recognizable Butcher views. This demonstrates conflicting evidence, not the
precise rejection path: the face thresholds alone cannot establish whether
the whole group is safe to merge. The experimental rule remains disabled.
Source-frame review sheets are local `build/weak-merges-review-v2.jpg` and
`build/weak-conflict-review.jpg`; they are not committed.

## Local face-and-body corroboration experiment

Diagnostic stage tracing on the weak-face v2 frozen observations shows that
the repeated car views are already in different groups after initial seeding.
One group has a single face; the other contains over 4,000 observations. In
the subsequent gallery pass, the pooled body cosine is 0.737–0.739, below the
0.78 gate. The three best sampled gallery-face scores are below the separate
0.65/0.55 rule as well; the bounded whole-group gallery can omit the particular
repeat track whose individual face and body evidence agree.

A local-only variant considered a single-face group against individual tracks
of the other group. It required three independent faces from one track, the
existing 0.65/0.55 top-face thresholds and 0.90 body cosine, while retaining
the existing component conflict checks. On the weak-face v2 replay it joined
the split repeated car views: 484 groups became 483 and unresolved groups 473
became 472. All eight selected weak-recovery positives then joined; the selected
different-person controls remained separate. This experiment examined only one
source with added weak faces and does not establish general precision. On the
normal v19 Butcher and v16 Kyle frozen observations, component membership did
not change. This initial option was not added to the application. The experimental diff and
replays are local build artifacts (`weak-local-face-body-experiment.patch`,
`weak-local-face-body-experiment.json` and the corresponding Butcher/Kyle
replays); the later implementation and its limitations are described below.

An additional exhaustive diagnostic on individual tracks with exactly one
stored face and at least three observations, compared with tracks having at
least three faces, found five pairs meeting the experimental face and body
thresholds in the weak-face v2 Butcher capture. Their sampled frames show the
same person in each pair; four pairs are within the same shot, overlapping, or
repeated footage. No distinct simultaneous pair passed those raw thresholds in
this one capture, which does not prove cross-scene negatives are safe. Four
additional positive pairs were added to the fixture above. The diagnostic is
track-level, whereas production recovery works on assembled components.

An initial single-run frozen replay recorded 256 ms without local corroboration
and 351 ms with the unoptimized prototype; the separate v19 and Kyle runs also
showed higher elapsed times. These are diagnostic observations, not controlled
performance estimates. The prototype repeatedly summarizes tracks inside nested
comparisons and needs optimization and controlled A/B timings before use.

## Track-local corroboration in character grouping

The regular character grouping now also checks individual tracks when a
one-face component is compared with a larger gallery. It requires a body
cosine of at least 0.90, one face score of at least 0.65 and a third score of
at least 0.55 from independently timed faces of that same track. Faces with
conflicting embeddings at one timestamp are excluded. Existing co-visibility,
face-conflict and component-level vetoes still apply. Track summaries are
prepared once instead of repeatedly inside the group-pair loop.

On the saved weak-face v2 observations, the optimized implementation produces
exactly the same assignments and candidate records as the earlier local
prototype, including the reviewed repeated car view. Eight selected positive
controls pass and applicable selected negatives remain separate; those cases
overlap and do not establish general precision. Weak-face recovery remains
disabled by default, so this is not evidence that the normal Butcher analysis
now joins that car pair. On normal v19 Butcher and v16 Kyle saved observations,
all assignments remain identical to the prior implementation, including the
selected different women. Three alternating clustering runs measured medians
of 191.4 ms before and 201.3 ms after on normal Butcher, and 199.1 ms before
and 209.3 ms after on weak-face observations. These do not measure video
analysis time. Sixty-five character and dominant-person tests passed, including
new controls for insufficient face support, body disagreement, and co-visible
distinct people. The character cache key changes to v21, so older character
analysis entries are not reused. This does not filter pictures or mirrors.

A fresh CUDA run of the full Butcher video with v21 and weak faces disabled
reached character selection successfully. It produced 490 candidates, 479
unresolved, and the same candidate metadata as a fresh v20 run after excluding
session-specific preview URLs. All 1,437 unique previews existed. This is an
application-path regression check, not evidence of improved normal-mode recall;
the major fragmentation problem remains. Local reports are
`build/butcher-v21-validation.json` and `build/butcher-v21-validation.log`.

The existing Kyle observations add one selected user-provided negative to the
local-corroboration investigation. The younger woman's track 66 has one stored
face; comparing it with the different older woman's track 74 gives body cosine
0.652 and its three highest face scores 0.356, 0.352 and 0.340. These do not
meet the experimental 0.90 body and 0.65/0.55 face thresholds. A positive
comparison of track 66 with the younger woman's track 18 also fails these
thresholds (body 0.730; top faces 0.475, 0.463, 0.449). This one overlapping
positive/negative selection rejects the negative but does not independently
support the positive through this specific rule; it does not establish broader
precision or recall. The normal application already joins that positive pair.

Separately from that disabled experiment, the production face-conflict veto now
stops comparing two tracks as soon as it finds a face pair at or above the
existing 0.30 threshold. Only existence of such a pair affects this veto; no
threshold or identity rule changed. On the same computer, three alternating
before/after frozen-track runs gave median 199.4/169.9 ms for the normal v19
Butcher capture and 237.1/197.6 ms for the weak-face v2 capture. Candidate
records and every saved observation assignment were identical in all six runs
of each capture. These timings cover clustering after loading observations,
not video decoding, model inference, or end-to-end analysis. Sixty-three
focused character and dominant-person tests passed.

## Fresh capture after chained face-ownership fix

The production v19 character-selection path was run cold on the complete
Butcher video with CUDA, using `realVideoCharacterCapture` and isolated settings
and cache. The diagnostic capture and frozen-track replay contain 1,266 tracks,
490 candidates and 479 unresolved candidates. The prior v18 frozen replay also
had 490 candidates and 479 unresolved; this does not prove that every
assignment is identical. Eight applicable reviewed control sets retained their
v18 outcomes, including the weak-face control: the known same-person profile
and back view remain split and the selected different-person pair remains
separate. These overlapping, selected pairs do not measure general accuracy.
The change addresses a possible face-ownership conflict, not the general
fragmentation problem.

The cold capture logged 175.3 seconds in the pipeline and another 47.0 seconds
for 2,167 uncached candidate previews. These are separate stage timings from
one run, not an end-to-end performance comparison. The preview cost is a
candidate for further investigation. Local capture, tracks and replay are in
`build/butcher-v19-candidates.json`, `build/butcher-v19-tracks.json`, and
`build/butcher-v19-replay.json`; they are not committed.

The preview path now deduplicates exact source timestamps while retaining each
appearance's own time, bounding box, and candidate. On the same saved Butcher
character artifact, 2,167 requests map to 1,437 frames. With the character
artifact cached but all preview entries absent, the optimized stage took
22.0 seconds once; all preview files existed and requests with the same time
shared the same URL. Candidate and appearance metadata matched the previous
capture. The earlier 47.0-second measurement followed cold video analysis,
so these separate runs are not a controlled speedup measurement. A warm run
reused all 1,437 existing representative preview entries in 2.7 seconds.

Character preview encoding now uses three bounded workers on machines with at
least eight logical threads; smaller machines retain one worker. Video decoding
remains sequential, and cache batches are serialized. In four alternating
isolated-cache runs of the same cached Butcher character analysis, with no
preview entries initially present, one worker took 22.059 and 22.152 seconds;
three workers took 15.149 and 15.013 seconds. Each run prepared the same 1,437
unique frames for 2,167 appearance requests. All preview PNG payloads matched
byte for byte and all 490 candidate records matched after excluding their
session-specific URLs. These timings cover only preparation of uncached
previews on this computer, not video analysis, warm-cache performance or other
hardware. The focused preview tests also cover queue draining, failed writes,
cache ownership and reset with multiple workers.

## Duplicate timestamps in character evidence

Character-candidate summaries now count at most one valid face and one valid
body vector per finite timestamp within a track. This prevents repeated records
from a single frame from reaching the two-observation identity threshold by
themselves. The character artifact key changes to v20; previously saved
character selections require reanalysis. The separate dominant-source
selection policy and its motion cache key are unchanged.

The duplicate-frame regression verifies that three copies have the evidence
strength of one sample and do not establish a distinct identity. All 64
character/dominant-person tests passed. Frozen v19 Butcher and v16 Kyle
observations have no duplicate timestamps within a track; after this change,
all their candidate records and assignments remain identical. This is a guard
against fabricated repeated evidence, not a reduction of the 479 unresolved
Butcher appearances or evidence of broad recognition accuracy.

## Repeated scenes and split identities

`butcher-repeated-scene-controls.json` records two visually reviewed pairs of
the same person in repeated shots and one pair of different women visible in
the same shot. The current v21 baseline splits both same-person pairs and keeps
the different women separate. These are selected examples, not a population
accuracy measure. The source frames remain local.

Two research diagnostics examine whether existing image evidence could help.
`audit_repeated_scene_identities.py` reads character preview cache entries;
`audit_repeated_motion_identities.py` uses the 4 FPS scene sequences and pose
windows already prepared for the matcher. On the saved Butcher data, each
diagnostic nominated one split pair: tracks 189 and 940, visibly the same woman
at the right side of a repeated room shot. The second known same-person pair
was not nominated by these thresholds. The scene-window diagnostic compares
three successive thumbnails, requires body similarity and independently timed
nearby pose landmarks, and does not propose joining the different women in the
selected control. Similar backgrounds, clothing and poses can still occur for
different people; this small sample does not establish a safe merge rule.
Neither diagnostic changes application grouping.

The application now applies a separate bounded-evidence recovery for this
case before character selection. It requires three consecutive visually close
scene samples with variation in both shots, two independently timed matching
poses, and body similarity. It rejects competing candidate links, inconsistent
face evidence and conflicting simultaneous people. It works from the existing
scene-sequence and pose caches and keeps source track IDs. The selection dialog
sorts appearances by time, and a previously saved candidate ID that is now a
member of a merged group resolves to the merged candidate.

On the supplied Butcher video with the v21 cache, the actual selector merged
tracks 189, 940 and 1062 into candidate 189: 490 candidates became 489 and 479
unresolved became 478. Candidate 940 was removed, candidate 189 changed, and
all other candidate metadata matched the previous capture apart from numbering
and session URLs. All 1,437 preview PNGs matched the earlier run byte for byte.
Selecting the merged row admitted windows from all three tracks; the search
finished with zero matches on that selection. A rerun with the old saved ID 940
also selected the merged group without prompting and admitted the same tracks.
The local scene-link probe took 219 ms once on the saved Butcher windows; this
is not an end-to-end benchmark or evidence for other videos. Synthetic checks
cover differing positions, body and face conflicts, co-visible people,
ambiguous links and cancellation. The two earlier visually reviewed positives
are not independent: the other pair remains split. The two selected different
women remain separate. No population-wide error rate, portrait or mirror
filtering is established by this example.

A third repeated-shot control, the same blonde woman in the shop, now joins
tracks 107 and 861. For this less similar image sequence the rule requires
three independently timed, closer pose matches and visible variation in both
shots, along with body agreement and the existing conflict checks. The three
selected same-person repeated-shot controls now join and the selected different
women remain separate; nine other applicable Butcher control sets retain their
previous outcomes. This is a small, correlated collection, not a general
accuracy or false-merge rate. The disjoint simultaneous-person audit reports
zero conflicts but cannot detect all wrong merges.

The cached application capture showed three merges and retained the maximum
of 20 visible unresolved entries. Selecting candidate 107 admitted only tracks
107 and 861; a subsequent run with the old saved ID 861 selected the same
group. Both searches completed with zero matches. On the saved full-video
windows, one diagnostic run of repeated-scene recovery took 778 ms; this is
not a controlled end-to-end performance comparison. Other unresolved people,
and detections of images or mirrors, remain uncorrected.

Character preview keys now use the exact source timestamp rather than a
candidate ID and appearance index. Group recovery and chronological display
sorting can change those indices without changing the cached analysis, so an
old index key could otherwise name a different frame after a later grouping
change. Existing analysis remains reusable; only character previews are
regenerated once under the new key. On the current Butcher selector, all 110
unique previews matched the corresponding earlier PNGs after reconstructing
the original pre-sort appearance order, and all candidate metadata matched.
The next run reused all 110 new preview entries; selection, the 20-entry limit,
the on-screen total and the merged-target saved-choice regression passed.

The preview-cache research diagnostic now reads timestamp keys by default. It
does not infer timestamps from older candidate/index keys unless explicitly
invoked with `--assume-legacy-unmerged`: after a merge and display sorting,
the displayed index need not be the cached index. The earlier pre-merge v21
capture was generated before those merges and remains suitable for that
explicit legacy mode. This corrects the diagnostic's assumptions; it is not
evidence that the previously displayed application previews were wrong.

The selector now displays all confirmed identities and at most 20 unresolved
appearances, ordered primarily by duration, with any current suggestion kept
in the displayed set. It reports how many unresolved appearances exist in
total. Entries beyond the limit cannot be selected manually from this list;
the complete candidate and track data remain in the analysis cache, and the
all-people search is unaffected. This is a display limit, not an assertion
that the hidden entries have been recognized or merged.

The real Butcher selector showed 11 confirmed identities and 20 unresolved
appearances out of 478. It prepared 107 unique preview frames instead of the
previous 1,437; a warm mixed-cache run took 857 ms for preview preparation,
with 93 cache hits. The selector UI test checked that the notice shows both
20 and 478; the selected merged group still admitted all three tracks and a
saved pre-merge selection still resolved to it. These figures concern this
source and run only, not a general performance guarantee.

The repeated-scene rule now permits a larger difference between three scene
samples only with stronger body similarity (at least 0.90 rather than 0.85).
On the saved Butcher data it adds one visually confirmed same-person link:
track 184 and track 1010 are two views of the same man in the repeated forest
shot. The prior room-shot link remains. Replaying both associations changes
the selected repeated-scene controls from three split positives and one
separated negative to two joined positives, one split positive and one
separated negative. Nine other applicable Butcher fixture sets retained their
previous results; a disjoint same-frame co-visibility audit found no new
conflicts. These correlated controls and that narrow audit do not establish a
general error rate.

The application reused the v21 cache, showed both merged groups, and retained
the 20-entry unresolved display limit. Its character capture, selector notice,
merged-target search and saved old-ID rerun all passed. The saved-window probe
took 452 ms once versus 219 ms in the earlier single measurement; these are not
controlled end-to-end timings. The weaker scene threshold is paired with the
stronger body gate and the existing pose, timing, face, simultaneous-person
and ambiguous-link vetoes. It does not address pictures, mirrors or the still
large number of split identities.

Repeated-scene recovery now precomputes coarse sums for each three-frame
sequence. A pair is skipped only when the difference between those sums is
already larger than the existing full-pixel error limit; accepted pairs still
undergo the original exact comparison and all identity checks. Out-of-range
pixel values use the full comparison, and a small numerical margin protects
threshold-adjacent cases.

Three alternating runs on the saved Butcher windows measured 1,077.4 ms median
before and 291.9 ms after for this recovery stage. All three links and their
scores were identical across runs. A separate synthetic set produced the same
20 links before and after, including threshold-adjacent and out-of-range
values. The actual cached selector produced identical candidate metadata (apart
from temporary URLs), retained the 20-entry limit, and passed merged-selection
and saved-choice checks. These measurements cover one stage on one machine,
not full video analysis or general recognition accuracy.

Reproduce the scene-window audit with the local saved artifacts and a Python
environment containing NumPy:

```powershell
build/ucrt64-release/tools/pf_matcher_probe.exe build/butcher-all-people-windows-v18.json --scene-neighbours > build/butcher-neighbour-scenes-id.json
python tools/benchmark/audit_repeated_motion_identities.py --windows build/butcher-all-people-windows-v18.json --replay build/local-face-v19-replay.json --neighbours build/butcher-neighbour-scenes-id.json --output build/repeated-motion-identity-audit.json
python tools/benchmark/evaluate_identity_controls.py build/local-face-v19-replay.json tests/fixtures/butcher-repeated-scene-controls.json
```
