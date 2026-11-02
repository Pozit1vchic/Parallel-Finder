# Quality specification: completion audit and remaining work

Goal: finish the **whole** supplied specification, not just the character picker.
This document is a work queue, not a completion certificate. An active goal stays
active until the missing and unverified items below have authoritative evidence.

Current release decision: the user requested a quicker release with known
limitations rather than waiting for ideal recognition. Local package
`0.2.0-d2f9492d` was built and checked; it has not been published. Its notes
must disclose that repeated appearances can remain separate and different
people can still be confused. The broader specification remains unfinished.
The two women shown in the 2026-11-01 screenshots are different people;
visual similarity is not ground truth for merging them.

Source: the user attachment `Вставленный текст.txt`, SHA-256
`cc205bdefaa244e56e56b00961d681468367e20c7248fdb792250c5f9c621da2`.
Initial audited revision: `877500e`, following `ede833d` and the preceding quality
commits. User follow-ups additionally require a centered, visible selector,
unique people, visible attractive marking, and testing the supplied Kyle/Butcher
videos. The previous turn made progress: it committed functioning selection
corrections and structured filters and produced passing real-source evidence.

Status meanings: **evidenced** means the named limited behavior has direct
evidence; **partial** means some required behavior exists but the whole clause
is not proven; **missing** identifies an inspected implementation gap;
**unverified** means further inspection/execution is required. A passing unit
test does not establish general recognition accuracy or release readiness.

## Requirements and evidence

| ID | Original requirement(s) | Current evidence / next verification | Status |
| --- | --- | --- | --- |
| R01 | Existing architecture/style; closest components, density, fonts, colors | Existing Pf controls/theme reused; actual Kyle screenshots inspected. Audit every new state, not only the picker. | Partial |
| R02 | Preserve working behavior and interfaces; no duplicate stack | Existing tracker, clustering, MotionWindows and matcher retained; regression suite green at `877500e`. New changes require checks. | Evidenced, limited to suite |
| R03 | Logical commits, docs, changelog throughout | Git history and `quality-changes-unreleased.md`. Keep this invariant throughout remaining work. | Evidenced |
| R04 | Background work cancellable, no UI-thread expensive work | Worker analysis, cache cancellation and provider tests exist. Audit family/continuity inner loops and every new long operation. | Partial |
| R05 | Windows Unicode throughout | PathEncoding, native extraction and tested Cyrillic/Japanese paths; native-separator reselection fixed. Audit remaining probe/config/log boundaries. | Partial |
| A01 | Pre-analysis is normal workflow, not low-confidence fallback | GUI waits for explicit target; full-model real-video and synthetic selection tests. Diagnostic smoke is explicitly opt-in. | Evidenced |
| A02 | Multiple significant identity clusters; brief target retained | `characterCandidates`, minority regression, unresolved appearances retained. Recognition coverage beyond reviewed montage remains unverified. | Partial |
| A03 | Structured candidate ID/tracks/body/face/presence/observations/evidence/previews/windows | `CharacterCandidate`, PFCH metadata + all-character MotionWindows, serialization tests. IDs are artifact-local. | Evidenced |
| A04 | Inspect multiple appearances, select one, continue, change later | `CharacterSelector`, `CharacterAppearance`, workflow tests and real screenshots. | Evidenced |
| A05 | Cached target rebuild/reselection, no needless inference | Real reselection/restart reuse observations and downstream head/action caches. Two normal offline mode-change checks unexpectedly missed the cache; subsequent stress checks pass but cause remains open (series-restart-evidence.md). | Partial |
| A06 | Batch strong automatic carryover; weak/ambiguous confirmation | Strong-face real-media control now carries one explicit choice across eight files and two additions in fresh processes; newly earlier-sorting source also works. Synthetic weak-evidence confirmation exists; difficult real series remains to review. | Partial |
| A07 | Manual override and target non-dominant, e.g. 8% | Real-media 37.5-second control selects the 3-second target (8%) over competing identities, with full models and cached restart. Real manual reselection changes the minority source from ID 78 / 3 s to competing ID 2 / 33.083 s and preserves ID 2 after a fresh-process restart. Broader real manual series remains to exercise. | Partial |
| A08 | Identity diagnostics and unique identities | Selection reason/margin logs, coherent tracks, single-face ownership and relative distinctness. Twenty-two reviewed Kyle pairs include the user's two different women; three same-person pairs still split. Six further reviewed back-view/occlusion controls add four missed same-person pairs and two correctly separated different-person pairs. Previous Butcher false-merge labels were corrected after closer review; see `identity-v020-controls.md`. These small overlapping sets cannot estimate general error rates. | Partial |
| A09 | Export the complete parallel scene without excessive unrelated context | Short detected shots up to 12 seconds are exported in full; long shots receive up to 1.25 seconds of context per side without truncating the match. Automatic accurate export refines sampled shot boundaries; a conflicting end refinement cannot cut off supported match frames (scene-export-match-boundaries.md). An encoded two-shot control and a real Kyle CutService export check frame coverage. Fast export and undetected/soft cuts are not covered by this refinement. Seven detected cuts in the first 10 seconds of Kyle and 12 native-detector cuts in seconds 10–30 were visually checked; the latter interval's sampled grid revealed no other cuts at 0.25 s resolution. Application sampling, full UI workflow and general accuracy remain unverified (kyle-cut-review-10-30.md). | Partial |
| B01 | Identity/temporal/source/scene/sequence/cluster descriptors | `SceneDescriptor` has most fields; sequenceId currently not established by real sequence detection. | Partial |
| B02 | Continuous body yaw/pitch/roll and eight useful directions | Optional pinned local model runs before family organization with temporal/flip/crop checks and separate persistence. Explain Match now shows eight view labels plus continuous angles/support/unknowns in six languages, with short-window/reduced-motion checks. Same-frame all-17-output replay improves difficult temporal controls from 4/12 to 6/12 coarse-correct samples on CPU/CUDA, zero supported coarse errors; six back-view misses remain. Native single-frame controls support 9/11. Independent body/head view filters and unsupported/removed-side guards now work (orientation-dimension-filters.md). Dedicated dimension sorts, broader recovery, unsupported pitch and packaging remain open; see body-orientation-investigation.md. | Partial |
| B03 | Independent continuous head yaw/pitch/roll | Measured rotation, validity checks, head/body-independent tests and SO(3) similarity. Vertical head directions now retain rotation/pitch without invented yaw/roll; independent head-view filtering uses measured descriptors. Need independently reviewed real orientation cases. | Partial |
| B04 | Standing/sitting/lying; crouching/kneeling when reliable; ambiguous/unknown | Validated action observations now refine sitting/lying with temporal/frozen class margins. Conflict, wrong source/range and scene-boundary tests pass; two real reviewed controls pass. No real lying control yet; no fabricated kneeling. | Partial |
| B05 | Motion semantics, direction, velocity/trajectory/temporal evidence | MovementClassification now excludes unreliable wrist/shoulder/ankle endpoints and unknown intermediate observations from gesture excursions. Three reviewed Butcher windows cover invisible wrists, a raised bottle, and a lowered arm; 51 focused classifier/ranker/grouping tests pass, with one optional benchmark skipped. Broader real quality, acceleration/periodicity and camera contamination remain open. | Partial |
| B06 | Visibility, occlusion, missing-data/identity/pose/temporal quality | Body/head scoped quality and degeneracy tests. Confidence remains evidence strength, not calibrated probability. | Evidenced, limited |
| B07 | Camera motion/scale/view/context when reliable | Existing scene sequences/context; descriptor camera influence remains unknown. Need integrate actual available camera evidence or document measured insufficiency per observation. | Partial |
| B08 | Composable descriptors, ambiguity, filter/sort individual dimensions | Structured scope/posture and independent eight-view body/head filters exist, with unknowns, removed sides, export membership and six-language selection persistence tested. Separate body/head orientation sorts use both retained sides and keep unknowns last. Pose and continuity sorting now also place missing measurements after measured zero, with a focused UI regression. Motion-dimensional ordering and orientation recognition quality remain incomplete. | Partial |
| C01 | Explicit broad retrieval -> geometry -> identity -> alignment -> context | Existing indexed matcher has cheap gates and expensive compare paths. Verify real stage counters and a readable architecture map before declaring complete. | Unverified |
| C02 | Preserve independent identity/pose/motion/alignment/head/action/camera/visibility components | Several fields exist, but complete truthful component exposure and missing-data semantics need audit. | Partial |
| C03 | Similarity separate from quality; dedicated ranking | Scoped evidence/best score, tests and exports; review consistency after independent head/action enrichment. | Partial |
| C04 | Strong list + difficult true-match recovery | Strong/Possible UI exists. One independently reviewed Butcher phone-pose pair previously missed is now recovered in frozen replay and the real application while a reviewed glasses transition is rejected; see `butcher-retrieval-controls.md`. Empty endpoint poses cannot inflate the temporal span of supported observations, with positive and negative regression controls; 141 matcher/pose-support/family tests pass on the combined build. Broader recall and false-positive rates for the enumerated hard cases remain unproven. | Partial |
| C05 | Explain Match without internal noise | Existing localized explanation; must audit all component values against actual measurements. | Partial |
| C06 | Investigate local rejected/reviewed signals; retain reasons, no neural training | Existing review/hide actions exist; durable structured rejection reasons have not been established. | Unverified |
| D01 | Separate classification/grouping/dedup/ranking/continuity | `SceneFamily`, `ResultOrganization`, existing classification. | Evidenced |
| D02 | Actual scene/sequence/time soft locality; no fixed ten-minute bins | Scene boundaries/time-based continuity costs exist; actual sequence clusters not populated. | Partial |
| D03 | Multi-signal overlapping + adjacent + true copied-event families | Complete-link and strict both-range pixel proof; retained measured admission reasons and unique members. Mixed head/body comparisons now require shared measured head orientation or independent copied-frame proof; 13 family tests pass. Full trajectory and real mixed-scope quality remain to review; see scene-family-evidence.md. | Partial |
| D04 | Dedicated representative quality, completeness/occlusion/camera | Scoped evidence/identity/temporal/match terms retained per member after head/action review; representative changes reuse measured admission proof. Full completeness and measured camera terms remain unverified. | Partial |
| D05 | Canonical results before export + stable defensive export identity | Family pair dedup, fingerprints, shared JSON evidence and canonical own-shot bounds verified. Real cutter/decoder test proves four requests -> three physical outputs with Unicode sources. Real duplicate-positive neural quality fixture remains open. | Partial |
| D06 | All requested practical sorts, smart classification groups | Specific posture+shared-action facets, conflicts after supported classes, paired continuity inside default groups and visible-order export are implemented. Existing evidence tiers now precede continuity within a class, with conflict/filtered-view controls. See automatic-classification-order.md. Broader labelled action/sort quality and the continuous quality/continuity tradeoff remain to verify. | Partial |
| D07 | Bounded diversity suppresses top redundancy | Existing bounded family-use penalty and organization tests; real labelled diversity ordering not evaluated. | Partial |
| D08 | Continuous local pose/head/body transition cost, deterministic practical ordering | Greedy continuity with time/scene/pose/head/roll and optionally measured continuous body yaw. A nonnegative time lower bound skips candidates that cannot improve the current choice; a 1,000-pair synthetic replay preserves the ordering and accelerates this isolated step (continuity-order-timing.md). Actual sequence evidence and real-workload timing remain open. | Partial |
| E01 | Profile decode/detection/pose/face/ReID/track/cluster/windows/match/classification/previews/cache | Real stage logs exist; explicit clustering/classification timing coverage needs audit. | Partial |
| E02 | Persistent source/model/settings/version compatible observations/artifacts | PFCH + window/scene/head/action caches, atomic writes/checksum tests. Audit exact observation-settings vs matcher-settings separation. | Partial |
| E03 | Change target/matcher/classification/order without full inference | Target/mode/order reuse proven; 8+2 source addition and restart are now proven; changing every matcher setting still needs end-to-end proof. | Partial |
| E04 | 8 cached + 2 new infer only new | Three fresh-process real-media controls: seed 8 misses/144 pose frames; add 2 -> 8 hits/243 new pose frames; repeat 10 -> 10 hits/zero inference. Per-source assertions and raw UTF-8 logs retained in series-eight-percent-verified. | Evidenced, controlled fixture |
| E05 | Adaptive inference and GPU profiling with measured quality/speed | Existing periodic ReID/usable-face sampling, pipeline counters and preview benchmark. Broader adaptive quality/transfer validation remains open. | Partial |
| E06 | Atomic cancellation cleanup, incomplete artifacts never valid | Character/window/cache write tests exist; retained independently verified data after cancellation. Windows source-generation keys now normalize path spelling so a reset invalidates late writes using another case or separator; cache regressions pass. Audit remaining stages. | Partial |
| F01 | Persistent resumable Range/chunk downloads, bounded/adaptive concurrency | Persistent chunks and rolling queue now reuse free slots; retry pressure halves new-work concurrency, clean completions recover within a 1–4 caller bound. Semaphore, retry, limits, cancellation/failure/exception tests and controlled scheduling benchmark pass (provider-download-evidence.md). A real TLS server also confirms continuation after a valid short 206 response, bounded to three attempts. Full network adaptation remains to exercise. | Partial |
| F02 | Per-part exponential retry, timeouts, fallback/mirrors where supported | Inspected three attempts with nominal 500/1000 ms backoff, 60 s inactivity and 2 h total guards; manifest fallback exists, package metadata currently has one URL. Per-part retry/cancel controls pass; actual slow/no-Range/mirror workflows remain unproven. | Partial |
| F03 | Separate installer cache, version/size/hash state, verify before complete | ProviderStore persistent verified archives/parts, hash-failure tests. | Evidenced |
| F04 | Offline/restart retry and cancellation preserve valid parts | Local interrupted-transfer/offline/cancel tests. An oversized persistent partial is discarded and retried alone, preserving sibling parts. When a local TLS server ignores Range, a full replacement is now staged separately until verified: wrong length, wrong hash and interrupted body preserve the old prefix; a valid replacement is published and temporary files cleaned. HTTP 416 preserves the prefix and reports the error. Explicit process-restart and automatic recovery from an inconsistent remote range remain open. | Partial |
| F05 | Transactional staging/probe/activation preserves active provider | Interrupted rename state is now discoverable on startup through the preserved previous bundle, without mutation; fresh-process Unicode controls and installer restore-before-failed-probe/offline retry pass. Real GPU process-kill and activation-marker boundaries still need package-level proof. | Partial |
| F06 | Unicode discovery/probe/child args/config/logs | Native paths and Cyrillic/Japanese install controls; audit all remaining conversions. | Partial |
| F07 | Human error + stable code + Retry/Details | Raw installed-provider reasons and unknown raw status fallback are removed from the main message; installed failures retain expandable details, and error/cancel states show a localized Retry label. Six-language validation and focused settings/provider checks pass. Actual offline/error-state rendering still needs verification. | Partial |
| F08 | Stage/bytes/total/speed/cache/retry UI + explicit saved-data message | Progress fields exist; inspect offline/retrying rendered states and localized wording. | Partial |
| G01 | Central motion durations/easing; geometry, not default fade | Theme tokens/geometry transitions exist; raw values remain. Need inventory and coherent stage/source morphs. | Partial |
| G02 | Character selection continuity and source-state morph | Candidate expansion and central container reuse exist; selected thumbnail-to-target continuity unproven. | Partial |
| G03 | Animated result regroup/reorder | Existing displaced/move transitions and bulk guards; verify actual sort changes retain stable delegates. | Partial |
| G04 | SceneFamily collapse/expand relationship | Centered inspector exposes unique members, reasons and bounded playback; side switch/close cleanup, geometry, small window and reduced motion tested. Fixture screenshot inspected; real-source multi-member UI quality remains to exercise. | Partial |
| G05 | Spatial progress-stage and geometry-aware diagnostics/filter expansion | Inspect actual components; avoid replacing labels abruptly and ensure no inaccessible controls. | Unverified |
| G06 | Reduced motion, no animation delays work | Theme reducedMotion and UI checks exist. Verify every newly introduced transition. | Partial |
| H01 | All loading/empty/error/disabled/cache/cancel/ambiguous/offline states | Several states exercised; full state inventory and render/runtime checks outstanding. | Partial |
| H02 | All supported locales, no stray user-facing implementation text | Catalog validation passes six-language additions; audit remaining status/error translation paths. | Partial |
| I01 | Reproducible labelled real benchmark format covering listed hard cases | Real identity controls and existing movement anchors; generic judgement evaluator exists. Eleven coarse body-view controls and frozen actual detections now exist (nine supported, two unresolved); comprehensive retrieval/orientation/family labels remain missing. | Partial |
| I02 | Recall@K, Precision@K, MRR, median rank/false positives/identity/family/confusion where appropriate | Evaluator tests and a reviewed real Butcher retrieval control exist; the two directions of one phone-action pair were missed, with one labelled hard negative. See `butcher-retrieval-controls.md`. This tiny sample cannot estimate overall quality; representative retrieval and family metrics remain missing. | Partial |
| I03 | Controlled before/after quality and performance | Frozen Butcher replay and six Kyle controls documented; classifier refinement needs its own baseline/replay evidence. | Partial |
| J01 | Character tests: all enumerated selection/batch/minority cases | Core and workflow evidence; controlled strong-face and 8% minority E2E now pass (A06/A07); real manual minority override/restart also pass; difficult series and intermittent offline mode-cache misses remain open. | Partial |
| J02 | Classification tests: all views, boundary, posture, head/body, missing/unknown | Helper angular tests are not body-orientation estimator tests. Specific posture working change adds evidence gating checks. | Partial |
| J03 | Dedup/sort/cache/downloader enumerated regression controls | Existing suites cover many clauses. Requirement-to-test name mapping and missing no-Range/slow/restart cases still required. | Partial |
| J04 | UI transitions never leave controls inaccessible | Existing UI suite plus selector/runtime proof; family/source morph states remain untested. | Partial |
| K01 | Why selected/matched/rejected/family/representative/cache invalidated/download reused/probe failed | Selection and matcher reason enums/logs exist. Family admission measurements and representative component scores now retained/shared/exported. Cache invalidation explanations and remaining provider diagnostics still require audit. | Partial |
| L01 | Grounded phases, baseline before changes, no reckless rewrite | Baseline worktree and logs retained; logical commit history. This audit preserves original unfinished scope. | Evidenced |
| M01 | Branch/status/tags/release conventions, no unrelated deletion | Feature branch clean at start; release README inspected. Tag/release state and complete licenses/package audit still required. | Partial |
| M02 | Full suite, benchmark, clean Release build, packaging/resources | Current development build: 13 CTest targets pass in 129.98 s, and startup isolation passes separately using staged dependencies; this is not a new packaged release. Earlier packaged candidate 499a2218 passed 14 targets and fresh installation/upgrade checks but predates the reported identity errors. Full post-fix packaging, clean installation, upgrade, signed update and the broader quality checks remain required; see `v0.2.0-verification.md`. | Partial |
| M03 | Fresh install, previous upgrade, cache migration, provider update | Candidate 499a2218 passed fresh Unicode-path install and upgrade from rc23.2 with a preserved sentinel, on the current Windows machine. Clean-VM, actual cache migration and provider-update workflows remain unverified. See v0.2.0-verification.md. | Partial |
| M04 | Version/changelog/notes; publish/tag only when actually ready | Local version is `0.2.0`; candidate packages and notes exist, but reported identity errors block release. No evidence of an authorized public release in the local verification record. | Partial |
| N01 | Target + series + result + restart-cache + bad-network + failed-install + Unicode + UI end-to-end flows | See A/D/E/F/G and packaging gaps. Cannot infer all flows from one montage or green unit tests. | Partial |
| O01 | Final 14-part report with exact commits/version/remote status/limits | Existing reports cover completed phases. A final completion report is premature until this audit closes. | Pending final gate |

## Full identity requirement: reviewed evidence still required

The user supplied `D:/Торрент/KyleCrane_Parallel_NikolayOM.mp4` and screenshots
showing that apparent similarity between female characters caused a mistaken
identity claim. Treat the screenshot labels as user-provided ground truth: the
woman in the vehicle and the woman in the dark scene are distinct. Identify the
corresponding source timestamps by visual inspection before using them as a
regression fixture; do not infer identity from candidate number or clothing.

The reviewed control must report, for each labelled appearance, the source time,
expected identity, actual candidate/track assignment, and whether it is
unresolved. It must include repeated appearances of each confirmed identity,
the two distinct women, Kyle, difficult side/back/occluded views, and simultaneous
characters where available. Record false merges separately from missed links or
unresolved tracks. A broad positive merge threshold alone cannot establish the
requirement, and assigning everything to unresolved does not satisfy recovery.
Confirm the results against a fresh analysis and a restart/cache reuse; preserve
raw diagnostics and note the exact model files and revision. Extend the labels
before claiming accuracy beyond the reviewed source. The current limited
release does not establish completion of this requirement.

## Dependency-ordered remaining queue

The latest user instruction restores the full scope and explicitly requests
parallel work on the matcher, performance and general audit. The immediate
release gate is the newly reported identity failure: do not join distinct people
or abandon known repeat appearances, and validate both outcomes on reviewed
source frames. Other workstreams proceed independently; the former instruction
to defer classifier, sorting, matcher and performance no longer applies.

1. Refine posture using already validated model observations; add evidence tests
   and real before/after checks without inventing depth from 2D keypoints.
   Implemented and locally verified for sitting/standing controls; a real lying
   case and broader posture/false-positive coverage remain open under B04/I.
2. Family members/reasons/representative evidence and inspection are implemented;
   finish trajectory/scope review and independent real duplicate-positive controls.
3. Controlled strong-face series/restart/incremental/8%-minority and manual
   reselection controls pass. Investigate intermittent offline mode-cache misses
   and add difficult/ambiguous real episode controls.
4. Build independent real retrieval, duplicate/family and orientation judgements;
   use them to guide missing body-orientation and matcher work rather than
   declaring every unknown dimension finished.
5. Audit provider retry/adaptation/activation/errors against each network gate.
6. Complete motion/state/cancellation/Unicode diagnostics coverage.
7. Clean Release/package/fresh-install/upgrade verification, final localization,
   benchmarks and release readiness; only then evaluate publication and completion.

Each step must update this audit with actual file/log/artifact evidence. None of
the open items is currently an external blocker: useful local work remains.

## Latest verified progress

- Added `measuredSpecificPosture` without changing the broad legacy decoder or
  matcher admission. The structured descriptor refines only independently
  associated, continuous, model-hash/range/source-validated evidence. Both
  temporal and frozen observations must separate the winning posture from its
  nearest alternative. Conflicting supported postures produce Ambiguous/Unknown.
- `tests/fixtures/butcher-posture-observations.json` retains actual cached model
  scores for two independently inspected ranges in the supplied montage. The
  sitting range 50.050–51.718 s contains a foreground bystander; the standing
  range 10.865–12.033 s contains another seated/leaning person. Three source
  samples per range were inspected, with no image/biometric data committed.
- `pf_posture_probe ... --verify` replays production `describeScene`. Before:
  1/2 exact postures and 2/2 compatible broad postures. After: 2/2 exact and
  2/2 compatible. The prior broad seated-or-lying prediction was under-specific,
  not counted as a false classification. This is not population accuracy.
- `structured-posture-focused.log`: 19 action/descriptor/family controls passed
  before the added scene-boundary guard; the final full suite includes that guard
  and passes all 13 targets (`structured-posture-offline-suite.log`, 113.00 s).
- Repeated selection tests exposed hidden remote manifest lookup inside ReID
  discovery. GUI prerequisite checks now search all supported local names first
  and fail before source/session preparation when required assets are missing.
  Legacy diagnostic automatic preparation remains available. The workflow test
  now passes without depending on the remote model manifest; its final full-suite
  duration was 1.90 s. The initial failed full runs remain diagnostic evidence,
  not successful validation (`structured-posture-final-suite.log` and
  `structured-posture-verified-suite.log`).
- The goal is still active. B02 body orientation, real retrieval/family quality,
  difficult series behavior, remaining motion and release/package
  gates are not closed by the posture correction.

- Family evidence, canonical boundaries, physical export reuse, inspector playback and
  controlled before/after replay are recorded in [scene-family-evidence.md](scene-family-evidence.md).
  Frozen 2,037-window replay retains all 125 pairs/canonical IDs; organization
  median changes from 3.785 to 3.856 ms. This closes specific metadata/UI gaps,
  not real duplicate recognition quality or the remaining audit.

- [series-restart-evidence.md](series-restart-evidence.md) records three fresh
  processes with full real-media inference, strong automatic carryover, 8% target
  presence and 8 cached + 2 new per-source assertions. These repeated excerpts
  establish cache/selection behavior, not broad identity or retrieval accuracy.

- Five-process real control additionally proves manual minority override and
  restart persistence, including reuse of all 107 head and 12 action requests.
  A separate intermittent offline mode-change cache miss remains under audit;
  repeated green runs are not described as a causal fix. PF_DEBUG_CACHE now
  retains entry/key/validation/settings/source decisions for investigation.

- Automatic classification/order validation passes 13/14 CTest targets in
  116.15 seconds, including all 57 default UI checks and the actual Butcher
  posture-observation replay. The existing intermittent observation-cache miss
  recurs on second-character reselection (8 processed frames, expected zero);
  an isolated diagnostic rerun passes without establishing a fix. The earlier
  green M02 run remains historical evidence, not the status of this latest run.

- Subsequent settings-persistence investigation records 16 successful fresh
  processes followed by an empty-root cache failure on repetition 17. A separate
  deterministic delayed-write control fails before and passes after capturing
  current UI-owned run settings. Thirty fresh-process workflow repetitions pass
  after that snapshot change, and the final suite passes 14/14 in 115.97 s.
  Failed settings reads also no longer authorize overwriting the file with
  defaults. See settings-persistence-evidence.md; the exact filesystem event in
  the intermittent default-root observation remains unproven, so broader cache
  and restart gates are not marked complete from repetition counts alone.

- [provider-download-evidence.md](provider-download-evidence.md) records rolling
  scheduling, measured retry-driven concurrency adaptation, failure/cancellation
  cleanup controls and a controlled before/after benchmark. Real network/server
  and interrupted activation workflows remain separate verification gates.

- Native body yaw inference now runs on both CPU and CUDA through existing ORT
  sessions. Four offline checks include frozen real observations for 11 source
  views (9 supported, 2 unknown), and exact source-frame reproduction verifies
  original video/frame pixel hashes. Subsequent optional local-model integration
  now adds pose-owned temporal/crop review before family organization, separate
  versioned persistence and descriptor/continuity propagation. Real Butcher
  CPU/CUDA cold/warm/cancel/resume controls pass. Direction readout is implemented
  and its actual popup screenshot inspected; difficult temporal controls now
  support 6/12 coarse-correct samples versus 4/12 before, retaining six visible
  back-view misses and zero supported coarse errors. Direction filters/sorts,
  broader recovery and final packaging remain open. Unsupported body pitch is
  not invented.
