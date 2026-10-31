# Analysis settings during asynchronous persistence

## Observed failure and reproducible control

At `24b2851`, a fresh-process repetition of the target-character workflow
passed 16 times, then failed in static-mode reuse on repetition 17. Its buffered
cache decision recorded an empty configured cache root and a cache miss. The
worker was waiting for a new target choice while the test expected cached
completion. The existing failure report and a live native stack snapshot are
retained privately under `build/cache-reselection-audit-17.txt` and
`build/cache-reselection-hang-stack.txt`. The live process was inspected rather
than restarted on an observation timeout; it subsequently ended with its own
QtTest failure in 105.013 seconds. This observation establishes inconsistent
settings, not the exact filesystem event that supplied defaults.

A deterministic regression now pauses the settings writer using a condition
variable, changes the UI cache directory and scene threshold, then starts a
normal analysis. Before the fix it used the old directory; the test failed with
the actual old path versus the requested `pending-settings-cache` path
(`build/settings-snapshot-before.txt`). No disk delay or timing guess is used as
the synchronization predicate.

The analysis launch now captures its UI-owned cache directory, cache size,
processing-thread count and scene threshold on the UI thread. The worker applies
these immutable values to its settings before cache lookup or inference, while
preserving non-UI settings such as minimum scene frames and adaptive multiplier.
Reading settings errors now aborts that run instead of silently analyzing with
defaults. Debug cache decisions separately retain the persisted root and actual
configured root.

The delayed-writer check proves that the persisted directory is still old while
the worker uses the new directory and threshold. After releasing and draining
the writer, another run must reuse observations and process zero new frames.
The snapshot change passed the complete selection/reselection/cancellation and
batch workflow in 30 consecutive fresh processes, including the delayed-write
control (`build/settings-snapshot-stress-1.txt` through `-30.txt`). This does not
prove absence of every future cache failure or explain every earlier failure.

## Do not overwrite unreadable settings with defaults

The asynchronous writer previously cleared a load error, applied a partial
change to default settings, then replaced the original file. A separate
regression supplies malformed JSON and proves this behavior before the fix
(`build/settings-read-error-before.log`). The writer now skips the update and
save when reading fails and passes the error to its completion callback. The
test requires the change callback not to run and the original bytes to remain
unchanged. A missing first-run file still permits normal initialization.

Release rebuilding succeeds and all nine settings-store checks pass in
`build/settings-persistence-core-tests.log`. The final complete CTest run passes
14/14 targets in 115.97 seconds (`build/settings-persistence-full-suite.log`),
including all 57 default UI checks and the strengthened delayed-writer workflow.
Explicit opt-in real-media tests remain skipped in this default suite. No user
settings, source videos, model weights or generated test profiles are committed.
