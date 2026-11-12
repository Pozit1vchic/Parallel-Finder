"""Verify an all-people ranking-only update against the previous native report.

Allows intentional order/score/tier presentation changes while requiring exact
pair membership and unchanged measured/classification data. Replays greedy
diversity selection and optionally checks exact same-version repeat equality.
"""
import argparse
import json
import math
from pathlib import Path

from score_prediction_review import digest, load

KEY = ("leftSource", "rightSource", "leftTrackId", "rightTrackId",
       "leftStart", "leftEnd", "rightStart", "rightEnd")
PRESENTATION = {"id", "bestMatchScore", "redundancyPenalty", "matchTier",
                "continuityRank", "leftPreview", "rightPreview", "identityEvidenceRequired"}


def key(row):
    return tuple(row[k] for k in KEY)


def close(a, b):
    return type(a) in (int, float) and math.isfinite(a) and math.isclose(a, b, rel_tol=0, abs_tol=1e-12)


def verify(reference, candidate):
    before = {key(row): row for row in reference}
    after = {key(row): row for row in candidate}
    if len(before) != len(reference) or len(after) != len(candidate) or before.keys() != after.keys():
        raise ValueError("Pair membership changed or contains duplicates")
    old_position = {key(row): i for i, row in enumerate(reference)}
    used, remaining = set(), dict(after)
    for i, row in enumerate(candidate):
        previous = before[key(row)]
        if {k: v for k, v in previous.items() if k not in PRESENTATION} != {k: v for k, v in row.items() if k not in PRESENTATION}:
            raise ValueError("Measured or classification fields changed")
        expected = row["similarity"] * (.55 + .45 * row["evidenceQuality"])
        if row.get("identityEvidenceRequired") is not False:
            raise ValueError("All-people identity policy is not retained for range edits")
        if not close(row["bestMatchScore"], expected):
            raise ValueError("Identity-independent score formula mismatch")
        old_expected = expected * (1 if previous["identityVerified"] or previous["faceVerified"] else .85)
        if not close(previous["bestMatchScore"], old_expected):
            raise ValueError("Reference is not the expected historical identity-weighted policy")
        if row["matchTier"] != ("strong" if row["evidenceQuality"] >= .45 else "possible"):
            raise ValueError("All-people tier still depends on identity")
        if row["id"] != i:
            raise ValueError("Displayed ID does not follow returned order")
        def penalty(record):
            return .04 * sum(bool(record[name]) and record[name] in used
                             for name in ("leftSceneFamily", "rightSceneFamily"))
        expected_penalty = penalty(row)
        if not close(row["redundancyPenalty"], expected_penalty):
            raise ValueError("Diversity penalty mismatch")
        selected_score = row["bestMatchScore"] - expected_penalty
        if any(other["bestMatchScore"] - penalty(other) > selected_score + 1e-12 for other in remaining.values()):
            raise ValueError("Greedy diversity selected a weaker remaining candidate")
        used.update((row["leftSceneFamily"], row["rightSceneFamily"]))
        remaining.pop(key(row))
    if sorted(row["continuityRank"] for row in candidate) != list(range(len(candidate))):
        raise ValueError("Continuity order is not a permutation")
    return {"pairCount": len(candidate), "pairMembershipPreserved": True,
            "measuredAndClassificationFieldsUnchanged": True,
            "changedPositions": sum(old_position[key(row)] != i for i, row in enumerate(candidate)),
            "expectedIdentityIndependentScoresTiersAndDiversity": True}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reference", required=True)
    parser.add_argument("--candidate", required=True)
    parser.add_argument("--repeat-reference")
    parser.add_argument("--output", required=True)
    args = parser.parse_args()
    output = Path(args.output)
    if output.exists():
        raise ValueError("Use a new comparison artifact")
    candidate = load(args.candidate)["results"]
    result = verify(load(args.reference)["results"], candidate)
    if args.repeat_reference:
        strip = lambda rows: [{k: v for k, v in row.items() if k not in {"leftPreview", "rightPreview"}} for row in rows]
        if strip(load(args.repeat_reference)["results"]) != strip(candidate):
            raise ValueError("Same-version repeat output changed")
        result["sameVersionRepeatExactExceptTemporaryPreviewUrls"] = True
        result["repeatReferenceSha256"] = digest(args.repeat_reference)
    result.update(referenceSha256=digest(args.reference), candidateSha256=digest(args.candidate),
                  scope="All-people ranking-policy verification; not semantic-quality evaluation")
    output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
