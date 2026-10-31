# Strong target carryover, minority presence and process restart

This control exercises the production `AnalysisController` with real person,
pose, face and ReID models. Each phase runs in a fresh process and temporary Qt
settings profile. Only the deliberately selected observation cache survives.
It therefore tests restart persistence rather than in-memory singleton reuse.

## Fixture and interpretation

`prepare_real_series_fixture.py` reads the supplied Kyle and Butcher videos and
creates disposable 1280x720, 12 fps media in a new output directory. Originals are
never changed. Eight initial files contain the same real 3-second Kyle excerpt.
The two additions are a copy that sorts before the saved anchor and an episode
containing 34.5 seconds of Butcher footage followed by 3 seconds of Kyle (8%).
The manifest retains source/generated SHA-256 hashes and exact source ranges.
Media, model outputs and caches remain ignored local artifacts.

These repeated excerpts intentionally provide strong independent face evidence
for a controlled identity/persistence test. They do not establish general
cross-costume, makeup, lighting, occlusion or cross-episode identity accuracy.
Identical footage is excluded from match results; zero result pairs in this
fixture is expected and is not a retrieval recall measurement.

## Verified run

Local evidence: `build/series-eight-percent-verified/manifest.json`, per-phase
`.json`, `.txt` and `.diagnostics.log` files, and `summary.json`.

| Fresh process | Sources | Observation cache hits | Pose sample frames | Detected poses | Automatic selections |
| --- | ---: | ---: | ---: | ---: | ---: |
| Seed, explicitly select anchor | 8 | 0 | 144 | 200 | 7 |
| Restart, add two sources | 10 | 8 | 243 | 603 | 9 |
| Restart, repeat all sources | 10 | 10 | 0 | 0 | 9 |

Every old source in the incremental run reports `decoded=0`, `view_samples=0`,
`pose_samples=0`, and a complete observation cache hit. New sources are marked
as misses. The filename sorting before the anchor still receives the correct
automatic target: the saved explicit prototype is loaded before suggestions.

In the 37.5-second minority episode, the selected candidate is ID 78, has
3.000 seconds of observed presence, and is selected automatically with face
similarity 1.000 and margin 0.742. The candidate ID is artifact-local and is
not hardcoded as the identity of Kyle. Its observed duration and source segment
establish that the short target was selected instead of the dominant source
character. The controlled repeated image explains the perfect face similarity;
it is not an accuracy probability.

`realSeriesIncrementalRestart` checks per-source cache status, exact counts of
sources, automatic/manual selection state and minority presence, in addition to
global zero-inference warm counts. Normal synthetic selection/reselection,
manual overrides, weak-evidence confirmation and cancellation still pass in
`series-normal-workflow-regression.log` (2.09 s).

## Reproduction

Create a **new** fixture directory, then run `run_real_series_restart.py` with
the test executable, generated manifest, pose model, runtime DLL, model directory,
Qt platform plugin directory and Qt DLL directory. Both utilities expose these
as explicit command-line arguments. The runner launches five separate processes (including manual override/restart),
removes unrelated diagnostic overrides, captures native UTF-8 diagnostics, and
refuses an existing cache/evidence directory. It never deletes an old run to
manufacture a cold result.

Source summaries now expose `observationCacheHit` (observation reuse, independent
of optional view decoding) and `targetPresenceSeconds`. Target diagnostics name
the source as well as ID, reason, margin, track count and observed presence.
Cached-source diagnostics explicitly encode UTF-8 instead of the locale-dependent
`qPrintable` conversion, preserving Cyrillic/Japanese filenames in raw logs.

## Separate full-video regression

`family-evidence-kyle-verified-results.json` exercises the original complete Kyle
montage through the app's explicit-selection smoke path: four result pairs in
3,695 ms on a warm run. All pre-existing result values agree with the previous
v9 report except temporary preview session paths; new family evidence is additive.
An earlier attempted offscreen run timed out before analysis because its command
omitted the SDK platform plugin path; it is not counted as a passing application
run. The corrected invocation explicitly supplies that plugin path.

## Real manual override and restart

`build/series-manual-verified` repeats the three phases above with a fresh cache,
then adds two more fresh processes. The manual phase opens the minority source,
explicitly selects its prior Kyle candidate, then uses the normal reselection API
to choose the longest-lived competing candidate. That candidate is ID 2 with
33.083 seconds of observed presence, replacing Kyle's ID 78 / 3.000 seconds.
The next process preserves ID 2 and reports `targetAutomatic=false`.

Both manual phases reuse the complete observation artifact and report zero
primary pose sample frames / detections. Changing the target first needs 107
head-review requests and 12 action-review requests whose data did not previously
exist. These are downstream evidence measurements, not a claim of zero model
work. On the following restart all 107 head and 12 action requests are cache
hits with zero decode, face or head-orientation inference. The manual phase takes
14.741 s; the cached restart takes 2.201 s in this controlled run.

A normal offline workflow check failed twice after a mode change, unexpectedly
reporting six primary pose samples instead of zero (`series-manual-normal-failed.txt`
and `series-normal-ctest-failure-1.txt`). Its cause is not yet established. Ten
focused diagnostic runs, ten cache-instrumented CTest runs and ten uninstrumented
CTest runs then passed; these repeats do not establish a fix. `PF_DEBUG_CACHE=1`
now records observation key digest, entry presence, validation outcome, configured
cache path, scene settings and UTF-8 source. The failure remains an explicit cache
reliability investigation, separate from the passing real fixture controls.

The final Release build and all 13 CTest targets pass in 117.71 s
(`series-manual-final-suite.log`), including 56 passing UI checks. The intermittent
cache failure above remains under investigation despite this successful run.

Remaining: intermittent offline mode-cache miss, difficult/ambiguous real series,
broader labelled identity and retrieval metrics, and the other open specification
gates. This evidence does not declare the entire specification complete.
