# Export scheduling

Clip export and montage preparation use the same bounded queue by default in
Fast, Exact and ExactLossless modes. No switch is required. The final montage
mux runs after the prepared clips and uses their original requested order.

The queue chooses between one and four simultaneous FFmpeg processes using:

- the number of jobs and logical CPU cores (at least four cores per process);
- currently available physical RAM, keeping half available for the app and OS;
- the largest source resolution, budgeting decoder/encoder surfaces per job.

Unavailable source metadata or memory information selects one worker. Each
process receives a bounded decoder, encoder and filter thread budget. The
scheduler does not change quality, bitrate, native pixel format, clip bounds or
lossless compression. Fast stream-copy still rejects incompatible streams;
exact lossless montage adapts them as described below without a lossy fallback.

Preparation callbacks run serially, just ahead of the bounded encoding window.
Progress callbacks cannot overlap. Output names are reserved in input order;
completed results are returned in that same order even when encoders finish out
of order. Repeated cuts reuse the first successful output bytes. Cancellation
stops further dispatch, forwards the stop token to every active cut, and waits
for cleanup of owned temporary files. Already completed valid files are kept.

## Verification

`ExportConcurrency.RealAllModesKeepOrderAndLosslessPixels` is an opt-in real
FFmpeg integration test (`PF_TEST_EXPORT_PARALLEL=1`), covering eight 720p clips
per mode, independently decoded lossless frame hashes, callback serialization,
output order, and a montage in each mode. Existing queue/montage tests also
cover collision suffixes, cancellation, duplicate reuse, mixed-source montage
normalization, reordered stream-copy frames and incompatible-source rejection.

Historical integrated run on the 24-thread development machine (2026-10-15):

| Mode | Sequential | Parallel | Speedup |
| --- | ---: | ---: | ---: |
| Fast | 724 ms | 405 ms | 1.79x |
| Exact | 2177 ms | 932 ms | 2.34x |
| ExactLossless | 2027 ms | 999 ms | 2.03x |

Rerun from the current `main` build on 2026-10-18, using the same eight-clip
1280×720 fixture:

| Mode | Sequential | Parallel | Speedup |
| --- | ---: | ---: | ---: |
| Fast | 624 ms | 454 ms | 1.37x |
| Exact | 2624 ms | 1197 ms | 2.19x |
| ExactLossless | 2216 ms | 1293 ms | 1.71x |

These are short generated 1280x720 clips, not a promise of the same speedup for
4K scenepacks or a whole analysis. Storage throughput, source decoding and GPU
encoder session limits can determine the actual speed. The implementation
bounds CPU and system RAM; it does not measure free GPU VRAM or disk throughput.
The opt-in check passed from the current build with byte-identical lossless
decoded frames, ordered results and non-overlapping progress callbacks.

## Adaptive exact lossless montage (2026-10-20)

Exact lossless montage no longer rejects different frame sizes before encoding.
It selects a canvas from the maximum width and height of all sources, pads
smaller frames, retains each frame timestamp, and uses one deterministic codec
for every worker. Ordinary 8/10-bit YUV uses lossless AVC (`libx264 -qp 0`);
higher precision/alpha uses FFV1. Missing audio receives silence only if another
clip has audio. PCM float64 avoids an additional lossy audio encode.

Matching video formats and audio rates keep native pixel/sample values. Mixed
YUV formats expand to 4:4:4 at 10 bits (16 bits when needed), using nearest
sampling, without chroma downsampling. RGB/YUV mixing needs colour conversion.
Anamorphic pictures expand the long-pixel axis; there is no shrinking. Mixed
audio rates/channels use the largest source rate/channel count and resample as
necessary. These adaptations change sample representation: lossless compression
must not be described as bit-identical mixed-format conversion. MOV codec
support in third-party editing applications, especially FFV1, is separate from
pixel preservation and is not certified by the export tests.

The prepared outputs are validated against the common canvas/pixel/audio plan
before concatenation. The queue reuse key includes the complete plan. Existing
files remain protected and cancellation discards only unfinished staging.

Verification includes mixed sizes, 8/10-bit formats, 12/24 FPS, 44.1/48 kHz,
mono/stereo and silent clips in one montage; decoded frame hashes and the exact
PCM output duration are checked. A generated 3840x1600 10-bit fixture also
checks fractional trims, unchanged decoded pixel/audio values, and montage.
The personal `D:/for_tests_pf` folder is empty during this audit, so these are
synthetic integration checks, not a claimed scenepack or After Effects test.
