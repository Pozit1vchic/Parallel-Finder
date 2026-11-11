# Windows PowerShell GPU telemetry compatibility

The Windows PowerShell 5.1 resource collector recorded GPU allocation samples
but failed to emit engine utilization because .NET Framework does not implement
`Double.IsFinite`. The observed `gpuError` in the first uncached control states
that missing method. Its earlier CPU, RSS, logical I/O and GPU-memory fields
remain available; missing utilization must not be treated as zero or reconstructed
from those other fields.

The collector now uses the equivalent `IsNaN`/`IsInfinity` checks followed by
the existing 0–100 validity range. Out-of-range counter samples retain their raw
value, an invalid flag and a null usable percentage. No utilization clamping or
cross-engine addition was introduced. Optional `MaxSamples` permits a bounded
collector check without stopping the observed application.

Validation:

- Two live samples collected by Windows PowerShell 5.1 against the actual
  uncached analysis process completed without the missing-method error and
  included GPU engine and allocation arrays. Evidence:
  `build/goal-powershell51-gpu-monitor-check.jsonl`. These two samples are a
  collector functionality check, not full-analysis GPU utilization statistics.
- Eight finite/range controls (NaN, both infinities, -1, 0, 74, 100 and 234) and
  Windows PowerShell parsing of both benchmark scripts passed:
  `build/goal-powershell51-monitor-validation.log`.
- The smoke harness captures the monitor SHA-256 and actual shell executable
  before starting each subsequent collector, preserving the version of the
  measurement code in the corresponding analysis report. The running first
  uncached collector retains its original code and is not silently replaced.

The first uncached run is designated functional-only before completion, as
documented by the frozen timing policy; subsequent designated timing runs use
the fixed collector. No application/model rebuild was needed or performed.
