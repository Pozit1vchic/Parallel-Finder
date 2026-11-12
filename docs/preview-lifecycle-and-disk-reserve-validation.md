# Temporary preview lifetime and benchmark disk reserve

During uncached-02 the D: volume reached zero free bytes. PNG writing reported
an error and the harness failed while writing its output; no finalized analysis
report/comparison was produced. That run is **not** a valid timing or completed
functional result. Dependent extra-run/integration jobs stopped at their guards
and were not silently restarted. The successful earlier uncached-01 remains
functional-only under its predeclared timing policy; a clean uncached median is
still outstanding.

## Reproduced retained working data

The system temporary directory is `D:\tmp\temp`, outside the configured analysis
cache. A separate frozen-binary, isolated-temp native control in
`build/goal-preview-lifecycle-before-01` returned the expected 188 records but
left **377 files / 615477335 bytes** in its preview session after exit. Several
recent global temporary sessions likewise retained about 615 MB each. Cache
hits avoided decoding but still materialized full previews on disk.

The analysis worker now has an explicit preview-owner scope and moves ownership
to the published result instead of leaving a thread-local shared owner behind.
Published results retain their previews while in use; worker exit, cancellation
or replacement no longer relies on thread-local destruction for this reference.

After the fix, all three complete native runs in
`build/goal-identity-preview-after-02` reported **zero files / zero bytes** in
their isolated runtime directories after normal exit. All 376 previews were
cache hits and all 188 measured/classified pairs were preserved, so cleanup was
not achieved by skipping the preview work. The companion identity-policy update
intentionally changes order/score/tier metadata and is verified separately.
The full regression suite passed 15/15 after both changes.

This proves normal-exit lifetime behavior for these runs. It is not proof that
forced termination, antivirus locks or every failure path leaves no orphan.
Previews are still eagerly materialized while results are active, with transient
disk I/O and duplication. Lazy preview delivery and accounting for all working
space remain architectural work, rather than being declared solved by cleanup.

## Reserve and isolation

The smoke harness gives each report a unique child-only TEMP/TMP directory and
records its post-process footprint. It does not change the user's environment
or delete their temporary sessions. It checks available report-volume space
before launch and during execution, with a configurable default 1 GiB reserve.
Reserve exhaustion aborts the test and writes failure metadata; it is never
reported as successful analysis. Resource samples also record report-volume
free space. A Windows PowerShell 5.1 preflight control correctly rejected an
impossible reserve without launching analysis or creating a successful report;
both benchmark scripts passed the existing parsing/finite-value checks.

The latest successful three-run control did not perform harness-side deletion:
the zero retained files come from application ownership release. A controlled
in-flight reserve-loss check remains to be executed; the real disk-full incident
is preserved as a failure, not substituted for that controlled test.

## Space reclaimed without deleting data

An earlier recursive deletion of an obsolete disposable cache had been rejected
by automatic approval review; no deletion was performed. Instead, sixteen files
in the explicitly marked failed pre-v2 pressure control were verified to have
all-zero filler payloads and the expected disposable keys/header/filenames.
NTFS sparse zero ranges reclaimed **8588886016 bytes** of allocation. Every
file's full SHA-256 before/after was equal, logical sizes and files were retained,
and source videos/cache records were not rewritten. Evidence:
`build/goal-zero-filler-reclaim-01.json`, with its original report retained in
the user's local temporary directory. No user working-cache purge was used.

Read-only SHA-256 verification of the two original source episodes still matches
the frozen source contract (`build/goal-original-source-integrity-20261112.json`).
The user settings hash remains
`DCFB88623F1853DF9A5BF004D95DC1500362504A40DAE02E567A9514996915D1`.
