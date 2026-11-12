# Low-resolution pose and temporal action validation — 2026-11-11

This is independent action validation, not proof of professional editing-parallel quality. Original application fixtures reference missing `D:/for_tests_pf` videos. Public KTH controls let us investigate a confirmed input defect and compare temporal evidence without inventing labels or person embeddings.

## Data and fixed protocol

[KTH's official dataset](https://www.csc.kth.se/cvap/actions/) contains six actions, four scenarios, 25 subjects, grayscale 160×120 video at 25 fps, generally a static camera and one person. Its published terms permit non-commercial use; downloaded videos remain local under ignored `build`, outside the app and release. Cite Schuldt, Laptev and Caputo, ICPR 2004. This dataset does not establish performance on crowds, camera motion, occlusion, cinematic cuts or different character identities.

`tools/benchmark/kth-validation-plan.json` freezes tuning subjects 11/12, development 19/20, and held-out subjects 2/3/5/6/7/8/9/10/22. We downloaded all six actions and four scenarios: 48 tuning, 48 development, 216 holdout videos. Official one-based inclusive action boundaries are converted to half-open seconds as `(first−1)/25` to `last/25`. There are 863 held-out annotated intervals; every interval stays in the denominator, including missing detections. Selection does not depend on matcher output. Download manifests record source URLs, CRC, size, file hashes, annotation hash and plan hash.

Pose collection uses the actual C++ decoder and estimator at 12 fps, CUDA, four CPU threads, the same `release-models/yolo26m-pose.onnx` on both sides (SHA-256 `dc8741d097d642df343361cb956087e2991140ababcb6e063a6f077968c28fe1`). This existing export differs from another catalog's pinned YOLO26m export; they are not represented as byte-identical. The original extractor executable was preserved before changing preprocessing. Observation manifests include model/source hashes, provider, sample rate and preprocessing contract. Missing observations are retained, not interpolated into detections.

## Confirmed input defect and correction

The previous nearest-neighbor enlargement caused almost no person detections on these small videos. A visible KTH person produced maximum model score 0.020879 before enlargement correction; bilinear reference enlargement of the same image produced 0.937874. The corrected application preprocessing produced 0.937890. A separate existing YOLO11m model showed the same sensitivity. A known larger positive image already worked, so a general failure to load the model was not the explanation.

Enlargement now uses bilinear pixel-center sampling, replicated edges and reusable coordinate/buffer storage. The [Ultralytics LetterBox implementation](https://raw.githubusercontent.com/ultralytics/ultralytics/v8.4.0/ultralytics/data/augment.py) uses linear interpolation by default. Analytical pixel tests cover horizontal and two-dimensional interpolation, padding, tiny images and batch tails. Identity and reduction preserve the prior byte output. No detector confidence threshold or model weights changed.

Only sources that require enlargement invalidate the observation cache with `motion-v44|pose-input-v2-bilinear-upscale|`. Sources whose largest dimension is at least 640 retain their prior cache keys. Frozen replay rejects stale small-source preprocessing explicitly.

## Independent holdout results

The following comparison covers the same 216 videos, 863 original intervals and 38,125 labelled sample times. It combines preprocessing and the documented classifier/ranking fixes; it is not an ablation attributing every ranking change solely to interpolation.

| Metric | Original preprocessing/core | Corrected preprocessing/core |
|---|---:|---:|
| Labelled samples with a detection | 95 / 38,125 (0.249%) | 34,881 / 38,125 (91.491%) |
| Gait precision on supported observed runs | 0, no predictions | 139 / 142 (97.887%) |
| Gait recall on supported observed runs | 0 / 432 | 139 / 432 (32.176%) |
| Gait F1 on supported observed runs | 0 | 0.484321 |
| Unknown interval rate, supported runs | 100% | 31.170% |
| Six-action retrieval Precision@1 | 0, no results | 25.492% |
| Six-action retrieval Recall@10 | 0, no results | 11.114% |

Gait positives are walking/jogging/running; negatives are boxing/handwaving/handclapping. Supported-run evaluation unions non-static facets from production `observedPoseRuns` within each annotation. Full-interval classification remains much weaker: after correction TP=5, FP=5, FN=427, TN=426, F1=0.022624 and unknown=53.071%. People entering/leaving the frame and gaps cannot certify continuous full-interval evidence. The supported-run result must not be substituted for full-interval performance.

Retrieval compares the same known subject across scenarios, retains all annotated actions, and uses the production pose/motion matcher with exhaustive diagnostic candidate limits and appearance admission disabled. Ground truth is strict same-action identity, not a human judgement of editing relevance. Known identity is supplied by the dataset, not discovered by the app. Empty result lists score zero; baseline zero precision does not establish a measured baseline false-positive rate. After correction the six-action ranking remains poor and does not satisfy the full objective.

## Existing video model: temporal versus frozen controls

We reused the existing AVA SlowFast R50 ONNX model, 135,286,691 bytes, SHA-256 `b9316ea2de6a29282fa1567246dc74670052525319e48cbab02b6b91ff10c481`. Its existing source/license/export metadata remain in `models/ava-slowfast-r50.json`. No new ML dependency or model download was added to application inference.

For each source, the first published interval supplies one clip of at most two seconds around the median observed single-person frame, constrained to the annotation. This produces all 48 development and all 216 holdout clips. ROI comes from the actual detector. The probe explicitly records a single-subject dataset selection policy; this is **not** a validated application identity association. Native decoded forward frames, reversed frames and a repeated midpoint are evaluated by the same model. All 80 scores are retained. Policy `kth-semantic-policy.json` was committed in `dc39037` before holdout inference; it reuses the existing walk gates and their symmetric run counterpart, with no threshold fitting on the holdout.

| Frozen-policy classifier on 216 holdout clips | TP | FP | FN | Precision | Recall | F1 |
|---|---:|---:|---:|---:|---:|---:|
| Walk (walking is positive) | 28 | 2 | 8 | 0.933333 | 0.777778 | 0.848485 |
| Run (running/jogging are positive) | 17 | 0 | 55 | 1.000000 | 0.236111 | 0.382022 |
| Either gait | 47 | 0 | 61 | 1.000000 | 0.435185 | 0.606452 |

Supported walk/run macro-F1 is 0.615254; unknown rate across all six actions is 78.241%. Arm actions are intentionally unsupported by these semantic gates. This is not six-class macro-F1, calibrated confidence or an accuracy comparison against the same 863 pose intervals. Zero false positives from this finite sample does not guarantee zero future false positives.

Cross-scenario retrieval from normalized 80-score representations uses the same 216 clips, known subject, three same-action positives per query, deterministic cosine ranking, and no tuned weighted fusion:

| Representation | Precision@1 | Recall@3 | Recall@5 | MRR |
|---|---:|---:|---:|---:|
| Forward video | 0.648148 | 0.584877 | 0.774691 | 0.782099 |
| Reversed video control | 0.666667 | 0.567901 | 0.768519 | 0.799118 |
| Repeated midpoint control | 0.310185 | 0.266975 | 0.382716 | 0.518621 |
| Positive forward-minus-midpoint residual | 0.638889 | 0.563272 | 0.759259 | 0.779442 |

Temporal input helps this action proxy; reversal's strong result also shows it does not establish motion direction understanding. The repeated midpoint is a control through the same video model, not an independently trained image-embedding model. A semantic reranker is not promoted from these results alone: application candidate admission, identity errors, alignment and actual editing relevance still require independent validation.

## Evidence, scope and remaining gates

Raw local results: `build/goal-kth-{before,after}-holdout-evaluation/summary.json`, `build/goal-kth-ava-holdout-evaluation.json`, observation and download manifests, and execution assets under `build/goal-kth-*`. Tools and frozen protocols are tracked; dataset media and generated model outputs are not distributed. See the benchmark README for replay commands.

The added production run label consumes already available, associated continuous torso measurements after matching; it does not change candidate admission or discard unknown pairs. Its score remains evidence strength. It refines geometric step evidence and preserves simultaneous independent arm/head facets. Missing model, ambiguous gait, stale model identity, wrong source, discontinuity and head-only scope retain geometric/unknown behavior. This limited extension is justified by the frozen run control; general running recognition and confidence calibration remain open.

No end-to-end speed, peak RAM or VRAM improvement is inferred from extraction timings collected during diagnostics. Cold/warm full-application measurements on a fixed original-video corpus, cinematic positive/negative judgements, confidence calibration on separate data, and clean-machine release certification remain required.

An additional five alternating full smoke runs on the same four development KTH videos (2035 source frames, fast/6 FPS, CUDA, four CPU threads, fresh application caches and separate processes) measured cold analysis medians **5324 → 5711 ms**, process wall **6179 → 6563 ms**, and live-sampled peak working set **1,307,635,712 → 1,619,562,496 bytes**. Warm analysis medians were **21 → 74 ms**, with all four observation/window cache hits and zero main-pipeline pose samples. Cache capacity was the benchmark's default 8 GiB; actual footprint must not be confused with that capacity. Raw contracts and 20 run records are under `build/goal-pipeline-ab-02`.

This is a measured cost increase, not a whole-application speedup: the original extracted virtually no people, while the correction executes identity/tracking work and carries valid observations. Both versions returned zero final matches under normal identity admission on this small low-resolution corpus, so these measurements do not exercise nonempty result export or establish cinematic quality. A separate native all-people smoke adapter now exercises the exact UI branch; the first diagnostic-only attempt incorrectly used an empty single-person admission mask, was diagnosed in the debugger and corrected. Its failed output is retained and is not counted as a successful run. [Real-series validation](chernobyl-cache-validation.md) proceeds separately with the user-supplied footage.
