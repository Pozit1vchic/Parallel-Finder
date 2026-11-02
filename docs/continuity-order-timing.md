# Continuity-order pruning

The greedy continuity organizer compares each remaining result with the current
result. Its transition cost has nonnegative terms, with a time-distance base (or
an exact fixed cost when sources differ). It now skips skeleton and orientation
comparisons when that lower bound cannot beat the best candidate. For paired
results, bounds from both lanes are added. The original strict comparison and
sorted traversal remain, so equal costs retain their existing deterministic
order. Cancellation is checked at the same intervals as before.

A focused local A/B harness (`build/perf-continuity.cpp`) compiled production
sources with GCC 16.2 `-O3`; it supplied 1,000 synthetic two-lane occurrences,
random timestamps over 770 seconds and 17 fully observed random skeleton joints.
Five alternating runs took 53.13–54.10 ms before and 8.46–8.66 ms after;
all produced the same FNV-1a hash of the 1,000 ordered indices,
`14078053271887940259`. This is a synthetic ordering microbenchmark, not the
user's video, a full analysis, or evidence of meeting the 520,000-frame target.
The benefit varies with timestamps, pose evidence and number of results; the
quadratic scan remains. The harness and executables are kept in ignored build
files for local reproduction.

All 14 SceneFamily focused tests pass in the independent `perf-cache-audit`
build. A new regression checks that a better pose can beat a closer timestamp,
equal-cost ties remain stable, and source changes remain correctly ordered.
