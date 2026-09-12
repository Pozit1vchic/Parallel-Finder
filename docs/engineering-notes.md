# Engineering notes for Parallel Finder

This project uses the following external guides as review references, not as
blind style rules:

- [C++ Core Guidelines](https://github.com/isocpp/CppCoreGuidelines): prefer
  RAII, explicit ownership, small interfaces, and type-safe resource handling.
- [cppbestpractices](https://github.com/cpp-best-practices/cppbestpractices):
  keep warnings enabled, build through CMake/CTest, and add a regression test
  for each meaningful feature or bug fix.
- [System Design Primer](https://github.com/donnemartin/system-design-primer):
  separate concerns, identify bottlenecks, measure before optimizing, and make
  cache/queue trade-offs explicit.
- [Naming cheatsheet](https://github.com/kettanaito/naming-cheatsheet): use
  short, intuitive, descriptive English names with a clear action/context;
  boolean names state the expected condition.
- [thoughtbot guides](https://github.com/thoughtbot/guides): favor consistency,
  small reviewable changes, and explain a disagreement instead of silently
  violating a shared convention.
- [Fluent UI](https://github.com/microsoft/fluentui): model components as
  reusable stateful parts with explicit enabled, hover, focus, pressed and
  selected states; keep accessibility and design tokens close to the control.

Applied in the current UI pass:

- `PfSlider` and `PfCheckBox` are reusable QML controls instead of duplicated
  hand-built styling.
- all nine matcher values have named backend properties and are copied into
  `MotionMatcherParams` at analysis start;
- the left rail scrolls instead of clipping controls;
- empty state, settings and result states are explicit states, not fake data;
- the core remains Qt-free and the app/UI bridge owns presentation concerns.

Backend performance note:

- `pfcore::MotionIndex` is a deterministic, dependency-free HNSW-style index.
  `MotionMatcher` builds one fixed-size temporal embedding per motion window,
  queries a bounded union of nearest candidates in both directions, and only
  then runs exact band-constrained DTW. The candidate threshold remains a
  similarity gate, while DTW remains the acceptance decision. This keeps the
  all-pairs result semantics (a window may appear in multiple matches) without
  paying the quadratic DTW cost for every possible pair.
- `pfservices::ModelStore` treats a release manifest as untrusted input: only
  HTTPS URLs are accepted, downloads resume into `*.part`, declared size and
  SHA-256 are checked, and only then is the file installed under the local
  models directory. A missing network or manifest leaves the explicit local
  model path usable.
- `pfgpu::PoseEstimator` validates the loaded static input shape before the
  first frame. The profile controls fixed batch (`b1`, `b8`, `b16`), short final
  batches repeat their last frame, and the decoder accepts both end-to-end
  `[B,N,57]` pose output and raw `[B,56,8400]` output with C++ IoU-NMS.
- `pfcore::DominantPersonTracker` refuses to reconnect a track after a
  configurable time gap (default 1 s), and matching ties are deterministic.
  `MotionMatcher` uses position-plus-velocity descriptors, path-length
  normalized Sakoe–Chiba DTW, and applies the cross-file gap constraint. The
  production window builder uses a 1.0 s window and 0.25 s stride on decoded
  timestamps, so VFR material is not silently converted to guessed counts.
- Stage 3c adds a transparent pose-only `MovementClassifier` (direction plus
  gesture heuristics), `MotionRanker`, and versioned `PFCACHE1` disk entries.
  Cache records are keyed by source/model/file metadata, written atomically,
  hash-named, size-limited and evicted by least-recent access. `JobManager`
  now supports multiple workers while preventing two jobs for the same source
  path from running concurrently; unrelated paths can use the available
  workers, and queue overflow remains explicit backpressure.

Stage 4 audit and implementation notes:

- Text exports are written through a sibling `.part` file and installed with
  a rename. Prefixes are restricted to filename-safe characters, so an export
  cannot escape the selected destination directory. EDL, FCPXML and AEP
  require a positive FPS obtained from the decoder/probe; no writer silently
  invents a 30 FPS timeline. FCPXML keeps rational frame durations such as
  `1001/30000s` for 29.97 FPS.
- `NumberingMode::AsInVideo` orders pairs by source and timeline position;
  `RenumberSorted` orders by rank score with deterministic similarity/time
  tie-breakers. AEP output contains a real JSX importer with footage layers
  plus the adjacent `parallel_data.json` sidecar.
- `CutService` still uses QProcess without a shell and encoder fallback
  NVENC → AMF → QSV → CPU for exact cuts. Optional output ceilings prevent
  upscaling while preserving aspect ratio; fast stream-copy mode rejects a
  resize request because filtering would invalidate `-c copy`.
- Settings schema 3 persists all nine matcher controls. Writes use `QSaveFile`
  and now check directory creation and the byte count returned by the write.
