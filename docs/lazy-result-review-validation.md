# Deferred result-review model construction

The head-orientation and video-action reviewers previously constructed inference
sessions before checking whether all requested observations were already cached.
They now create each estimator on the first usable cache miss that needs it.
The existing model fingerprints, provider keys, inference inputs and cached
observations remain unchanged. Debug diagnostics count successful estimator
constructions separately from decoded frames and cache hits.

## Native full-episode evidence

Artifacts: `build/goal-chernobyl-lazy-review-01`, including executable SHA-256,
source paths, settings and shared isolated cache roots in `contract.json`.
These are three separate native application processes, analyzing the same full
S01E01/E02 episodes in combined, all-people mode. No original video or user
configuration/cache was changed. Results were compared with the completed cold
analysis in `build/goal-chernobyl-after-native-cold-01/cold.json`.

| Run | Full analysis ms | CPU ms | Peak working set bytes | Results | Changed records |
| --- | ---: | ---: | ---: | ---: | ---: |
| warm-01 | 12531 | 18000.0 | 936267776 | 188 | 0 |
| warm-02 | 11840 | 18343.8 | 937455616 | 188 | 0 |
| warm-03 | 11143 | 16687.5 | 936472576 | 188 | 0 |

Median full analysis: **11840 ms**. Equality excludes only temporary
`leftPreview`/`rightPreview` URLs; every other result field is compared in order.
All runs hit the persistent raw Matcher cache and report zero main-analysis
decoded frames, analyzed frames, pose detections and cache-write failures.
Each head review requests/hits 3936 observations (3273 observed), with zero
decoded frames, face/orientation inference time and model constructions.
Each action review requests/hits 95 observations (90 observed), with zero
decoded frames and model constructions. All 376 previews are cached.

The preceding raw-cache implementation, before this deferred construction
change, measured 10972/9780/8602 ms (median 9780 ms) in
`build/goal-chernobyl-raw-matcher-cache-02/summary.json`. These small sequential
samples do **not** establish an end-to-end speed improvement for deferred
construction. They establish the absence of unnecessary review sessions and
unchanged output. The 9780 ms result belongs to the preceding variant and must
not be attributed to this variant.

## Regression checks and limits

- Full release build and CTest: **15/15 passed**, 146.44 seconds;
  `build/goal-lazy-review-full-build.log` and `build/goal-lazy-review-ctest.log`.
- The opt-in `cachedObservationsDoNotInitializeInferenceModels` test was also
  executed with the audited head model. It seeds positive cached rotations and
  supplies a deliberately unloadable detector, then verifies successful cache
  reuse, retained observations, zero decode and zero estimator constructions.
  Evidence: `build/goal-lazy-head-cache-test.txt`. Without the supplied audited
  model, that integration case explicitly skips.
- Zero review constructions does not mean every model in the entire application
  is deferred. Initial primary-analysis model setup is a separate concern.
- Exact output preservation is a cache regression check, not independent
  evidence of semantic search quality. The previously reported held-out misses
  and remaining global cache-budget work remain unresolved.
