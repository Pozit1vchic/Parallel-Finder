# CPU analysis thread budget

The 0.3.0 report showed CPU analysis of four long videos while the user expected
CUDA. Selecting CUDA manually improved progress on that machine. The CPU path
also deserves its own test; the GPU report does not establish CPU performance.

An isolated, cache-disabled, combined analysis used the same short excerpt of
Chernobyl E03, YOLO26m and ReID weights, CPU runtime, and all-people mode. The
original video and user settings were left untouched. Results are individual
runs on one machine, not medians or a full-film speed claim.

| Run | Analysis time | Decoded frames | Analyzed frames | Pose detections | Pairs |
|---|---:|---:|---:|---:|---:|
| 0.3.0 automatic threads | 49.186 s | 526 | 127 | 240 | 1 |
| 0.3.0 explicit 4 threads | 39.826 s | 526 | 127 | 240 | 1 |
| 0.3.0 explicit 8 threads | 23.819 s | 526 | 127 | 240 | 1 |
| Candidate automatic CPU inference budget | 24.197 s | 526 | 127 | 240 | 1 |

Explicit thread settings also affect other stages. The final comparison uses
the same automatic settings on both binaries and changes only the default CPU
inference pool: half the logical processors, clamped to 1–8 threads per
session. Explicit counts and GPU policies are unchanged. The one returned pair
kept both tracks, time ranges and classification; similarity differed by
less than 2e-8. Fifteen session cache tests passed. This does not establish
result parity for all videos, performance on other processors, or a correction
to automatic GPU selection.

Local evidence: `build/goal-031-cpu-thread-control-02`,
`build/goal-031-cpu-default-01`, and
`build/goal-031-cpu-default-session-tests.log`. The first attempted offscreen
run did not reach analysis and was stopped; it is excluded from timing.
