# Butcher: first independently reviewed retrieval control

The supplied source is `D:/Videos/butcher_parallel_notsorted.mov`, SHA-256
`6c7ed560ed6e337e2a0d183f52c29d2bb6db6e95cf13f598ad105526e6dc5830`.
Three scenes were inspected before consulting the result list. On sampled
frames, the same character holds a phone to his ear in two different shots:
the forest at 496.201–498.057 s and the shop at 521.956–523.812 s. At
556.949–558.805 s he raises sunglasses instead; this is a deliberately difficult
negative for a phone-action query, not a negative for every possible pose query.
The forest footage repeats at 523.812–525.668 s and is excluded from retrieval
labels: matching copied footage and recognizing an independent parallel are
different assessments.

The actual production windows at indices 1039, 1094 and 1175 were reviewed
against source frames, including their pose keypoints. Contact sheet:
`build/butcher-retrieval-review/labelled-windows.jpg`. The source's
frames, not embeddings or private images, determined the annotations. Both
phone windows cover the demonstrated action and all three tracks belong to
Butcher. The fixture stores scene boundaries, observed track indices and hashes,
not frames or embeddings: `tests/fixtures/butcher-retrieval-controls.json`.

The following command evaluates the complete captured application result list
and, separately, replays only the three labelled production windows with the
captured matcher parameters. It checks source, windows, parameters and result
hashes before use. The complete-list mapping includes family members, not just
representatives; unreviewed candidates remain unjudged. This control concerns
body/action results, so head-only matches are excluded.

```powershell
$env:PATH='D:\msys2\ucrt64\bin;'+$env:PATH
python tools/benchmark/run_butcher_retrieval_controls.py `
  --video 'D:/Videos/butcher_parallel_notsorted.mov' `
  --windows build/butcher-v15-match-windows.json `
  --params build/butcher-v15-match-windows.json.params.json `
  --application build/butcher-v15-match-results.json `
  --probe build/ucrt64-release/tools/pf_matcher_probe.exe `
  --output build/butcher-retrieval-controls-report.json
python -m unittest discover -s tools/benchmark -p test_butcher_retrieval_controls.py
```

The application returned 126 cards in total. Neither phone scene appears in a
body/action card, including family members; both directions of the independently
labelled positive pair were missed. On these **two directions of one pair**,
Recall@1 = 0 and MRR = 0. Precision@1 is numerically 0 under the evaluator's
fixed-slot convention because neither query returned a result; this is not an
observed false positive. The separate three-window replay also returns no
matches. Direct pair diagnostics on those windows report `static_geometry`
as the first failing gate for the positive pair; this is not proof of the
cause of all misses or the complete retrieval path. The debug-enabled full
analysis is not valid performance evidence.

This is an actionable real-source regression control, not a representative
retrieval benchmark: it has only one semantic positive pair, one hard negative,
one video and one capture; many other returned scenes have not been judged.
Broader positive/negative candidate sets, rankings, family judgements and
independent holdout videos remain necessary before drawing quality conclusions.

## Sustained partial-body recovery

The matcher now measures a sustained visible hand-to-face pose when another
arm is cropped. It requires a verified identity, compatible coarse geometry,
at least 1.25 seconds of temporally supported and stable landmarks, and
agreement between the two views. This is pose evidence only: it does not
identify a phone, glasses, an object, or intent.

On the exact recorded windows and captured production parameters, a replay
now returns the forest/shop pair in both query directions, while the reviewed
sunglasses transition is not returned. A replay of all 2,054 captured windows
changed the selected results from 126 to 127: the added result pairs the two
phone scenes, no previously selected pair disappeared, and the fields of the
126 retained pairs were unchanged. The controlled fixture and 124 matcher
tests pass. This comparison is on one saved analysis, not a population recall
or precision claim; sustained hand-to-face poses can occur for different
actions, and a broader independent review is still required.

## Direction labels for pose-only results

An offline replay compared the former and current grouping-direction logic on
128 all-people Butcher results, using saved pose windows and match ranges. The
saved windows do not contain learned action or head-orientation evidence, so
this is a partial geometric replay rather than a full UI classification audit.
The first version of the independent-direction change gave five pose-only
pairs new left/right/mixed movement labels even though the results compared
static poses. Reviewing three frames from each side of one of these pairs
(windows 1357 and 3313) showed why this is misleading: minor camera or body
displacement can produce a direction despite the pose-only search scope.

Result presentation now marks motion direction unknown for two pose-only
windows; posture and pose classification remain independent. A UI regression
test covers drifting pose windows and range edits. With this guard, the same
partial replay changes 50 formerly labelled pose directions to unknown and
changes one motion pair from unknown to mixed; its three sampled frames were
inspected, but do not establish an accurate physical movement label. Local
contact sheets are in `build/grouping-direction-review-79b` and
`build/grouping-direction-review-124`. Reproduce the diagnostic with
`pf_matcher_probe <windows.json> --grouping-directions <results.json>`.

The legacy endpoint movement classifier now subtracts measured whole-person
centroid displacement before judging wrist lift or spread. Focused tests cover
uniform horizontal and vertical translation as well as three previously
reviewed Butcher wrist observations. On the 3,376 saved all-people Butcher
windows, replay with the captured parameters retained all 128 prior selected
pairs and identical serialized match records. This is a regression check on one
capture; it does not establish general gesture accuracy or correct for camera
motion independently of the person's screen-space displacement.

### One-handed gesture correction

A before/after endpoint-classifier replay of the 3,376 captured windows showed
154 changed gesture labels after subtracting whole-body displacement. Three
sampled frames from window 305 (53.87–55.37 s) show Butcher lifting one hand;
the earlier two-hand average labelled it `static` after body compensation.
The classifier now retains the stronger wrist movement when the other hand is
still, and uses `mixed` when clearly opposing movements are observed. The
recorded skeleton is retained in `tests/fixtures/butcher-one-hand-raise.json`.

Compared with the prior compensated version, 53 of 3,376 endpoint labels change;
these windows overlap and are not independent cases. Additional selected frame
sheets were inspected, but borderline gestures and the other changed labels
are not all independently reviewed. Replaying the full matcher kept all 128
selected pairs and serialized match records identical. All 190 focused tests
passed, with one optional benchmark skipped. Frame sheets remain local in
`build/wrist-relative-review-a` and `build/wrist-refined-review-b`.

A subsequent fresh application process reused the v16 analysis cache and ran
the complete target-selected search on the source video. Its 127 cards include
the forest/shop pair as a body-pose result (score 0.889); the reviewed glasses
scene did not appear as its counterpart. This confirms the specific case in
the application as well as the isolated replay; it is still one labelled pair
and the known character identity errors remain release blockers.
