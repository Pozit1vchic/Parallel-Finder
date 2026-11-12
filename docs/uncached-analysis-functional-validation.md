# Full uncached functional equality control

The first native cache-disabled S01E01/E02 analysis completed with all **188
results exactly equal** to the earlier completed cold-cache reference, excluding
only temporary `leftPreview`/`rightPreview` URLs. Evidence is in
`build/goal-chernobyl-cache-disabled-01/uncached-01.json` and
`uncached-01-comparison.json` (zero changed records).

This is a functional result, not an eligible timing comparison. Before it
finished, `build/goal-uncached-timing-policy.json` excluded this run because
pose probes, review extraction and a small diagnostic test build overlapped it.
The separately designated timing runs were uncached-02, uncached-03 and
uncached-04 in `goal-chernobyl-cache-disabled-02`. Uncached-02 subsequently
failed when the D: volume filled during preview/report writing; no finalized
report was produced. Uncached-03 and the queued uncached-04 did not start, and
dependent integration jobs correctly stopped. No uncached median exists from
this sequence. A fresh timing sequence is required after the preview-lifetime
correction; the failed run must not be silently replaced in this artifact.

The isolated settings explicitly contain `cacheEnabled=false`. Actual work:

- 146567 primary decoded frames, 35179 analyzed samples, 86394 pose detections.
- Raw Matcher cache miss, with 188 raw results.
- Head review: 3936 requested, zero cached, 3273 observed, 19362 decoded frames,
  two review-model constructions.
- Action review: 95 requested, zero cached, 90 observed, 3800 decoded frames,
  one review-model construction.
- All 376 previews rendered; zero cached previews. No reported primary-cache
  write failures.

These observations distinguish actual source processing from a successful
process exit or an accidentally cached run. The binary is the frozen pre-guard
application recorded in its contract. The newer invalid-score organization
guard has not been relinked into that executable.

## Metadata correction

The test script inherited `settingsSha256` from its cached baseline contract
before changing the isolated settings to disable caching. The original contract
and actual settings are retained unchanged. An explicit
`contract-settings-hash-amendment.json` records both the inherited hash and the
actual hash, the original contract hash, and the reason. This correction does
not alter application settings, analysis output or timing eligibility. The
extra uncached run was guarded and did not start, so no second contract was
created. Fresh uncached scripts must compute their actual settings hash after
changing the isolated settings rather than inherit this cached hash.

The initial collector's GPU-utilization method was unavailable under Windows
PowerShell 5.1; CPU/RSS/logical-I/O and GPU-memory records remain available, while
missing utilization remains unknown. Subsequent runs use the corrected monitor;
live samples in uncached-02 already contain valid per-engine records. This is
not a claim that every future counter sample is valid or a full-run GPU summary.
