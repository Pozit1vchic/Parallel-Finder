# Lossless combined export validation

The reported failure was in the combined video, while high-quality re-encoding
worked. The exact failing clip list, output drive state, and original error
text are not known, so these checks do not reproduce that original failure.

The montage exporter previously copied the entire completed mux output into
a second temporary file before publishing it. It now moves the completed
file from staging on the same filesystem into its reserved destination. This
removes one complete output copy and its extra space requirement, preserves
existing exports, and leaves the lossless encoding and muxing policy unchanged.

The four original Katniss sources were used for a combined export: 3840×1600
HEVC, 10-bit 4:2:0, and stereo 48 kHz audio. The extended real-source test
exported four eight-second intervals starting at 74.115, 82.115, 90.115, and
98.115 seconds, one interval from each file in filename order. The resulting
32-second MOV matched the full ordered decoded-frame hash list and all decoded
audio samples. The test also verified each individual cut and retained video
geometry and metadata. It passed in 118.184 seconds; that includes verification
and concurrent diagnostics, so it is not a performance measurement.

Reproduction uses `CutService.ExactLosslessRealSourcePixelsAudioAndMontage`
with `PF_TEST_EXACT_LOSSLESS_SOURCE`, the three other paths in
`PF_TEST_EXACT_LOSSLESS_ADDITIONAL_SOURCES` (platform path-list separator),
`PF_TEST_EXACT_LOSSLESS_START=74.115`, and
`PF_TEST_EXACT_LOSSLESS_CLIP_SECONDS=8`. The log is
`build/goal-031-long-katniss-lossless.log`; no media is tracked.
