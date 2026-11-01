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
