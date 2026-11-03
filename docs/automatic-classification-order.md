# Automatic classification and ordering

## Shared observed landmarks for movement

Movement direction and gesture normalization now compare the same landmarks
that are finite and confident at both endpoints. Unobserved limbs no longer
extend the body bounds or shift the centroid when their confidence changes.
Fewer than three common landmarks or a degenerate common extent produces
unknown rather than a confident movement label. These are geometric controls;
they do not distinguish camera movement from a person's actual movement.

The focused classifier, ranker and matcher run passed 182 tests with one
optional benchmark skipped. The recorded Butcher wrist controls still pass.
On the 2,054 saved Butcher windows, the matcher replay selected 132 pairs
instead of 127: 15 pairs appeared and 10 disappeared compared with the earlier
saved replay. The previously reviewed phone-pose pair remained present and the
chosen glasses-transition control remained excluded. The changed pairs have
not been independently reviewed, so these counts do not establish improved
overall recall or precision; the complete application has not been rerun.

Reviewing frames around 490.25–491.4, 593.6–594.9 and 654.4–655.6 seconds
exposed a false body-pose result in both replays: the first scene shows Butcher
standing with a bottle, the second reclining with a phone, and the third
standing with a bottle. The newer replay pairs the first two; the earlier
replay pairs the second and third. An experimental articulation-score veto
removed the former but selected the latter again. A stricter veto also rejected
existing changed-view positive controls. Neither veto is shipped: broader
evidence is needed to reject these mismatches without losing valid parallels.

The matcher now checks sustained wrist-height asymmetry relative to the
shoulder axis before accepting an articulation-based static match. It uses
only measured shoulders and wrists; missing wrists do not create a veto.
Replaying the three reviewed standing/reclining windows now returns neither
known mismatched pair, while a self-repeat of each recorded pose and the
existing changed-view aiming controls pass. The isolated phone-pose positive
and sunglasses negative also retain their previous outcomes. Across 2,054
saved windows, selection changed from 132 to 129 pairs (four removed and one
newly selected); their full trajectories have not been independently labelled.
This is not an overall accuracy claim or a complete fix for posture mismatch.

The default result list now preserves independently measured specific posture.
Two confirmed sitting intervals form `sitting`; two lying intervals form `lying`.
A sitting/lying disagreement becomes `posture_mismatch`, with an explicit conflict
flag. A broad seated-or-lying observation stays broad when compared with a
compatible specific observation. Missing posture remains uncertain. Existing
manual classification overrides are preserved.

The shared GUI/export/MCP taxonomy puts specific classes before broad, uncertain,
conflicting and unknown classes. All six interface languages include the new
specific labels. Static pose disagreements now also set the conflict flag.

Continuity previously used only the left occurrence of each pair. Organization
now computes transition costs for both displayed lanes, preserving A/B ownership,
and retains deterministic tie breaking and cancellation. The default class sort
uses this paired continuity rank within each class. Imported records without a
rank follow ranked records and retain quality ordering; this explicit separation
avoids a non-transitive mixed-rank comparator.

The export dialog previously received the visible selection order but ignored it,
then requested chronological renumbering. It now validates and deduplicates that
order, appends any selected rows absent from it, and requests sorted numbering.
Explicit chronological montage behavior remains a separate existing choice.

## Evidence and limits

- Core regression checks cover a pair sequence whose right lane jumps by 100
  seconds, lane swapping and cancellation. Both lanes now influence the result.
- UI checks cover specific/broad/missing posture, posture conflict flags, manual
  overrides, mixed ranked/imported rows, class placement and export selection
  order. Existing export numbering contracts remain checked.
- The UI classification check additionally replays the two independently reviewed
  Butcher observations in `tests/fixtures/butcher-posture-observations.json`.
  The sitting interval now receives `sitting`, and its comparison with the
  standing interval remains an explicit posture disagreement.
- These are deterministic correctness controls, not population recognition
  accuracy. There is no reviewed real lying interval in this fixture. A fresh
  complete analysis of both source videos, broader action classification quality,
  recovery of difficult back views, and complete trajectory grouping remain
  separate open quality gates. Ordering cannot repair mistaken identity evidence.

## Validation for this change

The Release build succeeds. The focused core run passes 17 checks. The final
complete CTest run (`build/classification-sorting-full-suite.log`) passes 13/14
targets in 116.15 seconds, including all 57 default UI checks (7 explicit opt-in
skips), the new real-observation UI replay and real-posture controls.

The remaining failure is the already-open intermittent observation-cache miss in
`TargetCharacterTests::selectionCachedReselectionAndCancellation`: reselecting
the second character processes 8 frames instead of reusing observations. The
captured source summaries report `observationCacheHit:false`. A separate buffered
diagnostic rerun passes (`build/classification-target-cache-diagnostic.txt`),
which does not establish a causal fix. This failure is retained as evidence;
neither its assertion nor a timeout was weakened to make the suite green.
The subsequent settings snapshot investigation and its green full-suite run are
recorded in [settings-persistence-evidence.md](settings-persistence-evidence.md).

## Independent posture and action facets

An additional regression found that a confirmed sitting posture returned before
movement grouping: sitting while raising an arm and sitting while lowering an
arm both became simply `sitting`. The new contract check fails on the earlier
implementation (`build/posture-actions-before.txt`). Specific sitting/lying
posture now combines with actions actually supported on both sides, for example
`sitting+raise/static`. Existing head-turn phase and compound-action rules are
shared with the other movement classifications. A confirmed action disagreement
becomes `action_mismatch` (or its head-scoped equivalent) even when posture
agrees. A posture mismatch remains explicit. Static comparisons and uncertain
posture do not acquire guessed action facets.

Inside a class, existing Strong/Possible evidence tiers precede continuity rank;
known classification conflicts cannot be promoted by a high numerical match
score. The two existing filtered views remain separate. A regression verifies
that a supported row precedes a conflicting row in the Strong view, while the
Possible row remains available in its own view. This is not continuous evidence
quality calibration or a proof of the best quality/continuity tradeoff.

The focused UI run passes five test functions plus initialization and cleanup
(`build/posture-actions-contracts.txt`). It checks the two seated actions,
opposite actions in both pair directions, compound class canonicalization,
filter membership and export class mapping, alongside existing trim/range,
manual-override and real frozen Butcher posture controls. The action examples
are controlled pose trajectories with validated synthetic action observations;
they do not establish real-video action recognition accuracy. No primary
observation cache version changes and no additional neural inference is needed.

The final Release full suite passes 14/14 targets in 115.79 seconds
(`build/posture-actions-full-suite.log`), including all 57 default UI checks and
the real frozen posture controls. The seven explicitly opt-in UI workflows
remain skipped in this default run. Six-language catalog validation also passes.
