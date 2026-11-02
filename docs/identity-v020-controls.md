# Character identity controls for v0.2.0

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
