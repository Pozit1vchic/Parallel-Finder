# Shared analysis and refinement cache capacity

The common capacity policy and three compatible native warm runs are verified.
Native analysis of the pressure copy is still running. This document does not
mark the full cache objective complete.

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
- A subsequent native control is running in
  `build/goal-chernobyl-shared-pressure20-native-01`; it is not counted as passed
  until its reports and exact output comparisons are checked.
- The first pressure-preparation attempt stopped before copying because the
  default 5 GiB free-space reserve was unavailable. No deletion was performed.
  The preparer now exposes `--minimum-free-gib` (default remains 5), independently
  reserves the maximum 64 MiB sparse allocation, and records the chosen reserve.
  This run selected 1 GiB, copied about 1 GiB of actual records, allocated only
  2621440 bytes for fillers and left **3476357120 bytes** free before the small
  refinement copy. This is separate from physical disk-full robustness testing.
