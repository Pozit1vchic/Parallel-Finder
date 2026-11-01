# Labelled quality reports

`pf_matcher_probe FIXTURE.json --labelled-actions` replays the repository's
reviewed `scenepack-movement-grouping.json`. It preserves the original eleven
labels and treats fixture end timestamps as the final included observation,
using the recorded cadence for the exclusive boundary. It does not synthesize
new poses. Run `evaluate_quality.py REPORT.json` for accuracy and a confusion
matrix. These eleven reviewed clips are a regression sample, not population
accuracy or a body-orientation validation set.

Retrieval reports can be evaluated independently of a decoder/provider:

```json
{
  "schema": 1,
  "scope": "Manually reviewed source and sampling configuration",
  "queries": [{
    "id": "source-A:12.0-14.0",
    "positiveIds": ["source-B:30.0-32.0"],
    "negativeIds": ["source-C:40.0-42.0"],
    "rankedIds": ["source-B:30.0-32.0", "source-C:40.0-42.0"],
    "tags": ["hard-positive", "back-view", "appearance-change"]
  }]
}
```

The example illustrates the schema; it is not a measured benchmark result.
Stable candidate IDs should include source fingerprint, selected identity,
canonical interval, and fixture version. Preserve judgements independently
from predicted rankings. Never label an unreviewed candidate as a negative.
Unjudged retrievals make Precision@K unavailable. Missing relevant candidates
still lower Recall@K and MRR. Duplicate ranked IDs and contradictory labels
are errors rather than silently repaired data.

Use tags for positive/hard-positive, negative/hard-negative, same/different
person and motion, occlusion, back-view, clothing-change, camera-movement,
partial-body, head-only, static-pose, tempo-change, duplicates, orientation,
and temporal-grouping cases. Add labelled family membership and posture/yaw
observations only after actual review; synthetic family tests are not a real
family precision/recall estimate. Do not publish source paths or biometric
embeddings from local diagnostic caches.

Keep timing comparisons on the same machine, model, settings and warm/cold
state. The replay clock measures classification alone, without decoding,
identity inference or scene-family organization. Report these stages separately.

## Minimal recorded retrieval baseline, 2026-11-01

`matcher-retrieval-baseline-v1.json` freezes the existing reviewed positive
pair of Soldier Boy walking toward the camera in Gen V and The Boys S03. The
two inputs are recorded pose, face and body observations, not generated poses;
the annotations were already used during matcher development. Their fixture
records a prior 5 FPS original-video review, but the original videos, review
contact sheets and their source hashes are not currently available here. The
labels therefore cannot be independently rechecked on this machine yet.

Run with the freshly built probe on PATH configured for its runtime DLLs:

```
python tools/benchmark/run_matcher_labelled_baseline.py --probe build/ucrt64-release/tools/pf_matcher_probe.exe --output build/matcher-retrieval-baseline-v1.json
```

The script pins the input fixture hash, runs actual `--all` retrieval with
the probe's fast-profile defaults, maps its returned ranking to the two
previously judged query directions, and records the probe/reference hashes.
On the current machine it returned the single pair in both directions:
Recall@1 = 1, Precision@1 = 1, MRR = 1. These are **two directions of one
positive pair**, with no alternatives or negative cases; they cannot measure
discrimination, generalization, or release quality. P@5 and P@10 are omitted
because each query has only one candidate. Model observations are replayed;
decoding, inference, target selection, full result organization and video
coverage are not measured. The run's probe SHA-256 was
`2352b712ff5da02e334011a46320ec72b734c2a66a5ba5beff2ecbd68b130f45`;
rebuilds will change it.

The existing `matcher-quality-v1.json` and `dean-reference.json` retain
additional positive/negative annotations, but the associated original videos,
chronological review sheets and full window reports are missing on this
machine. Their short or policy-excluded examples must not be silently counted
as complete retrieval queries. A meaningful next baseline needs independently
reviewed candidate sets with positives and hard negatives per query, source
hashes and exact temporal boundaries, full rankings, and a separate holdout
covering view changes, occlusion, clothing changes and unrelated people.

## Explicit target / repeated-edit diagnostics

Set `PF_MODEL_ROOT` to the directory containing the same identity models for
both executables. Keep `PF_MODEL_PATH`, runtime, provider, quality profile and
source identical. Executable-local head/action assets must also match for an
end-to-end timing comparison; otherwise use the frozen-window matcher replay.

With `--pf-analysis-smoke`, setting `PF_SMOKE_TARGET_ID=inspect` enters the real
all-character GUI backend, writes candidate metadata/previews to
`PF_SMOKE_TARGET_CANDIDATES_JSON`, and deliberately cancels at selection. Its
exit status is 1 because a complete match analysis has not run; inspection is
not a successful empty match report. Read the artifact and the
`PF_CHARACTER_PREVIEWS`/`complete_cache_hit` counters. Set a reviewed numeric
`PF_SMOKE_TARGET_ID` to perform a complete explicit-target run. An explicit ID
forces confirmation over a previously saved diagnostic choice. These controls
are opt-in and are ignored outside the analysis-smoke entry point.

`pf_matcher_probe WINDOWS.json --all PARAMS.json` replays retrieval and ranking.
Set `organizeResults: true` in a copy of PARAMS.json to include new result
organization; the report separates its time from matcher time. The original
baseline harness omits organization. `--copied-scenes` reports strict sampled
whole-range copy candidates. `--scene-neighbours` only helps find visually
similar plans for manual review: its pixel difference is not duplicate proof.
Neither mode fabricates identity labels or judged negatives.
