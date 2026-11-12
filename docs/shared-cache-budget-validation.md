# Shared analysis and refinement cache capacity

The common capacity policy, three compatible native warm runs and four native
analyses of the pressure copy are verified. This document does not mark the full
cache objective complete.

`CacheBudget` coordinates an explicit set of flat managed directories under one
capacity. The analysis controller registers its primary cache, movement review
cache, video-action cache and body-orientation cache. The same shared budget is
also retained by the manual result-review context. When supplied, this budget
replaces the older independent per-directory capacities; standalone reviewers
without a shared budget retain their existing bounds.

The total includes payloads, ownership and source-generation records, and other
regular files directly in those managed directories. Subdirectories are never
discovered or recursively traversed. Unknown files consume capacity but are not
deleted. Source-generation records survive capacity eviction so clearing a
source cannot reactivate older observations. Recognized ownership records are
removed with their payloads, and orphaned ownership is reclaimed under pressure.

For recent data, primary observations have the highest retention priority;
raw Matcher results and neural review observations precede replaceable previews.
Within a tier, eviction uses access time, then size and deterministic filename
order. The seven-day retention preference is a pressure policy, not proof of a
seven-day elapsed persistence test. A newly written entry may be declined if it
cannot fit without exceeding capacity. Preserved metadata or inaccessible files
that alone exceed capacity produce an explicit failure.

All participating cache instances use the shared recursive coordination mutex.
The tests cover concurrent readers/writers within this process and budget;
this does not establish cross-process coordination or physical disk-full safety.

## Evidence gathered so far

- Four focused `PfCache.Shared*` checks pass in
  `build/goal-shared-budget-focused-tests.log`: aggregate payload/ownership
  accounting, preservation of generations and unknown files, concurrent writes
  across directories, and replacement of old subdirectory limits with retention
  of neural review observations before previews.
- The initial implementation passed the complete CTest suite, 15/15 in
  144.74 seconds. Its subsequent native harness failed before opening a video
  because Windows PowerShell decoded an unmarked UTF-8 JSON contract using the
  system encoding. That attempt is not an analysis or performance result.
- The final retention/subdirectory-cap changes passed a new full build and
  **15/15 CTest checks in 145.91 seconds**. Three separate native warm analyses in
  `build/goal-chernobyl-shared-budget-02` took 11616/10800/10964 ms, median
  **10964 ms**. Every run retained all 188 cold-reference records exactly apart
  from temporary preview URLs, reported zero primary decode/analysis/detection
  and write failures, and reused all head/action observations without loading
  those review models. Peak working sets were 936701952/936546304/936730624 bytes.
- A completed separate-copy control adds 20 GiB of sparse logical preview pressure
  plus the real refinement tree in `build/goal-cache-shared-pressure20-02`.
  The extended `pf_cache_budget_probe --shared-refinement` reports both PFC-only
  and aggregate totals and checks unchanged primary payload hashes. Sparse
  logical pressure is not a physical disk-full test. Aggregate bytes decreased
  from **22472297337 to 21319670395**, below the 21474836480-byte limit. All four
  primary payload hashes were unchanged. All **8062** refinement files retained
  their paths, lengths and SHA-256 hashes (zero differences in before/after
  manifests). PFC-only totals, separately reported, are 22468908779/21316456971.
- The subsequent native control completed in
  `build/goal-chernobyl-shared-pressure20-native-01`. All four runs retained all
  **188 records** exactly except temporary preview URLs. Primary analysis
  decoded/analyzed/detection counts and write failures remained zero. The raw
  Matcher cache hit, all 3936 head and 95 action requests were cached, and those
  review stages constructed no inference models. The first run rebuilt 376
  previews removed by pressure, taking **34468 ms**; its preview decoding is
  separate from the zero primary-analysis decoding counter. The next three runs
  took **8749/13540/13628 ms**, median **13540 ms**. Final recursive cache
  footprint (including refinement) was **21398555574 bytes**, below the
  21474836480-byte limit. This verifies a native repeat under this logical
  pressure scenario, not sustained multi-process or physical disk-full safety.
- The first pressure-preparation attempt stopped before copying because the
  default 5 GiB free-space reserve was unavailable. No deletion was performed.
  The preparer now exposes `--minimum-free-gib` (default remains 5), independently
  reserves the maximum 64 MiB sparse allocation, and records the chosen reserve.
  This run selected 1 GiB, copied about 1 GiB of actual records, allocated only
  2621440 bytes for fillers and left **3476357120 bytes** free before the small
  refinement copy. This is separate from physical disk-full robustness testing.
- After the pressure analysis finished, three full uncached native runs were
  started in `build/goal-chernobyl-cache-disabled-01` with `cacheEnabled=false`
  in isolated settings. Only the first completed (functional equivalence only);
  the second failed when the disk filled, and the third never started. There is
  no valid uncached timing median; see `uncached-analysis-functional-validation.md`.

## Admission priority regression (2026-11-12)

A new non-preview entry was unconditionally protected during eviction. Under a
tight budget, admitting a raw Matcher result or head/action/body refinement
could therefore evict recent primary observations, even though those have a
higher recomputation cost. This affected both local and shared budgets.

`PfCache.NewCheaperEntriesCannotDisplaceRecentPrimaryObservations` reproduces
the defect with four actual production key families in local and separate-root
shared caches. A 400-byte budget can retain the primary record and its ownership
metadata, but cannot retain both records. The test requires rejection of the
cheaper incoming record, unchanged primary bytes, removal of the rejected
record's ownership metadata, and capacity compliance. It failed before the fix.

Admission protection now breaks ties only within the incoming entry's retention
tier. Older entries of that tier are reclaimed first; an incoming lower-tier
entry is reclaimed before any higher-tier record. Preview admission and the
existing seven-day protection window retain their policies. This is a bounded
eviction regression, not a new full-episode performance or accuracy measurement.

Validation: the complete application rebuilt successfully; all 26 `PfCache`
tests passed (1847 ms), followed by all 15 CTest suites (141.94 s). Evidence:
`build/goal-cache-cost-priority-before.log`,
`build/goal-cache-cost-priority-after.log`, and
`build/goal-cache-cost-priority-final-ctest.log`. The intermediate CTest log is
not the final result: it ran the first revision, which failed an existing
same-tier replacement assertion. Attempted linking while that run was alive
was rejected by Windows file locks; final build and tests ran sequentially.
