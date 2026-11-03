# Search without choosing a character: Butcher validation

The real-video test `realVideoAllPeopleSearch` uses the supplied
`butcher_parallel_notsorted.mov`, the existing YOLO26m pose model, SFace and
OSNet, CUDA, fast sampling, and the combined search mode. It is opt-in via
`PF_TEST_REAL_CHARACTER_VIDEO`, `PF_MODEL_PATH` and
`PF_TEST_REAL_CHARACTER_CACHE`; `PF_TEST_REAL_CHARACTER_REPORT` optionally
saves result records. The original video and model files remain local.

The first run completed in 178.4 seconds, processing 4,668 pose samples and
producing 130 result cards from 3,376 windows without opening the character
selector. An immediately repeated run reused observations (zero newly analysed
frames), completed in 15.2 seconds and produced the same 130 ordered pairs.
Matcher timings were 45.0 and 11.5 seconds respectively. These measurements
are individual runs, not controlled speed benchmarks; they include different
cache and preview states. Raw local logs and result JSON are in `build/`.

Of the 130 cards, 16 did not have verified same-person evidence, four had
body-only identity evidence, and 110 had face verification. The absence of
same-person evidence does not prove that the two people differ. A visual
contact sheet of the 16 unverified pairs is `build/all-people-unknown-review.jpg`;
individual stills alone do not establish whether their full trajectories are
correct matches. No population precision or recall is inferred from these
counts. The pre-existing standing-versus-reclining mismatches documented in
`automatic-classification-order.md` remain unresolved.
