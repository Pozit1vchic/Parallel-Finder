# Preview cache write performance

Each result still is written atomically as a PNG, then stored as an independent
PFCACHE1 entry. Previously the general result-preview writer called `put` for
every image; `put` scans the entire cache for LRU eviction. For a large existing
cache this repeated directory traversal dominates preparation. The writer now
flushes groups of up to 32 images or 32 MiB and performs one eviction scan per
group. Individual files remain atomic. The final partial group is flushed on
finish, and already written entries are budgeted even if another write fails.

Local A/B harness `build/perf-preview-writer.cpp` uses the production code,
isolated temporary directories, 1,000 pre-existing cache entries, and 128 PNGs
at 640x360 from the user's Butcher frame at 30 seconds. A first run measured
6,935 ms before and 1,641 ms after. A subsequent reversed-order comparison
gave 6,440 and 5,867 ms before versus 1,651 and 1,631 ms after; medians over
the three runs are 6,440 and 1,641 ms. The concatenation of all 128 cached PNGs
has identical SHA-256
`9caa2b187851955a43137a0d6569893c4fbb2669b03e861061d5880b91a22397`;
the cache size was identical. This is a focused cache/encoding benchmark, not
the time for the complete analysis, and it does not establish the 520,000-frame
goal. Actual results depend on disk, cache size, compression and result count.

The targeted cache and preview tests cover image bytes, queue draining,
cross-source reset, an incomplete final group, an oversized write in the middle
of a group, and limit enforcement after that failure. Fifteen passed, with one
separate opt-in existing-cache benchmark skipped (`build/perf-preview-tests.log`).
