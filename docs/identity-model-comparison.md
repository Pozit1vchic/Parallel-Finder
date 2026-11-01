# Identity model investigation

The current SFace and compact OSNet models remain in use. Model replacement has not been justified by this small investigation.

The published InsightFace pretrained weights examined during this investigation have a non-commercial restriction and were not adopted. An upstream OSNet x1 model was also compared locally against the packaged compact ReID model; similarities for the same and different women overlapped, so that model alone did not reliably separate them. Rights for any replacement weights and training dataset would require separate review.

The [AuraFace-v1 model card](https://huggingface.co/fal/AuraFace-v1) describes Apache-2.0 licensing and commercial use. Its local `glintr100.onnx` file had SHA-256 `a7933ea5330113b01c9b60351d8f4c33003f145d8470ac5f0e52ee2effe25c60`. On selected face crops, current SFace / AuraFace cosine similarities respectively were 0.322 / 0.147 for the two different women at 17.03 and 59.03 seconds, versus 0.230 / 0.179 and 0.394 / 0.097 for same-woman pairs at 17.03 and 24.36 or 54.85 seconds. At 13.68 seconds no face was detected. This small sample does not establish a model ranking, but it does not support replacing the current model to solve these cases.

The source video and tested model files are local artifacts and are not included in the repository. See `identity-v020-controls.md` for the actual clustering controls and remaining failures.
