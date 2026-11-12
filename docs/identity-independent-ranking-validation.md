# Identity-independent organization in all-people mode

All-people search accepts comparisons between different people, but final
organization still applied an 0.85 multiplier to pairs without verified common
identity. The presentation/range-edit layer independently repeated that formula
and its identity-dependent evidence tier. Updating only the organizer initially
changed order while the displayed score reverted to the previous policy.

The organizer now receives the identity requirement explicitly. All-people mode
does not discount an otherwise identical comparison for missing common identity;
selected-person mode retains its previous identity policy. Shared core helpers
calculate organization score and evidence tier in both organization and scene
presentation. Each row retains `identityEvidenceRequired` so editing a range
does not silently restore a different policy. Legacy rows default to the old
identity-required behavior. Identity-verification flags themselves are unchanged.

## Verified behavior

- Core organization coverage compares identical geometry with and without
  verified identity under both policies.
- A Qt regression checks that evidence refresh preserves the all-people score
  and that a later range edit retains the policy; switching explicitly to the
  selected-person policy restores the previous factor/tier.
- Seven verifier tests exercise a valid policy transition and rejection of
  changed measurements/classification, wrong scores/tiers, missing edit policy,
  pair-membership changes, duplicate pairs, incorrect diversity penalties and
  weaker greedy choices.
- The full build and **15/15 CTest suites passed in 142.16 seconds** after the
  shared presentation calculation was fixed. The first native integration
  attempt correctly failed when presentation still overwrote the score; its
  reports remain in `build/goal-identity-preview-after-01`.
- Three complete native S01E01/E02 repeats in
  `build/goal-identity-preview-after-02` retained the exact same **188 pair
  identities and measured/classification fields** as the previous cold reference.
  **137 positions intentionally changed**. Scores, tiers and greedy diversity
  were checked against the new policy, not exempted from verification. Runs 2/3
  were additionally exactly equal to run 1 except temporary preview URLs.
- Primary decode, analyzed-frame, detection and cache-write-failure counters
  were zero. Raw Matcher results were reused. All 3936 head and 95 action
  requests were cached, and those review stages created no inference models.
  This does not claim that primary model session setup is already fully lazy.

Full analysis times were **11414/8606/8528 ms**, median **8606 ms**. Process wall
times were 12164/9327/9248 ms. This is an observed compatible-warm median, not a
claim that removing the identity factor itself caused a speed improvement.

## Quality limits

This corrects a mode-dependent identity bias; it is not a demonstrated
semantic-relevance improvement or probability calibration. Transferring the
already frozen training judgments by unchanged pair identity leaves the one
strong-anchor preference audit at **1/9 correct**. The previously selected head
turn moves from returned index 37 to 40; the weak repeated hand-behind-head pose
moves from 64 to 109. The first ten training pairs remain unchanged. Evidence:
`build/goal-identity-preview-after-02/transferred-training-preferences.json`.
Generic-pose prominence and the independent held-out misses remain unresolved.

The existing strong/possible tier describes available evidence under the chosen
identity policy; it must not be presented as independent human confirmation of
a useful montage parallel.
