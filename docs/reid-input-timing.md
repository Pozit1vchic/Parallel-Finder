# ReID input preprocessing measurement

The ReID crop is sampled as 8-bit RGB, then each channel is normalized using
fixed ImageNet constants. For each channel there are only 256 possible results.
The input builder now computes the same FP32 expression once for each value and
reuses that table; the crop coordinates, layout, model and provider are unchanged.

An isolated local comparison used one frame at 30 seconds from the supplied
Butcher video, resized to 1280x720 RGBA. It compiled the actual `makeInput`
function from the previous source and the modified source with GCC 16.2 `-O3`.
For each of NCHW and NHWC it wrote 100 tensors from different crop coordinates;
the resulting 78,643,200-byte files have the same SHA-256
`296bb0420f9da5f03b1c354ac92b0008fa8af7f204ebf3a2bbbcecc6d2f8eac`.
In six local runs of 1,000 preparations, NCHW took about 149 ms before and
52 ms after, including tensor allocation. This is a microbenchmark: it does not
measure decoding, neural inference or full-analysis time. Concurrent compilation
was running, and this is not evidence of an overall analysis speedup or of the
520,000-frame target.

An ONNX identity-model regression exercises all 256 input values in each channel
for both supported tensor layouts and checks the exact resulting normalized
embedding. It complements the real-frame comparison; neither test assesses
identity quality or replaces end-to-end performance measurements.

The actual packaged OSNet model (SHA-256
`75146f8fa0893808981a8d2d52586789e59521f2b06e3d7415439e7e718dd2ea`)
was also run on CPU with 40 different crops of the same frame. Six alternating
before/after process pairs all produced identical embedding bytes (SHA-256
`3167140b3d1c7546a9ead7e4549524eaa5c80b24e2d53f8d5c24e1a1570c9911`).
Times including session creation varied from 195–207 ms before and 190–199 ms
after; the small difference does not establish an end-to-end speedup. Local
inputs, harnesses and measurements are retained in ignored `build/perf-*` files.
All three `ReIdEstimator.*` tests passed against the real CPU runtime, including
the new complete byte-range/layout control.
