# Character identity controls for v0.2.0

After the local 0.2.0 package was built, a separate development change made a
co-visible, face-backed person count as an ambiguity competitor for body-only
recovery. A synthetic control previously merged the fragment into the wrong
person and now keeps them separate; a dissimilar co-visible person does not
prevent an otherwise supported join. All 22 CharacterCandidates tests pass.
Frozen-track replays of Kyle (74 observed tracks, 23 groups) and Butcher
(1,164 observed tracks, 491 groups) retained the same group assignments.
This does not recover the known missing links. The cache key is bumped to v17;
existing character-analysis artifacts need reprocessing; a fresh full search
after target selection has not yet been checked. This change is not in the already
built `0.2.0-d2f9492d` packages.

A subsequent v17 application inspection on the supplied Kyle video used the
same local YOLO26m model under its `yolo26x-pose.onnx` filename alias and the
same OSNet model described below. The fresh run reached the character picker
and produced 23 groups; a repeated run produced the same IDs and track counts,
with `complete_cache_hit=1`, `decoded=0`, and `pose_samples=0`. Both inspection
runs intentionally stop at selection and consequently report exit code 1;
they are not full searches or exports. All candidate records including preview
times and boxes match after excluding their `url` fields: the URLs point to
different session-owned files, so the raw JSON is not identical. Image bytes
were not compared.
Logs and candidate files are under `build/kyle-v17-{fresh,warm}-*`.

Release remains blocked: these controls are a small, manually inspected sample, not a measure of whole-video accuracy. In particular, three known same-person pairs are still split.

The source was `KyleCrane_Parallel_NikolayOM.mp4`, supplied by the user. The woman driving around 17.03 seconds and the older woman in the dark room around 59.03 seconds are different people. The two labeled negative controls for them must remain separate. The fixtures also contain visually reviewed examples of repeated appearances and other people; they store timestamps and locations, not video frames or embeddings.

On freshly extracted observations, the current candidate clustering gives these results:

| Fixture | Same, joined | Different, separate | Different, joined | Same, split |
| --- | ---: | ---: | ---: | ---: |
| `kyle-user-women-controls.json` | 4 | 8 | 0 | 0 |
| `kyle-independent-controls.json` | 2 | 2 | 0 | 0 |
| `kyle-additional-controls.json` | 3 | 0 | 0 | 3 |

This checks 22 selected pairs, not every appearance in the video. The three remaining misses include back and profile views; unresolved appearances must not be represented as fully solved. These counts use the actual saved post-tracking tracks with `--frozen-tracks`; their candidate IDs match the application output. The prior version had five misses on the same controls and source tracks. An earlier replay reconstructed tracks from detections and incorrectly suggested only three misses for that version; reassociation changed the tracks. A broader baseline has not yet been established.

The current application was also run on the video with CUDA and the fast profile. The actual pose model was YOLO26m, SHA-256 `dc8741d097d642df343361cb956087e2991140ababcb6e063a6f077968c28fe1`; its local filename used the `yolo26x-pose.onnx` alias to accommodate the existing saved model setting. The OSNet ReID model SHA-256 was `75146f8fa0893808981a8d2d52586789e59521f2b06e3d7415439e7e718dd2ea`. The current cold run produced 23 candidates; a repeat from cache produced the same candidate identities, preview times and boxes, with no video decoding or pose inference. The inspection run intentionally stops at character selection and does not test matching or exported results.

To reproduce the pair evaluation, run `pf_character_probe` on freshly captured track observations and the corresponding scene log with `--frozen-tracks`, then run `tools/benchmark/evaluate_identity_controls.py` against each fixture. Validate additional negative and positive pairs on more videos before treating these results as evidence of release readiness.

On the user's much longer `butcher_parallel_notsorted.mov`, the same fast-profile inspection produced 492 candidates, of which 481 were marked unresolved and 11 were marked distinct. This is a serious coverage problem, not a count of the true number of people; it also does not establish that the 11 confirmed groups are error-free. The run reached character selection after generating 2,177 previews and intentionally stopped there. Independent frame-level identity labels and systematic review are still needed on this video.

## Correction to the Butcher labels

A prior review mistakenly identified the appearance at 15.89 s (track 24) and a later view at 642.20 s (track 1029) as A-Train. On closer review, these appear to be Mother's Milk in his changed season-four appearance, while tracks 60, 182, 814 and 1058 show his earlier, bearded appearance. The actor lost weight and shaved between seasons: [background](https://thedirect.com/article/mothers-milk-recast-actors-the-boys-season-4-change). The 57.20 and 535.53 s shots show the same later-season character and the Fat Joe shirt associated with Mother's Milk ([reference](https://creativetdesign.com/product/the-boys-mothers-milk-fat-joe-shirt/)); the 15.89, 535.53 and 642.20 s shots have the same face and puffer jacket. We withdraw the prior allegation that these pairs prove a false merge. This correction does not change the user's ground truth about the two different women in Kyle.

The affected negative controls were corrected to positive in `butcher-identity-controls.json` and `butcher-bridge-controls.json`, and the mislabeled Frenchie comparison was renamed in `butcher-independent-identities.json`. A broader linked set, `butcher-milk-appearance-controls.json`, records reviewed views across the mixed-era group. The 60 positive pairs share anchors and source shots, so cannot be treated as independent samples or as whole-video accuracy. Some views are very brief or distant, and the group has not been completely reviewed.

| v16 frozen-track fixture | Same, joined | Different, separate | Different, joined | Same, split |
| --- | ---: | ---: | ---: | ---: |
| `butcher-identity-controls.json` | 12 | 2 | 0 | 1 |
| `butcher-bridge-controls.json` | 9 | 0 | 0 | 0 |
| `butcher-independent-identities.json` | 5 | 4 | 0 | 0 |
| `butcher-milk-appearance-controls.json` | 60 | 0 | 0 | 0 |

These fixtures overlap and do not establish a general error rate. The rejected experimental split (`build/identity-split-butcher.json`) scores 5 joined and 8 split positives on the corrected 15-case fixture, while preserving the two selected negatives; it must not be adopted as a repair. The original small low-resolution experiment was also interpreted against erroneous labels, and its old false-positive claims are withdrawn. The eye-span rule was not adopted.

On a fresh v16 run of the Kyle video, the application and frozen-track replay agreed on all 23 candidate IDs, track counts, durations and preview times, with 9 joined positive, 10 separated negative and 3 split positive controls. A fresh v16 run on the full Butcher video produced 491 candidates, 480 unresolved, and its candidate IDs and Butcher track count (487) matched the replay of its saved tracks. The first comparison accidentally used a stale probe; after rebuilding the current probe, its output agreed with the application. These observations do not establish full identity coverage or release readiness.

## Further Kyle back-view controls

A further visual review records six correlated pairs in `kyle-back-collision-controls.json`: the woman at 5.19 s compared with three later back/partial views at 31.53, 34.36 and 37.53 s; her 13.69 s back view compared with 37.53 s; and two distinct-person controls involving the older woman at 56.53 s. On the saved v16 replay, all four same-person pairs are split and both distinct-person pairs are separated. These are additional selected misses, not a new rate for the complete montage. The crop sheet is a local artifact `build/kyle-body-collisions.jpg`, with no image pixels checked into the repository.

A prototype-only body comparison does not justify simply attaching these views. The shirtless man's rear-view track 75 has prototype cosine 0.763 with the older woman's group and 0.673 with his own front-view track 70; the crop contains the older woman in front of him. The younger woman's back-view track 15 scores 0.816 against her other back-view track 47, higher than its 0.773 against track 8 already in her face-backed group. These are local measurements, not probabilities; the first collision is a concrete false-merge risk, while the second suggests missed continuity may run through currently unresolved views. Any change must test both confirmed negatives and reviewed positives rather than lowering a global threshold.

## Independent body-gallery experiment

An experimental recovery rule compared each unidentified track with two temporally separated body-track prototypes from a face-backed group, retaining a margin against other groups. On the saved Kyle tracks it recovered none of the four back-view controls. On the saved Butcher tracks it reduced 491 groups to 480, but one resulting group combined a blonde woman's back at 722.20 s with a dark-haired man's side view at 722.55 s. The new `butcher-backview-collision-controls.json` records this different-person pair: the current production baseline separates it, while the experimental replay joins it. Existing small Butcher controls did not catch this error. The experiment was rejected; its patch and local frame sheet are build artifacts, not shipped code. The pair does not establish the correctness of other experimental merges, nor measure overall accuracy.

## Deferred body-gallery experiment and a new collision

A local-only experiment repeated body-gallery recovery after the final face pass, using two reference tracks and a 0.04 margin. On the frozen Kyle sample it joined the previously split tracks 38 and 41 with the younger woman's face-backed group, without losing the selected prior controls. Deferring the pass avoided the already known Butcher collision between tracks 1174 and 1176. However, reviewing the new Butcher groups revealed a different definite error: track 718 at 492.72 s shows a dark-clothed man, while track 175 at 81.73 s shows Butcher. The experiment joined these different people. `butcher-body-gallery-collision-controls.json` records this negative; the v16 baseline keeps it separate. A contact sheet is a local artifact at `build/bodypair-late-review.jpg`, not checked into the repository.

A further research-only veto of tracks with repeated visible eyes and nose kept those two people separate while retaining the two selected Kyle recoveries. This removes one identified error but is not sufficient evidence for shipping: several other newly joined Butcher observations are too blurred, cropped, or dark to identify independently. The apparent success on selected controls therefore does not establish precision. Neither experimental variant changes application behavior; broader independently labeled positives and negatives are needed before any such recovery is considered safe.

## Additional review of the deferred body-gallery veto

A second visual review confirmed that the frontal-keypoint veto variant also creates a definite false merge. At 697.03 s, track 1131 is the person in shadow on the left, and track 1130 is Butcher clearly visible on the right at the same time. The variant attaches track 1131 to Butcher's group while leaving 1130 separate. This is not merely a question of an unrecognizable face: the two people are visible together. `butcher-body-gallery-positive-controls.json` records this different-person control together with three reviewed Butcher views which the baseline misses and the variant joins. On these four correlated controls, the baseline gives three split positives and one separated negative; the experimental variant gives three joined positives and one joined negative. This small, selected sample measures neither overall precision nor recall. It establishes that the veto variant must also remain experimental. Track 1189, a foreground back view at 734.68 s, remains independently unidentified.

## Simultaneous detections audit

`python tools/benchmark/audit_identity_covisions.py build/butcher-v16-frozen-current.json`
reported no same-identity track pairs whose boxes overlap by less than 10% of the smaller
box on an exactly shared observation time (within one microsecond). To check that the
audit detects a real counterexample, assigning the reviewed tracks 1130 and 1131 above
the same synthetic identity produces one finding with eight shared observations from
696.36 to 697.53 seconds. That synthetic edit is solely a check of the audit and is not
the actual application result. Run `python -m unittest discover -s tools/benchmark -p
test_audit_identity_covisions.py` for its automated edge-case checks.

This identifies only conspicuously separate detector boxes inside one assigned identity;
detector duplicates can still require manual review. It cannot detect identities merged
across different shots, overlapping people, missing detections or split appearances. The
prior experimental collision involving track 1131 and Butcher's main group also evades
this direct check because the simultaneously visible Butcher track 1130 is itself split
off from that group. Zero findings is therefore not proof of correct identification.

## Recognizable additional Butcher pair controls

The local contact sheet `build/bodypair-late-review.jpg` also contains four recognizable
Butcher views (tracks 782, 881, 1120 and 1242) and a separate dark-clothed man
(track 718); tracks 24 and 59 show Mother's Milk and Hughie. The selected fixture
`butcher-visible-pair-controls.json` records four same-person comparisons of track 175
with those Butcher views and seven different-person comparisons. The v16 frozen replay
splits all four positive pairs and separates all seven negatives. The source frames were
visually reviewed; the saved points contain no pixels or embeddings. These examples share
anchors and were chosen from an existing experiment sheet, so the counts cannot be
interpreted as population-level recall or precision. They make concrete that fixing the
previous collision alone would not establish complete appearance grouping.
