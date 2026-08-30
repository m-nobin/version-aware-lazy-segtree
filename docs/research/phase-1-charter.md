# Phase 1 charter and exit criteria

Versioned 30 August 2026 from the Route B research programme. This document is
the reviewable Phase 1 contract; the broader working programme remains in
`.local/docs/PLAN.md`.

## Goal

Preserve and correctly label the exploratory pilot, finish the closest-prior-art
audit, and define a mathematically valid policy/capability comparison before
broad code generalization or theorem work.

## Provenance policy

- Raw measured data and generated tables, figures and PDFs stay out of Git.
- Analysis code, dependency locks, report source, the raw SHA-256 manifest and
  pilot provenance record are versioned.
- The local pilot raw data is immutable and covered by
  `bench/results/raw.sha256`; it is scheduled for the external deposits created
  during protocol registration and final artifact release.
- `bench/verify_pilot.sh` is the one-command local reproduction entry point.
- New measurements require a unique campaign ID. The runner and benchmark
  binary refuse to overwrite existing campaign output.

This policy supersedes the earlier interpretation that analysis/report source
should be ignored along with data. Generated artifacts are rebuildable; source
and dependency locks are not.

## PR1 — pilot freeze and positioning

Required outputs:

- preserve and label all pilot raw files and environments, with a checksum
  manifest and an honest provenance record;
- regenerate every pilot table, figure and the report with one command;
- document the Wilcoxon/BH/Holm implementation and multiplicity families;
- maintain `claim-evidence-matrix.md` with closest mechanisms and exact
  differences, based on primary SB-tree/MVSB-tree reading and direct generic
  implementation audit;
- label every report entry point as exploratory, never confirmatory; and
- keep one version-controlled `paper/` manuscript source tree.

Acceptance:

- local pilot reproduction verifies the raw manifest and succeeds with one
  documented command;
- every pilot number has raw-row, environment and analysis-source provenance;
- no pilot result is described as confirmatory;
- every claim dependency is versioned or checksummed and scheduled for deposit;
  and
- an independent human reader signs the claim-evidence matrix.

## PR2 — semantic capability taxonomy

Required outputs:

- complete algebraic laws and the `compose(newer, older)` convention;
- a source-audited strategy capability matrix with named proof obligations;
- an explicit checkpoint query-projectability restriction;
- a minimal policy interface, law tests and a chronological element-wise
  oracle;
- SumAdd, MinAdd and AffineSum policies with exact arithmetic semantics and a
  defined machine-representation failure boundary;
- compile-time capability facts that are documented as claims, not proofs; and
- a representation model R fixed before the lower-bound attempt.

Acceptance:

- all policy laws pass deterministic exhaustive/property checks on admissible
  domains, with edge tests for representation and modular arithmetic;
- every strategy row cites code and a later proof obligation;
- invalid generic-checkpoint and unsupported version-stamped complexity claims
  are absent; and
- existing production SumAdd behavior, validation order and exception
  contracts are unchanged on their documented domain.

## Gate G1 — positioning and model

Route B continues only if the claim matrix leaves a defensible contribution in
C2–C5 and the capability model is internally consistent. If direct prior art
subsumes the action-order boundary and structural results, the programme moves
to the additive empirical fallback before implementing theorem work.

The independent-reader signature is an external governance condition. It may
not be self-attested by an implementation author or automated test.

The evidence-backed implementation assessment and final action list are in
[phase-1-exit-review.md](phase-1-exit-review.md).

## Amendment record

- 30 August 2026: versioned this Phase 1 contract.
- 30 August 2026: corrected the provenance policy to version analysis/report
  sources while keeping raw and generated artifacts outside Git.
- 30 August 2026: required non-overwriting campaign IDs after the preserved
  pilot was found to use fixed output paths.
- 30 August 2026: resolved the remaining primary-source flags and added the
  Phase 1 exit review and independent-review record.
- 30 August 2026: corrected the taxonomy's action-order statement, added the
  remaining audit-minimum rows and eight-field audit table, and recorded the
  independent reader's approval.
