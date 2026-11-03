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

The local artifacts are `build/butcher-weak-face-v2-tracks.json`,
`build/butcher-weak-face-v2-replay.json`, `build/butcher-weak-face-v2-stderr.txt`
and `build/weak-face-context.jpg`. They contain derived video/face data and
are not committed. Sixty-four focused identity/face tests pass. The prototype
must remain opt-in until broader same/different-person controls, ambiguous
competing detections, poster/reflection cases and end-to-end cost are checked.
