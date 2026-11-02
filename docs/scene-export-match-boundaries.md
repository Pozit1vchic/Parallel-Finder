# Protecting matched frames during scene-end refinement

A sampled scene boundary can disagree with an occurrence: it may be earlier
than the last matched frame, or native hard-cut refinement can move it earlier
than that frame. Previously either case could truncate a supported occurrence
while preparing an accurate export. Scene-end refinement is now limited to a
nearby boundary, and a proposed native edge is applied only if it preserves the
last supported frame. The bound follows the actual canonical family member (or
fallback occurrence), and manual edits do not use automatic refinement.

The `ParallelClip.InconsistentOrRefinedSceneEndCannotCutSupportedFrames` and
`SceneFamily.CanonicalExportUsesItsOwnShotAndManualBoundsRemainExact` controls
exercise conflicting bounds, an allowed cut, invalid cut data, and canonical
family attribution; all 14 selected clip/family tests pass in `family-audit`.
These numerical controls and a successful app build do not establish general
cut detection accuracy or end-to-end results on unseen video. Where a native
cut contradicts matched frames, this intentionally preserves the full match;
the clip may contain a later shot when the detection itself is inconsistent.
