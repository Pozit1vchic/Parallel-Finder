# Quality specification: completion audit and remaining work

Goal: finish the **whole** supplied specification, not just the character picker.
This document is a work queue, not a completion certificate. An active goal stays
active until the missing and unverified items below have authoritative evidence.

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
| A08 | Identity diagnostics and unique identities | Selection reason/margin logs, coherent tracks, single-face ownership, relative distinctness, six reviewed Kyle controls. General false merge/split rates not measured. | Partial |
| B01 | Identity/temporal/source/scene/sequence/cluster descriptors | `SceneDescriptor` has most fields; sequenceId currently not established by real sequence detection. | Partial |
| B02 | Continuous body yaw/pitch/roll and eight useful directions | Optional pinned local model runs before family organization with temporal/flip/crop checks and separate persistence. Explain Match now shows eight view labels plus continuous angles/support/unknowns in six languages, with short-window/reduced-motion checks. Same-frame all-17-output replay improves difficult temporal controls from 4/12 to 6/12 coarse-correct samples on CPU/CUDA, zero supported coarse errors; six back-view misses remain. Native single-frame controls support 9/11. Dimension filters/sorts, broader recovery, unsupported pitch and packaging remain open; see body-orientation-investigation.md. | Partial |
| B03 | Independent continuous head yaw/pitch/roll | Measured rotation, validity checks, head/body-independent tests, SO(3) similarity. Need independently reviewed real orientation cases. | Partial |
| B04 | Standing/sitting/lying; crouching/kneeling when reliable; ambiguous/unknown | Validated action observations now refine sitting/lying with temporal/frozen class margins. Conflict, wrong source/range and scene-boundary tests pass; two real reviewed controls pass. No real lying control yet; no fabricated kneeling. | Partial |
| B05 | Motion semantics, direction, velocity/trajectory/temporal evidence | Existing MovementClassification/windows plus image velocity; audit missing useful acceleration/periodicity and camera contamination. | Partial |
| B06 | Visibility, occlusion, missing-data/identity/pose/temporal quality | Body/head scoped quality and degeneracy tests. Confidence remains evidence strength, not calibrated probability. | Evidenced, limited |
| B07 | Camera motion/scale/view/context when reliable | Existing scene sequences/context; descriptor camera influence remains unknown. Need integrate actual available camera evidence or document measured insufficiency per observation. | Partial |
| B08 | Composable descriptors, ambiguity, filter/sort individual dimensions | Structured head/body + posture filters exist. Orientation/motion-dimensional ordering remains incomplete. | Partial |
| C01 | Explicit broad retrieval -> geometry -> identity -> alignment -> context | Existing indexed matcher has cheap gates and expensive compare paths. Verify real stage counters and a readable architecture map before declaring complete. | Unverified |
| C02 | Preserve independent identity/pose/motion/alignment/head/action/camera/visibility components | Several fields exist, but complete truthful component exposure and missing-data semantics need audit. | Partial |
| C03 | Similarity separate from quality; dedicated ranking | Scoped evidence/best score, tests and exports; review consistency after independent head/action enrichment. | Partial |
| C04 | Strong list + difficult true-match recovery | Strong/Possible UI exists; broader recovery recall for the enumerated hard cases is not labelled/proven. | Partial |
| C05 | Explain Match without internal noise | Existing localized explanation; must audit all component values against actual measurements. | Partial |
| C06 | Investigate local rejected/reviewed signals; retain reasons, no neural training | Existing review/hide actions exist; durable structured rejection reasons have not been established. | Unverified |
| D01 | Separate classification/grouping/dedup/ranking/continuity | `SceneFamily`, `ResultOrganization`, existing classification. | Evidenced |
| D02 | Actual scene/sequence/time soft locality; no fixed ten-minute bins | Scene boundaries/time-based continuity costs exist; actual sequence clusters not populated. | Partial |
| D03 | Multi-signal overlapping + adjacent + true copied-event families | Complete-link and strict both-range pixel proof; retained measured admission reasons and unique members. Full trajectory and mixed-scope compatibility remain to review; see scene-family-evidence.md. | Partial |
| D04 | Dedicated representative quality, completeness/occlusion/camera | Scoped evidence/identity/temporal/match terms retained per member after head/action review; representative changes reuse measured admission proof. Full completeness and measured camera terms remain unverified. | Partial |
| D05 | Canonical results before export + stable defensive export identity | Family pair dedup, fingerprints, shared JSON evidence and canonical own-shot bounds verified. Real cutter/decoder test proves four requests -> three physical outputs with Unicode sources. Real duplicate-positive neural quality fixture remains open. | Partial |
| D06 | All requested practical sorts, smart classification groups | Specific measured sitting/lying classes, conflicts after supported classes, paired continuity inside default groups and visible-order export are implemented. See automatic-classification-order.md. Broader labelled action/sort quality and in-class evidence priority remain to verify. | Partial |
| D07 | Bounded diversity suppresses top redundancy | Existing bounded family-use penalty and organization tests; real labelled diversity ordering not evaluated. | Partial |
| D08 | Continuous local pose/head/body transition cost, deterministic practical ordering | Greedy continuity with time/scene/pose/head/roll and now optionally measured continuous body yaw. Actual sequence evidence and broader ordering/cancellation controls remain open. | Partial |
| E01 | Profile decode/detection/pose/face/ReID/track/cluster/windows/match/classification/previews/cache | Real stage logs exist; explicit clustering/classification timing coverage needs audit. | Partial |
| E02 | Persistent source/model/settings/version compatible observations/artifacts | PFCH + window/scene/head/action caches, atomic writes/checksum tests. Audit exact observation-settings vs matcher-settings separation. | Partial |
| E03 | Change target/matcher/classification/order without full inference | Target/mode/order reuse proven; 8+2 source addition and restart are now proven; changing every matcher setting still needs end-to-end proof. | Partial |
| E04 | 8 cached + 2 new infer only new | Three fresh-process real-media controls: seed 8 misses/144 pose frames; add 2 -> 8 hits/243 new pose frames; repeat 10 -> 10 hits/zero inference. Per-source assertions and raw UTF-8 logs retained in series-eight-percent-verified. | Evidenced, controlled fixture |
| E05 | Adaptive inference and GPU profiling with measured quality/speed | Existing periodic ReID/usable-face sampling, pipeline counters and preview benchmark. Broader adaptive quality/transfer validation remains open. | Partial |
| E06 | Atomic cancellation cleanup, incomplete artifacts never valid | Character/window/cache write tests exist; retained independently verified data after cancellation. Audit remaining stages. | Partial |
| F01 | Persistent resumable Range/chunk downloads, bounded/adaptive concurrency | Persistent chunks and rolling queue now reuse free slots; retry pressure halves new-work concurrency, clean completions recover within a 1–4 caller bound. Semaphore, retry, limits, cancellation/failure/exception tests and controlled scheduling benchmark pass (provider-download-evidence.md). Full network adaptation remains to exercise. | Partial |
| F02 | Per-part exponential retry, timeouts, fallback/mirrors where supported | Inspected three attempts with nominal 500/1000 ms backoff, 60 s inactivity and 2 h total guards; manifest fallback exists, package metadata currently has one URL. Per-part retry/cancel controls pass; actual slow/no-Range/mirror workflows remain unproven. | Partial |
| F03 | Separate installer cache, version/size/hash state, verify before complete | ProviderStore persistent verified archives/parts, hash-failure tests. | Evidenced |
| F04 | Offline/restart retry and cancellation preserve valid parts | Local interrupted-transfer/offline/cancel tests. Add/inspect explicit process-restart and no-Range server proof. | Partial |
| F05 | Transactional staging/probe/activation preserves active provider | Interrupted rename state is now discoverable on startup through the preserved previous bundle, without mutation; fresh-process Unicode controls and installer restore-before-failed-probe/offline retry pass. Real GPU process-kill and activation-marker boundaries still need package-level proof. | Partial |
| F06 | Unicode discovery/probe/child args/config/logs | Native paths and Cyrillic/Japanese install controls; audit all remaining conversions. | Partial |
| F07 | Human error + stable code + Retry/Details | Existing provider status may still contain low-level errors. Inspect and correct before completion. | Unverified |
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
| I02 | Recall@K, Precision@K, MRR, median rank/false positives/identity/family/confusion where appropriate | Evaluator tests exist; real retrieval/family metric reports are missing, not zero. | Missing |
| I03 | Controlled before/after quality and performance | Frozen Butcher replay and six Kyle controls documented; classifier refinement needs its own baseline/replay evidence. | Partial |
| J01 | Character tests: all enumerated selection/batch/minority cases | Core and workflow evidence; controlled strong-face and 8% minority E2E now pass (A06/A07); real manual minority override/restart also pass; difficult series and intermittent offline mode-cache misses remain open. | Partial |
| J02 | Classification tests: all views, boundary, posture, head/body, missing/unknown | Helper angular tests are not body-orientation estimator tests. Specific posture working change adds evidence gating checks. | Partial |
| J03 | Dedup/sort/cache/downloader enumerated regression controls | Existing suites cover many clauses. Requirement-to-test name mapping and missing no-Range/slow/restart cases still required. | Partial |
| J04 | UI transitions never leave controls inaccessible | Existing UI suite plus selector/runtime proof; family/source morph states remain untested. | Partial |
| K01 | Why selected/matched/rejected/family/representative/cache invalidated/download reused/probe failed | Selection and matcher reason enums/logs exist. Family admission measurements and representative component scores now retained/shared/exported. Cache invalidation explanations and remaining provider diagnostics still require audit. | Partial |
| L01 | Grounded phases, baseline before changes, no reckless rewrite | Baseline worktree and logs retained; logical commit history. This audit preserves original unfinished scope. | Evidenced |
| M01 | Branch/status/tags/release conventions, no unrelated deletion | Feature branch clean at start; release README inspected. Tag/release state and complete licenses/package audit still required. | Partial |
| M02 | Full suite, benchmark, clean Release build, packaging/resources | Release build and all 14 CTest targets pass in 115.39 s (body-direction-nvdec-final-suite.log), including 57 UI checks. Actual-model CPU/CUDA and hardware NVDEC body controls pass; six difficult back-view misses remain documented. Export cancellation now has separate real-progress and live-child barriers, each repeated three times. Clean final installer/upgrade verification remains open. | Partial |
| M03 | Fresh install, previous upgrade, cache migration, provider update | Developer binary and synthetic store tests are not packaged fresh-install/upgrade proof. | Missing |
| M04 | Version/changelog/notes; publish/tag only when actually ready | Local `0.1.0-rc.23.3-dev`; notes retained; no tag/push/release attempted. Remains unreleased until gates pass. | Partial |
| N01 | Target + series + result + restart-cache + bad-network + failed-install + Unicode + UI end-to-end flows | See A/D/E/F/G and packaging gaps. Cannot infer all flows from one montage or green unit tests. | Partial |
| O01 | Final 14-part report with exact commits/version/remote status/limits | Existing reports cover completed phases. A final completion report is premature until this audit closes. | Pending final gate |

## Dependency-ordered remaining queue

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
