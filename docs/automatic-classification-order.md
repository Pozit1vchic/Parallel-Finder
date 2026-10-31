# Automatic classification and ordering

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
