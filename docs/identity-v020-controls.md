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

Eleven visibly reviewed Butcher pairs are recorded in `butcher-identity-controls.json`: six same-person pairs and five different-person pairs. On the saved tracks four same-person pairs join, two split, two different-person pairs remain separate, and three cross-view pairs of different men are incorrectly joined. This tiny control set does not validate the other appearances or resolve the coverage gap. The false join of A-Train and Mother's Milk in candidate 24 is a release-blocking error; a second clear Mother's Milk view tests against merely detaching one bad track.

`butcher-independent-identities.json` adds nine separately reviewed pairs for
other people in the same montage: Starlight, Frenchie, Kimiko, Hughie, the
older man and Butcher. The v16 frozen-track replay joins all five labelled
same-person pairs and separates all four different-person pairs. These
examples protect against solving the reported bridge by fragmenting unrelated
identities; they are not a representative accuracy estimate.

The subsequent v16 continuity change links one additional, independently reviewed Butcher profile across a nearby cut: five same-person pairs join and one splits; the three incorrect joins remain. On a fresh v16 run of the Kyle video, the application and frozen-track replay agreed on all 23 candidate IDs, track counts, durations and preview times, with the same 9 joined positive, 10 separated negative and 3 split positive controls. A fresh v16 run on the full Butcher video produced 491 candidates, 480 unresolved, and its candidate IDs and Butcher track count (487) matched the replay of its saved tracks. The 11 Butcher controls gave 5 joined positive, 2 separated negative, 3 incorrect joins and 1 split positive. The first comparison accidentally used a stale probe; after rebuilding the current probe, its output agreed with the application. This remains a small control set and does not establish general accuracy.

An offline experiment withheld face embeddings when the detected eyes were
less than 16 pixels apart in this particular 1280-pixel-wide inference frame.
On frozen Butcher tracks it reduced the selected incorrect joins from three to
one but increased split positives from one to two, including two appearances
of Mother's Milk; Kyle's selected controls did not change. This was not adopted:
a fixed pixel rule is resolution-dependent, the remaining false join is still
critical, and the lost same-person link is independently confirmed. The result
suggests face sampling quality matters but does not validate that cutoff.

The problematic seed contains track 24 (A-Train, 15.22–16.39 s) and track 60
(Mother's Milk, 29.86–30.70 s), joined through track 182 (Mother's Milk,
84.69–85.69 s). Mean face prototypes score approximately 0.435 for 24–60,
0.600 for 24–182 and 0.677 for 60–182; consequently transitive linking can
join both people. Another clear Mother's Milk view, track 814 around 547 s,
is also placed in that group. Eye spans in the 1280-wide inference frames are
approximately 32 px for track 24, 18 px for track 60, 15 px for track 182 and
49 px for track 814. These are observations from one montage, not calibrated
model probabilities. Track 1058 (another Mother's Milk view) is especially
ambiguous: its face prototype scores 0.479 against track 24 and 0.440 against
track 814, despite a 0.625 match with track 60. A safe fix needs to prevent
the bridge without promoting weak isolated matches or fragmenting the same
person; simply changing one similarity threshold has not met those conditions.
