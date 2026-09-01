# PR6 independent statistical review

Status: **awaiting an independent reviewer.** This file is a review packet,
not an approval record. It must not be changed to approved by an author of the
statistical machinery.

## Scope and reproduction

The reviewer independently checks the decisions and failure policy in
`docs/research/registered-protocol.md` against:

```sh
uv run --frozen --project bench/analysis \
  python -m unittest discover -s bench/analysis/tests -v
```

The synthetic fixtures cover passing and failing effects, censoring, missing
H3/H4/H5 cells, all-zero Wilcoxon input, non-finite values, MixedLM fit failure,
mean/median and leave-one-out sensitivities, order regression, corrections,
H1 identities, and custody-separated blinding through the study-wide unblind
boundary. The blinding fixture must show that every machine's output manifest
exists before the custody map is opened and that named post-unblind input is
bound to the blinded copy by the custody manifest. The reviewer also confirms
that the registered pair is lexically rather than semantically ordered, both
possible broad-regime subjects are hashed, and the post-unblind directional
interpretation matches section 5 of the protocol.
The reviewer also inspects the twelve rows of `bench/h4_cells.csv`, the twelve
draws of `bench/h5_trace_draws.csv`, the sixteen rows of
`bench/sensitivity_cells.csv`, and the fixed execution order in
`bench/run_registered_analysis.sh`.

## Reviewer record (pending)

- Reviewer name and affiliation:
- Independence from authorship of the reviewed changes:
- Review date/time (UTC):
- Repository commit reviewed:
- Locked-environment result:
- Required changes:
- Disposition of every required change:
- Final decision (approve / changes required / reject):
- Durable approval reference or signature:

PR6 cannot be frozen or registered while any field above is pending.
