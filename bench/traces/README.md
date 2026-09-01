# External workload traces

The harness replays any operation stream via `valseg_bench --trace FILE`
(reported as workload `WT`). This directory holds the tooling that produces
the registered external workload; generated trace files stay out of Git like
all campaign data.

## Search outcome

The registered search protocol (`docs/research/registered-protocol.md`,
section 8.2) looked for a public trace of range-add updates with historical
range-sum queries. None was found: published temporal-aggregation work
(SB-tree, MVSB-tree, Timeline Index) reports workload generators and
distribution parameters, not released operation logs. Per the protocol, the
external workload is therefore a distribution derived from published
criteria, and its absence as a real trace is reported as a limitation, not a
finding.

## The registered distribution

`make_external_distribution.py` derives each stream from the MVSB-tree
experimental criteria (Zhang et al., TODS 2008): ordered uniformly-keyed
interval insertions with additive values, and key-range/time-range SUM
queries over the accumulated history. The module docstring records which
parameters come from the paper's description and which mapping choices are
this repository's. `bench/h5_trace_draws.csv` registers twelve deterministic
draws (`WT01`–`WT12`); H5 evaluates both operation responses for all 24
draw-operation cells after twenty complete trials per draw, and fails closed
if any cell or trial is absent. PR7 must make
`bench/run_confirmatory.sh <id> trace` generate and replay every registered
draw under the fresh-process protocol.
