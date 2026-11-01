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
