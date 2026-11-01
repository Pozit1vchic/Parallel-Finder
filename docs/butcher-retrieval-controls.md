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
