# Image/text model audit for printed people

This is local research, not an enabled person filter. A CLIP ViT-B/32 model
compares candidate crops with fixed descriptions of live game characters and
printed portraits. The source model is [OpenAI CLIP](https://github.com/openai/CLIP);
the tested ONNX files are a third-party [split export](https://huggingface.co/inference4j/clip-vit-base-patch32/tree/88fd91c4b12c8301c2f61afc97afd06bd236e945),
not independently validated as numerically identical to the original model.
Their SHA-256 values are `e8518f8b64e5abdd0bdb44e336a4e1367f48803c5a60462baea32dc0d18a2fd7`
(vision) and `ec9cfa29fc10a5c6dd5e7efd3b4aab7351a1961a134721622e5eaa57dd44981f`
(text). The OpenAI tokenizer JSON has SHA-256
`b556ac8c99757ffb677208af34bc8c6721572114111a6e0aaf5fa69ff0b8d842`.

`tools/benchmark/audit_clip_picture.py` decodes the user-provided Butcher
frames locally and reports cosine similarities for explicitly recorded prompts
and three crop sizes. Similarity is not a calibrated probability. On the
original ten selected controls, a hand-chosen threshold of 0.01 for the
best printed-image prompt minus the best person prompt at crop scale 1.5
flagged all four printed detections and none of six live-person controls.
This threshold and crop were selected on that set, which shares scenes.

In a separate, unlabelled sample of 100 face-backed tracks, it also flagged
track 777: visual inspection shows a live person, not a picture. Requiring
the same 0.01 margin at both 1.5 and 2.0 excluded that case. This second rule
was then tried on 200 additional unlabelled tracks; it flagged track 58, where
Butcher is a live person standing in front of paintings. Both cases are added
as live-person controls in `tests/fixtures/butcher-printed-person-controls.json`.
The other sampled frames were not independently labelled and cannot be used
as true-negative counts. The results do not establish a population error rate.

The model is therefore not integrated into the application: even the
two-scale cue can remove the target character when a painting is behind him.
The local captures and model files in `build/clip-picture-audit/` are excluded
from version control. One reproduction command, with local Python dependencies
`onnxruntime`, `numpy`, `Pillow` and `tokenizers`, is:

```powershell
python tools/benchmark/audit_clip_picture.py `
  --model-dir build/clip-picture-audit `
  --video 'D:/Videos/butcher_parallel_notsorted.mov' `
  --controls tests/fixtures/butcher-printed-person-controls.json `
  --ffmpeg 'D:/msys2/ucrt64/bin/ffmpeg.exe' `
  --output build/clip-picture-audit/labelled-new.json
```

On 12 controls the reusable tool reproduced the prior scores for the first
ten exactly. Its timing includes decoding and CPU inference and is not a
benchmark of application latency.
