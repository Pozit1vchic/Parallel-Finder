# Reject inadmissible scores before family deduplication

The final result organizer accepted non-finite and out-of-unit-range similarity
values before sorting and choosing one match per scene-family pair. A NaN breaks
the numeric ordering assumptions; positive infinity can outrank a valid match.
When both records describe the same family pair, the invalid representative
could discard the valid record. This is separate from the observed prevalence
of weak generic poses and is not presented as a semantic-quality fix.

The organizer now filters similarities outside finite [0,1] in its private working
copy before constructing families. Cancellation still leaves the caller's input
unchanged. Normal measured similarities, pose/motion admission and ranking
weights are unchanged.

The existing production-organization regression now exercises NaN, both
infinities, -0.1 and 1.1, in both input orders alongside a valid duplicate. It
also verifies that an invalid-only input yields no admitted matches. The test
failed against the previous organizer, then all **14 SceneFamily tests passed**
with the guard. Evidence:

- `build/goal-organization-score-guard-before.log`.
- `build/goal-organization-score-guard-after.log` and corresponding target build
  logs. Only the static core library and test executable were rebuilt.

The native application used by the ongoing uncached benchmark was not relinked:
its SHA-256 remains
`5565a0a048e1f37a950961ad80883a2663c6f395eded4607543b0a5ec286e74d`,
matching the run's contract. Its measurements therefore describe the previously
frozen application, not this new guard. Full application integration and the
full regression suite must be rerun after the uncached measurement sequence.
