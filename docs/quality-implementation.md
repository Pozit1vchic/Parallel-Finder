# Quality implementation work log

This work follows the existing Qt/QML desktop architecture. It is not a release
checklist with presumed successes. Only completed checks are recorded below.

For the subsequent Kyle identity-switch correction, distinct-person confirmation,
clean themed highlights, scoped head evidence and reviewed association controls,
see the [RC.23.3 follow-up](quality-kyle-followup.md).

The full original scope remains active. The requirement-by-requirement
[completion audit and work queue](quality-requirements.md) distinguishes
implemented controls from missing or unverified end-to-end requirements.

## Baseline and dependency plan

- Starting revision: `e590b27` (RC.23.2); clean `main` checkout.
- Working branch: `codex/quality-foundations`.
- Release build: existing UCRT64 Release configuration builds successfully.
- Baseline CTest: all 10 targets passed, 121.59 seconds.
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

## Implemented workflow

The central stage now presents an explicit character selector, with actual
source appearances and a bounding box marking the relevant person. It is not
inside the source rail. The source rail retains the reselection action.
Candidates come from the existing conflict-aware identity graph; the GUI no
longer reduces observations to a dominant winner before target selection.
Only conservative, face-supported batch suggestions bypass confirmation.
Manual choices remain authoritative, including when a new source sorts before
an already analysed source. Automatic choices do not become persistent anchors.

The checksummed PFCH v2 artifact contains candidates, their track membership,
and all usable motion/static windows. Its key includes source/model/settings,
source generation and the all-character representation version. Reselection
and mode changes read that artifact without repeating pose inference. Cached
character previews are independent of a temporary session and belong to the
source reset namespace. Preview rendering sorts requests by timestamp, reuses
forward decoding and the established bounded encoder, and batches cache writes.

Structured scene descriptors retain posture evidence, normalized visible pose,
continuous measured head rotation, image velocity, identity/visibility/temporal
quality and optional context. Missing camera or signed body-orientation evidence
stays unknown. Similarity is separate from evidence quality. The results UI
separates Strong/Possible results, explains the evidence, and adds best-match,
continuity, pose, identity, evidence, duration and source sorting while preserving
the historical sort indices and the application's components/motion settings.

Scene families use identity, shot/context, pose and local temporal agreement.
Complete-link admission prevents long overlap chains. Distant copied edits
require the existing strict pixel-sequence verifier, extended to require 80%
coverage of both sampled ranges. Similar backgrounds are insufficient. Ranking
also records a bounded diversity penalty; physical clip/montage export reuses
canonical cuts instead of producing a second file for an identical canonical
interval. JSON/CSV exports retain the organization components.

Provider installers keep verified archives and partial chunks across retries,
bound manifest-part downloads to four connections, validate HTTP resume metadata,
retry transient failures, and expose bytes/speed/cancellation. Hash verification
and isolated runtime probing precede activation; a failed probe preserves the
active provider. Archive extraction and runtime/model paths use Unicode-safe
APIs. The installation tests exercise Cyrillic/Japanese paths and offline retry.

## Measurements and scope

- A baseline worktree at `e590b27` builds the original matcher and application.
  The identical replay harness can run against either revision.
- The existing eleven reviewed movement cases classify correctly both before
  and after (11/11). This is a regression control, not population accuracy.
- On the supplied 778-second, 3840x1632 montage, the original pipeline with
  pose, OSNet and SFace produced 2,037 diagnostic windows and 125 result pairs.
  Replaying those exact observations produced 125 pairs before and after;
  matcher time was 5,982/5,985 ms in one concurrent replay, with an additional
  5.2 ms for organization. These timings do not establish a speed improvement.
- No pair met strict whole-range copied-footage proof in that sample. Manually
  inspected similar camera views around 129/189 seconds and 312/317 seconds
  show different head positions; they must not be deleted just for sharing a
  background. There is no manually labelled duplicate-recall estimate yet.
- The new all-character pre-analysis retained 385 candidates across 504 detected
  scenes. The first cluster covers approximately 707 seconds. Explicit selection
  is exercised with `PF_SMOKE_TARGET_ID`; `inspect` writes the candidate artifact
  and cancels at selection. This opt-in diagnostic never changes normal GUI
  selection policy. It can force an explicit choice over a saved diagnostic choice.
- Selecting the first, visually verified Butcher cluster completed with 118 pairs:
  40 Strong and 78 Possible. This is a different identity-selection policy from
  the baseline's dominant-person path, not evidence that seven removed pairs
  were false positives. No pose inference was repeated for this selection run.
- For the same 1,594 representative requests, the original per-appearance seek
  preparation took approximately 152 seconds. Sorted decoding with batched
  cache writes took 37,512 ms on an isolated copy of the analysis artifacts;
  a subsequent preparation took 2,880 ms with 1,594/1,594 preview cache hits.
  Source decode, view sampling and pose sampling were all zero on that repeat.
  Concurrent test workloads differ, so these are observed local timings, not
  a general throughput guarantee.
- The initial diagnostic without identity models produced no matches. It is
  retained as a configuration failure control, not used as a quality baseline.
- Head/action-review assets differ between the original executable directory
  and the development executable. Their full end-to-end timings are therefore
  not an apples-to-apples matcher comparison; use the frozen-window replay.

## Validation and release limits

The full suite passed all 11 targets after the central selection, family and
provider changes (129.40 seconds). After the preview batching and metric-test
registration, the final full run passed all 12 targets (124.86 seconds).
Separate focused checks cover actual ONNX
selection/reselection/cancellation, cache corruption/cancellation, canonical
export reuse, complete-link families, copied-range coverage, bounded downloads,
resume validation, retry and cancellation. Three Python metric checks pass;
the preview batch test verifies both the full batch and its tail are persisted
and that resetting the source invalidates those previews;
the localization catalog validator covers all four additional offline languages.

This branch is unreleased. A successful build is not a claim that the complete
quality specification has been empirically validated. In particular:

- Monocular 2D pose does not establish signed body yaw or reliably distinguish
  every sitting/crouching/kneeling case. Those dimensions preserve ambiguity;
  head yaw is never substituted for body yaw.
- Real retrieval Recall@K/Precision@K/MRR and family precision/recall need
  independently reviewed judgements. The evaluator refuses to treat unreviewed
  candidates as negative ground truth.
- Large public provider downloads and recovery on every GPU/runtime combination
  are opt-in integration checks. Local synthetic transport tests establish
  lifecycle/integrity properties, not external network throughput.
- No release tag, installer publication, or unmeasured quality-improvement claim
  has been made. The actual source video and biometric diagnostic embeddings
  remain local and are not committed.
