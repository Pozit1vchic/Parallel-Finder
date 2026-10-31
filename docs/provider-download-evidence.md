# Provider download scheduling and reliability evidence

The provider part downloader now uses a rolling bounded queue. A completed part
frees its slot immediately, even if an unrelated part is still downloading.
The same hash-verified persistent part paths and `ModelStore::download` transport
are used; scheduling does not replace HTTP validation or archive verification.

## Adaptation and cancellation

The initial ceiling is the caller's connection limit, clamped to 1–4. A new
retry observation halves the limit for new work (minimum one). Already healthy
in-flight requests drain normally. Two clean completions per current slot raise
the limit by one, up to the caller's ceiling. Retried completions do not count as
clean recovery evidence. This responds to actual transient failures, rather than
assuming that more connections always improve the network.

Each part still has at most three attempts. Transient network errors, HTTP 5xx,
429 and hash mismatches are retried; metadata/path/security failures are not.
Nominal backoff is 500 then 1,000 ms, checked for cancellation every 50 ms.
`ModelStore` retains its 60-second inactivity timeout, two-hour total guard,
strict Content-Range validation and authoritative size/hash checks. The app also
has a release-manifest fallback; a package asset currently declares one URL,
so there is no invented package mirror.

The parent alone reports aggregate bytes and concurrency changes. Workers only
update separate atomic counters/outcomes and their own errors. Parent
cancellation, a terminal part error or a throwing progress callback joins every
active worker before returning/unwinding. Completed/partial files remain on
disk. `PF_DEBUG_DOWNLOAD` records part retry and initial/retry/recovery connection
decisions without printing URLs or credentials.

## Regression controls

`provider-adaptive-final-focused-tests.log`: 18 offline checks passed and four
explicit opt-in network/package checks were skipped. The new scheduling checks
cover:

- A semaphore proves that a later part starts before the unrelated blocked first
  part completes; this fails with the original fixed-wave scheduler.
- An actual retry lowers the limit, clean completions restore it, and the
  concurrency observer always runs on the parent thread.
- Caller limits 0, 1, 2, 4 and 99 remain clamped to 1–4, with no oversubscription.
- A terminal HTTP 404 retains the original failure and joins interrupted peers.
- A throwing progress callback joins active workers.

Existing checks still cover retained partial data on interruption, per-part
retry, aggregate progress, verified assembly, a failed staging probe preserving
the installed runtime, and offline verified installer reuse.

## Controlled before/after scheduling benchmark

`pf_provider_schedule_probe` uses 16 callbacks with fixed controlled delays,
five passes per profile. Both baseline and updated standalone probes were built
with the same GCC `-O2` and Qt libraries, then executed sequentially. The frozen
baseline header is the unmodified implementation at `d6ec0ff`, SHA-256
`c8952f802cf3f6180c32226f6793102520e898429f70274052657284ab8f715f`.
Reports are `build/provider-schedule-controlled-before.json` and `...-after.json`.

| Controlled profile | Median before | Median after | Interpretation |
| --- | ---: | ---: | --- |
| Uniform, 50 ms per callback | 245.69 ms | 245.51 ms | Essentially unchanged |
| One 350 ms straggler, others 50 ms | 539.87 ms | 355.07 ms | Free slots continue useful work |
| First part temporarily returns 429 | 859.42 ms | 676.50 ms | Other parts continue during retry/backoff |

Every part completes exactly once except the deliberately retried first part
(two attempts). At most four download callbacks run concurrently. These are
queue scheduling measurements, **not internet bandwidth or public-server speed**.
An initial sleep-polling prototype regressed the uniform case and was corrected
to wait for completion events; its initial reports remain separate under
`provider-schedule-before.json` / `...-after.json`.

The final Release build and all 13 CTest targets pass in 128.28 s
(`provider-adaptive-full-suite.log`), including 56 passing UI checks.

## Remaining verification

Actual process-restart partial HTTP transfer, a real server ignoring Range,
slow/stalled response integration, activation interruption recovery and the full
UI error/retry/localization states still need explicit controls. The existing
helper/header checks do not prove these complete workflows. Full public runtime
downloads and final package/fresh-install/upgrade gates remain opt-in/open.
