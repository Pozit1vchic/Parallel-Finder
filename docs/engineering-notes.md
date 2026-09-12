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
