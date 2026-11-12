# Lazy result previews

## Confirmed cause and implementation

The previous warm result path read and decoded all 376 cached PNGs, then wrote
them to a temporary session before publishing 188 pairs. Normal-exit cleanup
had already been fixed, but each process still materialized about 615 MB of
working images outside the persistent cache budget. Most images were never
viewed.

`ResultPreviewImages` registers an immutable table of source/time/cache-key
requests. Publication reads no image payloads and creates no PNGs. A Qt Quick
image provider loads an image only when QML requests its opaque session/index
URL. The production engine and UI test engines install that provider before
loading QML. No QML layout, resolution, timeline, or export policy changes.

On a hit, PNG bytes are decoded directly from the existing `preview-v1` cache
entry. On a miss or invalid PNG, the provider opens the source, seeks to the
same timestamp using the existing 1 ms admission tolerance, converts the first
eligible frame, and stores an optional lossless PNG in the same shared-budget
cache. It writes no temporary image. If exact-frame decoding fails, the
existing window preview remains a display fallback and is never stored as the
requested exact frame.

Qt runs provider requests asynchronously. A session serializes image work and
retains neither decoder pools nor decoded images. The controller owns the
session; the registry holds weak references. Closing/replacing results rejects
queued and subsequent requests, while an active operation checks closure
before publishing its image. Requests check source size/mtime and source-reset
generation; invalid IDs and oversized PNG dimensions are rejected. Cache
formats and existing entries remain compatible. Character-selection and
manual-review temporary image paths retain their existing behavior.

## Validation scope

The dedicated Qt tests cover cached pixel equality, insertion after request
registration, invalid IDs, session closure, source changes, manual source reset,
missing/corrupt entries, actual decoding with cache disabled, no temporary PNGs,
window fallback without exact-frame cache pollution, and asynchronous QML Image
readiness/dimensions. The small decoding fixture is `tests/fixtures/tiny.mp4`;
it does not establish full-episode accuracy or throughput.

An opt-in native control consumes a frozen episode result report and the
isolated benchmark cache. It compares dimensions and SHA-256 of every decoded
RGBA pixel against the previous cached image, keeping only hashes between
images. It also forces uncached decoding once per source and compares those
pixels. This is a preview integrity test, separate from analysis timing.

The first native control passed for all **376** unique images from **188**
pairs, with zero source frames decoded for cached loads and zero cache writes.
The fallback control covered both original episodes, decoded two frames total,
and preserved their pixels. Evidence:
`build/goal-lazy-preview-native-pixels-01.{json,txt}`. Reference report SHA-256:
`e9a48d56a5ec94933979fd40fdc9844e3c4d6851ebfee3e922286b41b9f0c27a`.

The initial integrated build passed all 16 CTest suites in 144.48 seconds; the
subsequent engine-setup regression run passed all 16 in 141.50 seconds. These
precede the final fallback-preservation addition, whose validation must be
recorded separately rather than inferred from those runs.

The final fallback implementation rebuilt successfully and passed **16/16**
CTest suites in **145.33 s**, including the explicit decode-failure fallback
test. Logs: `build/goal-lazy-preview-fallback-{build,ctest}.log`. A second native
pixel run of that implementation repeated the full 376-image and two-source
fallback checks successfully in **43298 ms**:
`build/goal-lazy-preview-native-pixels-02.{json,txt}`. This time includes test
preparation and pixel hashing and is not analysis or first-image latency.

## Native timing: publication with images deferred

The first three native S01E01/S01E02 runs took **7650/4080/4086 ms**, median
**4086 ms**, with process-wall median **4759 ms**. All 188 records were identical
to the preceding identity-policy implementation except preview URLs. Primary
decoded/analyzed/detection/write-failure counters stayed zero; raw Matcher and
all head/action requests hit their caches without review-model construction.
Every process left zero temporary runtime files. Evidence:
`build/goal-lazy-result-previews-native-01/summary.json`, its three reports,
policy comparisons, and `previous-version-exact-comparison.json`.

The earlier eager implementation's native median was **8606 ms**, but its work
included materializing all previews. The new timing ends at result publication
and defers image loading until viewing. It is not the time to display all 376
images and must not be presented as a comparison with identical preview work.
Sampled process logical I/O maxima had median reads **1326948022 → 670688101
bytes**, writes **615590359 → 4251 bytes**. These are sampled counters, can miss
work after the last sample, and are not physical storage-write measurements.

Final-code repetition in `build/goal-lazy-result-previews-native-02` took
**7642/4064/4079 ms**, median **4079 ms**; process-wall median **4763 ms**.
All three reports exactly match the preceding implementation's full records
after removing only the two preview URLs. Their new URLs all use the image
provider. Primary work and post-exit runtime footprints remain zero, and all
expected stage-cache hits were verified. Sampled logical read/write maxima
have medians **666788709/4251 bytes**. The first process's last telemetry sample
missed more tail work, illustrating why these are sampled rather than final
whole-process I/O totals. `summary.json` records individual report hashes and
per-run values; `contract.json` freezes executable/settings/source hashes.

Original source hashes and user settings were rechecked unchanged in
`build/goal-lazy-preview-source-integrity.json`; the user settings hash was
checked again after final native controls. All writes used disposable test
settings, cache roots, temporary test directories and benchmark reports.

## Remaining limits

This change does not improve semantic matching or classification accuracy.
It does not bound the total persistent plus all other runtime storage, finish
the uncached three-run timing matrix, make primary model construction lazy, or
prove crash/disk-full/concurrent-reset behavior. Metadata checks do not detect
content replacement preserving size/mtime, and generation checks around a
write are not a transactional reset/write protocol. The native integrity test
forces a missing-preview decode on one timestamp per episode, not every frame
or decoder backend. Character-selection images remain eager. Existing opt-in
long manual-review/export tests require their external videos and models and
must not be claimed as executed by the default green CTest run.
