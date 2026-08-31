# Phase 1 closure addendum and corrected exit review

Re-audited 31 August 2026 against the restored
[Phase 1 charter](phase-1-charter.md).

## Verdict

**Technical and evidence package: pass.** The primary-source audit, scoped
claim matrix, capability model and preserved exploratory pilot satisfy the
technical Phase 1 criteria.

**External governance: pending.** The repository and GitHub records do not
contain durable approval authored by an independent human reviewer. The former
owner-recorded assertion is not a signature or review link and has been
withdrawn. Consequently, formal Gate G1 is **not reconfirmed** by this
addendum.

PR #34 (`317dac7`) and PR #35 (`f8a184f`) remain valid delivery history.
Their merges do not substitute for the charter's independent-review
condition.

## Corrections completed

| Area | Status | Evidence |
| --- | --- | --- |
| Previously flagged primary texts | Pass | Kaplan, Timeline Index, Johnson and McGeoch were read and their flags removed from the [matrix](claim-evidence-matrix.md) |
| Locators for retained claims | Pass | Matrix §7a records an exact section/page or fixed-code symbol for every retained source |
| Closest precedent and scoped difference | Pass | Matrix §8 names both for C1-C5; universal novelty, empty-intersection and project optimality language was removed |
| Timeline Index characterization | Pass | Corrected to Event List plus Version Map, optional checkpoints and SUM/AVG/COUNT plus MIN/MAX/MEDIAN |
| Versioned governance authority | Pass | This charter and exit record were restored under `docs/research/`; the contradictory outside-repository statement was removed |
| Repository and Wiki status | Pending publication | README and both Wiki working copies are corrected; the canonical public Wiki still requires commit, push and remote verification |
| Owner-recorded signoff | Corrected, not satisfied | Matrix §10 now records the absence of reviewer-authored evidence instead of claiming approval |
| Final AI technical review | Pass with external condition open | [AI review record](phase-1-ai-review.md) recommends the package for human review, records all findings/dispositions and explicitly does not claim human approval |

## Pilot preservation and reproduction

The documented command `bench/verify_pilot.sh` was run successfully on 31
August 2026. It:

- verified all 96 manifested measured/provenance files against
  `bench/results/raw.sha256`;
- ran 26 analysis tests;
- loaded 3,324 recorded trials into 357 summary cells;
- regenerated the tables and figures; and
- rebuilt the 15-page exploratory-pilot report.

The 96-entry manifest covers `runs_*`, `memory_*`, `environment_*` and
`system_*`. Twelve locally present `structural_*.csv` files are
machine-independent values derived after the measurement campaign; they are
not measured raw data, are not consumed by the pilot report and are not
represented as checksummed historical inputs.

The initial rerun reproduced the core data hashes and every page/figure
render, but Matplotlib and pdfTeX embedded wall-clock metadata in their PDFs.
The verifier now pins `SOURCE_DATE_EPOCH` to the pilot date and forces the
report target to rebuild instead of accepting a cached PDF. Two subsequent
forced full runs produced identical artifact hashes:

| Artifact | SHA-256 |
| --- | --- |
| Report PDF | `6639468bdd9e037c00b1844a414b5a66cc2d520d97f1e1f505117160d802225a` |
| `summary/cells.csv` | `1c50ab48a554b08505f8ff7e641431bb106875615b3f2c6df75ec3fba1d08756` |
| `summary/comparisons.csv` | `da73a102f8f132643d94186c9fec60dbcdd55fd14db28701c4a217fd5d5c5c3c` |
| `tables/facts.tex` | `b0189513432cf6643b2bad3a38882b1533185d0352a4e75eb4a400544ff7f0e5` |

All generated figure PDF hashes also matched between the two pinned-epoch
runs. The report source and all pilot-facing documentation call the campaign
an **exploratory pilot**, never confirmatory evidence.

Campaign isolation was checked directly:

- `bench/run_campaign.sh` without a campaign ID exited with status 2; and
- a second benchmark invocation using an existing output tag exited with
  status 2 before overwriting any file.
- `run_confirmatory.sh` now also refuses a pre-existing system metadata file
  when its runs file is absent; `confirmatory_output_overwrite_guard` covers
  this partial-output regression.

The legacy pilot stays at `bench/results/raw/`; later campaigns use one
explicit `bench/results/campaigns/<campaign-id>/raw/` directory. Confirmatory
analysis receives an explicit campaign path and never pools the legacy pilot.

## Remaining Gate G1 action

The reported reviewer is Sunjare Zulfiker, but the prior independence and
approval statements are not yet corroborated by a reviewer-authored record.
To close Gate G1, preserve one PR review, signed document or archived
reviewer message containing:

1. reviewer identity and date;
2. confirmation that the reviewer did not author the reviewed Phase 1
   changes;
3. identification of the matrix, charter, capability taxonomy and Route B
   recommendation reviewed;
4. approve/request-changes/reject decision; and
5. every required change and its disposition, or an explicit statement that
   none were required.

After that evidence is linked, update matrix §10, this section and the Wiki,
then formally reconfirm Gate G1. No repository owner or automated agent can
complete that external-human step.
