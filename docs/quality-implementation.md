# Quality implementation work log

This work follows the existing Qt/QML desktop architecture. It is not a release
checklist with presumed successes. Only completed checks are recorded below.

## Baseline and dependency plan

- Starting revision: `e590b27` (RC.23.2); clean `main` checkout.
- Working branch: `codex/quality-foundations`.
- Release build: existing UCRT64 Release configuration builds successfully.
- Baseline CTest: running; results will be recorded after completion.
- Identity scaling control: 300 summaries, 2.585 ms, 60 selected observations.
  This is synthetic timing, not recognition accuracy.

Implementation order grounded in the current code:

1. Extend `DominantPerson`'s existing conflict-aware clustering to expose every
   identity component, keeping the historical dominant API compatible.
2. Preserve structured candidates and all usable target windows in the versioned
   `MotionWindowCache`, then connect `AnalysisController` and `SourcesRail` to
   explicit selection, cached reselection and conservative batch suggestions.
3. Extend `MovementClassifier`/`MotionRanker` using existing evidence rather than
   replacing their tested behavior. Separate result families and export identity.
4. Repair Unicode conversions in runtime probing and analysis/cache paths.
5. Extend `ProviderStore`/`ModelStore` with persistent verified package storage
   and staging validation; retain the existing provider until validation succeeds.
6. Reuse `Theme` motion tokens and compact `Pf*` controls. Add translations via
   the existing localization catalog. Verify state transitions and headless UI.
7. Run existing real-fixture regressions and the full suite; document measured
   results and remaining work. Tag/package only a release-ready implementation.

## Architectural constraints

The existing matcher already performs indexed retrieval, identity checks,
banded DTW, camera checks and recovery. Existing measured fixtures must survive.
Current motion-v43 cache stores only the automatically admitted lead. It cannot
support selecting a different person: invalidate that representation when
introducing all-character artifacts; never pretend that an old cache is complete.
Track IDs and candidate IDs are source/artifact-local, not cross-episode identity.
Uncertain cross-source suggestions must require confirmation.
