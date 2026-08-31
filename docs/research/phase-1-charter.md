# Phase 1 charter and exit criteria

Originally versioned 30 August 2026 and restored with a governance correction
on 31 August 2026. This document is the reviewable Phase 1 contract. The
broader working programme in `.local/docs/PLAN.md` may add planning detail,
but it does not replace this version-controlled charter.

## Goal

Preserve and correctly label the exploratory pilot, finish the closest-prior-art
audit, and define a mathematically valid policy/capability comparison before
broad code generalization or theorem work.

## Provenance policy

- Raw measured data and generated tables, figures and PDFs stay out of Git.
- Analysis code, dependency locks, report source, the raw SHA-256 manifest and
  pilot provenance record are versioned.
- The preserved pilot raw data is immutable and covered by
  `bench/results/raw.sha256`; it is intended for the external deposits made
  during registration and final artifact release.
- `bench/verify_pilot.sh` is the one-command local reproduction entry point.
- The legacy pilot remains at `bench/results/raw/`. Every later campaign
  requires a unique ID and writes to
  `bench/results/campaigns/<campaign-id>/raw/`; output is non-overwriting.
- Pilot measurements are exploratory and must never be pooled with
  confirmatory campaign data.

## PR1 — pilot freeze and positioning

Required outputs:

- preserve and label all pilot raw files and environments, with a checksum
  manifest and an honest provenance record;
- regenerate every pilot table, figure and report with one command;
- document the statistical implementation and multiplicity families;
- maintain a primary-source claim/evidence matrix with closest mechanisms,
  exact locators and scoped differences;
- label every pilot entry point as exploratory, never confirmatory; and
- keep one version-controlled `paper/` manuscript source tree.

Acceptance:

- local reproduction verifies all raw checksums and succeeds with the
  documented command;
- every pilot number has raw-row, environment and analysis-source provenance;
- no pilot result is described as confirmatory;
- every claim dependency is versioned or checksummed and scheduled for
  deposit; and
- an independent human reader supplies durable approval evidence for the
  claim/evidence matrix.

## PR2 — semantic capability taxonomy

Required outputs:

- complete algebraic laws and the `compose(newer, older)` convention;
- a source-audited strategy capability matrix with named proof obligations;
- an explicit checkpoint query-projectability restriction;
- a minimal policy interface, law tests and a chronological element-wise
  oracle;
- SumAdd, MinAdd and AffineSum policies with exact arithmetic semantics and a
  defined machine-representation failure boundary;
- compile-time capability facts documented as claims rather than proofs; and
- a representation model R fixed before any lower-bound attempt.

Acceptance:

- all policy laws pass deterministic checks on admissible domains;
- every strategy row cites code and a later proof obligation;
- invalid generic-checkpoint and unsupported complexity claims are absent; and
- existing production SumAdd behavior remains unchanged on its documented
  domain.

## Gate G1 — positioning and model

Route B continues only if the matrix leaves a defensible contribution in C2-C5
and the capability model is internally consistent. If direct prior art
subsumes the proposed action-order and structural results, the programme moves
to the additive empirical fallback before theorem work.

The independent-reader decision is an external governance condition. It must
be preserved as a submitted PR review, reviewer-signed document, archived
reviewer message or equivalent durable reviewer-authored evidence. It may not
be self-attested by an implementation author or inferred from a merge commit.
The record must identify the reviewed material, decision, date, independence
basis and every required change with its disposition.

The corrected evidence assessment and current gate status are in
[phase-1-exit-review.md](phase-1-exit-review.md).

## Amendment record

- 30 August 2026: versioned the Phase 1 contract and provenance policy.
- 30 August 2026: required non-overwriting campaign IDs for post-pilot runs.
- 30 August 2026: PR #34 merged as `317dac7` and PR #35 as `f8a184f`.
- A later housekeeping commit `234f19f` removed the charter and exit record
  under an "outside repository" policy, creating a contradiction with the
  versioned-charter requirement.
- 31 August 2026: restored the charter as the authoritative public contract;
  completed the primary-source locator audit; corrected the Timeline Index
  characterization; and withdrew owner-recorded signoff as insufficient
  evidence.
- 31 August 2026: technical Phase 1 closure remains supportable, but formal
  Gate G1 reconfirmation is pending durable independent-human approval.
