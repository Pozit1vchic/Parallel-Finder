# Scene colour conversion performance

The Butcher cold-analysis log (`build/butcher-v15-results.json.stderr.log`)
reported 24.0 seconds in the scene stage within a 130.0-second pre-selection
pipeline; this stage includes decoding and other work. The HSV colour
conversion used by scene comparisons computed `fmod(x, 6)` even though, in
the red-maximum branch, `x = (green - blue) / (max - min)` is necessarily
within [-1, 1]. The remainder is therefore exactly `x`.

A focused A/B uses the production `SceneDetector.cpp` compiled before and
with this change at `-O3`. On this machine, 48 frames sampled at 4 FPS from
30–42 seconds of `butcher_parallel_notsorted.mov`, scaled to 320x180, were
passed to `SceneDetector(27,2,3).detect` 100 times. Two alternating runs
measured 368/364 ms before and 273/280 ms after. All six detected boundaries
and their scores produced the same bitwise hash (11146920438572263424).
These figures measure the repeated in-memory detector alone, not the whole
scene stage or whole application; no end-to-end speedup is claimed.

The hue outputs were also compared bitwise against the original formula for
all 16,777,216 possible 8-bit RGB triples, with zero differences. On one
1280x720 real Butcher frame, 40 repeated colour-conversion passes measured
602–642 ms before and 213–218 ms after, with identical output hashes. The
source video is not redistributed. GPU, disk, seek, UI and export performance
are outside these measurements.

## Avoid impossible fade comparisons

The fade pass previously converted and compared both compact images on every
eligible low-motion frame, even before a fade could satisfy the minimum of
three observations or the minimum aggregate brightness change. These two
existing predicates can be checked first, without changing the conditions for
emitting a boundary or altering accumulated state. The fade and cut tests
remain unchanged and pass.

With release optimization and the production detector, 480 frames sampled at
4 fps from seconds 300–420 of the Butcher video (scaled to 320x180) were
processed ten times in memory. Alternating before/after runs measured
258.4/139.4 ms and 264.1/134.2 ms. All 74 detected boundaries and scores
produced the same bitwise aggregate hash, 617779512827091184. The 48-frame
30–42-second control also retained all six boundaries and scores, with
275/127 ms and 275/129 ms over 100 iterations. This only measures the
in-memory detector, not video decoding or the complete application; no overall
speedup is inferred. The source and intermediate raw frames are local and are
not distributed.

After this change, the focused `*Scene*:*Cut*` regression run passed 104
checks; five explicitly optional real-source/NVIDIA checks were skipped.
