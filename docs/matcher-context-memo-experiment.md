# Matcher scene-context memo experiment

The experimental `build/context-memo-experiment.patch` reuses the identical
scene-context calculation across mirror, torso, and head alternatives within a
single pair. This has **not** been integrated: a clean benchmark did not show a
speedup. The prior 166/177-second diagnostic runs had verbose matcher logging
enabled and cannot support a performance conclusion.

On this Windows machine, the two previously built probe executables were run
three times each in alternating order, with `PF_DEBUG_MATCHER` absent, on the
same 2,054 extracted windows from the user's Butcher video. The input windows
SHA-256 is `1ca463034e3411b5f1b38611fcc0927cc6106706b62dd6a3a5665b5612d49570`;
the parameters SHA-256 is
`d3be5b31bb58a6e6f10f40538421ca9ae95620b04741924de1e5550c7c66d55b`.
Probe internal matching times in milliseconds:

| Run | Existing code | Memo experiment |
| --- | ---: | ---: |
| 1 | 5670.8 | 6137.2 |
| 2 | 6370.2 | 6597.4 |
| 3 | 5828.6 | 5726.4 |
| Median | 5828.6 | 6137.2 |

All six runs returned 126 results with identical serialized result arrays
(SHA-256 `fd935df42998024a65481d53a42c3f8ab44c90a00e48e75f63137d0f0d5180c1`);
timing fields differ. Three runs on one machine are not a general performance
estimate, and this frozen workload does not prove equivalence for all inputs.
The two executable SHA-256 hashes are respectively
`bb2cf670ec2fe3109091961188902faf5a70b9489903c019ed15f8949f74a42a` and
`fd099849ceae38a72c0d6769d259f0396d7c4d1753c6511ab74fafa5400d1b75`.
The experiment remains unmerged pending a workload where it provides a measured
benefit without changing results.
