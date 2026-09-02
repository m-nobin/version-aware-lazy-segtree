# PR6 independent statistical review

Status: **approved at round 3 (1 September 2026).** The approval attaches to
the exact change set recorded below; the registration manifest must be
generated from the commit that contains it and nothing else in these files. Only the reviewer, never an author of the
statistical machinery, may change this file's final decision.

## Scope and reproduction

The reviewer independently checks the decisions and failure policy in
`docs/research/registered-protocol.md` against:

```sh
uv run --frozen --project bench/analysis \
  python -m unittest discover -s bench/analysis/tests -v
```

The synthetic fixtures cover passing and failing effects, censoring, missing
H3/H4/H5 cells, per-structure H3 inventories with registered caps, all-zero
Wilcoxon input, non-finite values and zero operation counts, real and mocked
MixedLM fit failure, mean/median and leave-one-out sensitivities, order
regression, Holm arithmetic, H1 identities, unavailable-decision records, and
custody-separated blinding through the study-wide unblind boundary including
the registered-pair check, the orientation column and the post-unblinding
output re-hash. The blinding fixture must show that every machine's output
manifest exists before the custody map is opened and that named post-unblind
input is bound to the blinded copy by the custody manifest. The reviewer also
confirms that the registered pair is lexically rather than semantically
ordered, both possible broad-regime subjects are hashed, and the post-unblind
directional interpretation matches section 5 of the protocol. The reviewer
also inspects the twelve rows of `bench/h4_cells.csv`, the twelve draws of
`bench/h5_trace_draws.csv`, the sixteen rows of `bench/sensitivity_cells.csv`,
and the fixed execution order of both halves of
`bench/run_registered_analysis.sh`.

## Round 1 record

- Reviewer: automated statistical reviewer acting for Sunjare Zulfiker at the
  repository owner's request, under the dated public amendment that permits
  automated pre-registration review (`docs/research/claim-evidence-matrix.md`).
- Independence: the reviewer session authored none of the reviewed changes and
  had read-only access to the repository.
- Review date/time (UTC): 2026-09-01T18:57Z.
- Repository commit reviewed: `2d6ee41` (`feat/protocol-hardening`, PR #47).
- Locked-environment result: 40/40 fixtures pass (Python 3.12.13, SciPy
  1.18.0, NumPy 2.5.2, pandas 3.0.5, statsmodels 0.15.0). Registries: twelve
  H4 rows, twelve H5 draws with 20 trials each, sixteen sensitivity rows, six
  primary rows.
- Decision table: items a to k (H2, H3, H4, H5, Wilcoxon, MixedLM, order,
  mean/median and leave-one-out, compiler/allocator, censoring, blinding)
  reproduced under the fixtures and eleven adversarial probes, except the two
  disagreements recorded below.
- Final decision (round 1): **changes required.**

### Required changes and dispositions

| # | Severity | Finding | Disposition |
| --- | --- | --- | --- |
| 1 | Blocking | H3 required every included structure to complete every hold-out cell, but `bench/capped_cells.csv` registers 23 point-only and 2 checkpointing caps, so the registered rule was almost certainly unsatisfiable and its failure also skipped H5. | Fixed. The prepare stage records the registered capped structure-cells inside the hold-out (`registered_capped_holdout_cells`, manifest schema 3); each structure's inventory is the hold-out minus its own caps, hashed into `expected_inventory_sha256`; `h3_decisions` checks the per-structure count; the custodian half runs H5 regardless of H3. Protocol section 4 H3 row and `cost-model.md` section 4 restated. |
| 2 | Required | Any fail-closed pre-unblinding stage (a capped 1e7 H4 or sensitivity pair, a non-converged MixedLM) blocked unblinding for every hypothesis. | Fixed. A `RegisteredAnalysisError` now writes `<stage>_unavailable.json` (reason, diagnostics, UTC) in place of the CSV; `hash_primary_outputs` accepts and hashes the record; both script halves continue and exit non-zero at the end. Protocol section 2 and hierarchical rule restated. |
| 3 | Required | An insufficient-pairs primary cell kept `meaningfully faster` beside `p = 1` and `underpowered = True`, against protocol line 187. | Fixed. Such a cell is classified `inconclusive`; its interval stays in the row. Protocol primary-family rule restated. |
| 4 | Required | The locked command took the named campaigns and custody directory as arguments from its first stage, so the person running blinded stages held the names. | Fixed. `run_registered_analysis.sh analyst` takes only opaque campaigns and labels; `run_registered_analysis.sh custodian` takes custody material. Protocol sections 5, 6 and 10 restated. |
| 5 | Required | A NaN per-operation response (`updates = 0`) was silently dropped by `paired_cell_ratios` while the hierarchical and sensitivity stages failed closed on the same input. | Fixed. `paired_cell_ratios` checks finiteness of every shared complete pair before any branch; the `.dropna()` calls are gone. Protocol section 2 defines the structurally incomplete row as any empty field. |
| 6 | Minor | `restrict_primary_effects` keyed on (workload, n, variant) without `axis`. | Fixed. It merges on the full registered cell key through `normalize_registered_cells`. |
| 7 | Minor | Nothing re-checked hashed pre-unblinding outputs after the named stages. | Fixed. `blind.py verify-outputs` re-hashes every listed file against `primary-results.json` and `unblinding.json`; the custodian half runs it last. |
| 8 | Minor | Unblinding never verified that the blinded contrast resolves to `{persistent, copy-on-push}`. | Fixed. `unblind` reads the opaque pair from the blinded outputs and refuses any other resolution. |
| 9 | Minor | The protocol cited `pr6-statistical-review.md`; the file is `statistical-review.md`, and `make_registration.sh` would have aborted on it. | Fixed in both files. |
| 10 | Note | `uv.lock` resolves two SciPy/NumPy versions by Python version. | Documented in the protocol Wilcoxon rule; both machines run Python 3.12 or later, and every stage table now records `scipy_version` and `numpy_version`. A single-version relock needs network access and is deferred. |
| 11 | Note | Fixture gaps: Holm arithmetic, real MixedLM singularity, insufficient-pairs classification, orientation rule, unavailable path. | Fixtures added for each; the shell lexical guard is exercised manually (`S02 S01`, `S01 S01` exit 2). The suite is 53 fixtures. |
| 12 | Note | Whether PR7 emits external-adapter rows into the timing CSVs for the checksum stage. | PR7 scope; carried to the PR7 dry-run acceptance list. |

## Round 2 record

- Reviewer and independence: as round 1; the reviewer session again authored
  none of the applied changes.
- Review date/time (UTC): 2026-09-01, after the round 1 dispositions.
- Repository state reviewed: `2d6ee41` plus the uncommitted working tree
  (11 files, 789 insertions, 189 deletions).
- Locked-environment result: 53/53 fixtures pass.
- Verification of each disposition: items 1, 3 to 9 and 11 resolved; item 2
  resolved for every `confirm.py` stage and partially for the custodian half;
  item 10 resolved as documented; item 12 carried to PR7. Probes P2a, P2e,
  P2f, P3a, P6, P9, U1 to U4, an end-to-end four-campaign run with three
  unavailable decisions through unblinding and output verification, and eight
  malformed script invocations all behaved as registered.
- Final decision (round 2): **changes required (narrow)**, on new finding N1.

### Round 2 findings and dispositions

| # | Severity | Finding | Disposition |
| --- | --- | --- | --- |
| N1 | Required | The custodian half halted on an unwrapped `cost_model.py` evaluate or transfer failure, and a missing prediction file raised an uncaught error with no record, so the restated "H5 runs regardless of H3" and "every other decision produced" promises overstated the script. | Fixed. Evaluate and transfer are wrapped like the decision stages; a missing or unverifiable prediction CSV or model artifact raises `RegisteredAnalysisError` and writes the h3/h5 record. Protocol section 2 states that a prepare/fit failure makes both H3 and H5 unavailable. Fixture `test_missing_h3_inputs_write_unavailable_record`. |
| N2 | Minor | A crash outside the registered rules leaves neither output nor record, and the protocol did not say what happens then. | Documented in protocol section 2: unblinding refuses until the defect is fixed and the stage rerun; every stage removes its own stale output or record first; the rerun is a recorded deviation. |
| N3 | Minor | `contrast_labels` infers the registered pair from single-valued subject/baseline CSVs, which is fragile for a campaign with only two structures. | Deferred with the invariant recorded: the registered schedule runs all nine structures (`blind.STRUCTURES`) in every campaign, so the regime files are never single-valued. `assert_blinded` only checks that labels are opaque; it does not enforce this. An explicit hashed contrast file is a PR7 dry-run acceptance item. |
| N4 | Minor | The capped registry has no `axis` column, so the H3 exclusion keys on `(workload, n, variant)`. | Recorded as an invariant: the prepare stage now refuses a hold-out inventory in which `(workload, n, variant)` does not identify one cell; the protocol H3 row states the rule. |
| N5 | Minor | A stale `h4_*_disagreements.csv` from an earlier successful run survived a later H4 failure. | Fixed. The H4 stage removes it before computing. Fixture `test_failed_h4_removes_stale_disagreement_table`. |
| N6 | Note | Nothing enforces the protocol's Python 3.12 or later requirement. | Deferred: raising `requires-python` needs a relock with network access; the recorded `scipy_version`/`numpy_version` columns make it auditable. To be done with the registration relock. |

## Round 3 record

- Reviewer and independence: as rounds 1 and 2; authored none of the
  reviewed changes; read-only access throughout.
- Review date/time (UTC): 2026-09-01T19:20Z.
- Repository state reviewed: `2d6ee41` plus the uncommitted working tree
  (11 files, 873 insertions, 191 deletions; no untracked files).
- Locked-environment result: 55/55 fixtures pass.
- Verification of N1 to N6: N1, N4 and N5 resolved (N1 demonstrated end to
  end on the custodian half with a failing evaluate, a failing transfer and a
  missing prediction file: both records written, output verification ran,
  exit status listed every unavailable decision); N2 resolved as documented;
  N3 and N6 deferred acceptably with their invariants recorded.
- Notes without required change: the h3/h5 record reason names the missing
  input rather than the upstream cost-model error (the custodian's stderr
  holds it); N4 lacked a negative fixture (added afterwards as
  `test_ambiguous_holdout_cell_key_is_rejected`); the N3 wording corrected
  above.
- Final decision: **approve.** Every round 1 and round 2 finding is fixed
  with a passing fixture or explicitly deferred with its invariant written
  down, and the protocol text describes what the script and code do.
- Durable approval reference: this section, frozen by the registration
  manifest hash of this file; the review session's transcript is retained by
  the repository owner.
- Conditions of scope: the deferred items N3 and N6 stay on the PR7 dry-run
  and registration relock lists.
